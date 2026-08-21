#include "vcames/engine.h"

#include "vcames/ffmpeg_source.h"
#include "vcames/image_transform.h"
#include "vcames/stream_source.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <system_error>
#include <utility>

namespace vcames {
namespace {

constexpr size_t kMaxFrameBytes = 16 * 1024 * 1024;
constexpr int kInternalJpegQuality = 90;

int64_t SteadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Engine::~Engine() {
    Stop();
}

bool Engine::Start(const Config& config, std::string* error) {
    if (!config.Validate(error)) {
        return false;
    }
    Stop();
    if (!sink_.Open(config.video_device, config.width, config.height,
                    config.fps, kMaxFrameBytes, error)) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        config_ = config;
        status_ = RuntimeStatus{};
        status_.running = true;
        status_.source_connected = false;
        status_.sink_open = true;
        if (config.url == "push://local") {
            status_.source_label = "LOCAL_SAF";
        } else if (config.url == "push://placeholder") {
            status_.source_label = "GLOBAL_PLACEHOLDER";
        } else {
            StreamSourceSpec source;
            std::string ignored;
            status_.source_label = ParseStreamSourceUrl(config.url, &source, &ignored)
                    ? source.label : "UNKNOWN";
        }
        SourceFrame placeholder;
        const size_t pixels = static_cast<size_t>(config.width)
                * static_cast<size_t>(config.height);
        placeholder.payload.assign(pixels * 3 / 2, 128);
        std::fill(placeholder.payload.begin(), placeholder.payload.begin() + pixels, 16);
        placeholder.format = SourceFormat::kNv21;
        placeholder.width = config.width;
        placeholder.height = config.height;
        placeholder.presentation_time_ns = SteadyNowNs();
        placeholder.arrival_time_ns = placeholder.presentation_time_ns;
        latest_frame_ = std::make_shared<const SourceFrame>(std::move(placeholder));
        latest_generation_ = 1;
        status_.last_frame_time = std::chrono::steady_clock::now();
        stop_requested_.store(false, std::memory_order_relaxed);
    }
    try {
        writer_thread_ = std::thread(&Engine::WriterLoop, this);
        if (config.url != "push://local" && config.url != "push://placeholder") {
            source_thread_ = std::thread(&Engine::SourceLoop, this);
        }
    } catch (const std::system_error& exception) {
        if (error != nullptr) {
            *error = std::string("unable to create worker thread: ") + exception.what();
        }
        Stop();
        return false;
    }
    return true;
}

bool Engine::WaitForFirstFrameWritten(
        std::chrono::milliseconds timeout,
        std::string* error) {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, timeout, [this] {
        return status_.frames_written > 0 || !status_.running || !status_.sink_open;
    });
    if (status_.frames_written > 0) {
        return true;
    }
    if (error != nullptr) {
        *error = status_.error.empty()
                ? "V4L2 placeholder did not produce its first frame in time"
                : status_.error;
    }
    return false;
}

void Engine::Stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
    condition_.notify_all();
    if (source_thread_.joinable()) {
        source_thread_.join();
    }
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    std::lock_guard lock(mutex_);
    status_.running = false;
    status_.source_connected = false;
    status_.sink_open = false;
    latest_frame_.reset();
    sink_.Close();
}

bool Engine::PushFrame(std::vector<uint8_t>&& jpeg, std::string* error) {
    {
        std::lock_guard lock(mutex_);
        if (!status_.running || config_.url != "push://local") {
            if (error != nullptr) {
                *error = "local frame producer is not active";
            }
            return false;
        }
    }
    if (jpeg.size() < 4 || jpeg.size() > kMaxFrameBytes) {
        if (error != nullptr) {
            *error = "local JPEG frame is empty or too large";
        }
        return false;
    }
    SourceFrame frame;
    frame.payload = std::move(jpeg);
    frame.format = SourceFormat::kJpeg;
    frame.presentation_time_ns = SteadyNowNs();
    frame.arrival_time_ns = frame.presentation_time_ns;
    PublishFrame(std::move(frame));
    return true;
}

bool Engine::PushNv21Frame(
        std::vector<uint8_t>&& nv21,
        int width,
        int height,
        int64_t presentation_time_ns,
        std::string* error) {
    {
        std::lock_guard lock(mutex_);
        if (!status_.running || config_.url != "push://local") {
            if (error != nullptr) {
                *error = "local frame producer is not active";
            }
            return false;
        }
    }
    const size_t pixels = width > 0 && height > 0
            ? static_cast<size_t>(width) * static_cast<size_t>(height)
            : 0;
    if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0
            || pixels > 3840u * 2160u || nv21.size() != pixels * 3 / 2
            || nv21.size() > kMaxFrameBytes) {
        if (error != nullptr) {
            *error = "local NV21 frame metadata or payload is invalid";
        }
        return false;
    }
    SourceFrame frame;
    frame.payload = std::move(nv21);
    frame.format = SourceFormat::kNv21;
    frame.width = width;
    frame.height = height;
    frame.presentation_time_ns = presentation_time_ns;
    frame.arrival_time_ns = SteadyNowNs();
    PublishFrame(std::move(frame));
    return true;
}

std::string Engine::StatusJson() const {
    std::lock_guard lock(mutex_);
    long long age_ms = -1;
    if (status_.last_frame_time.time_since_epoch().count() != 0) {
        age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - status_.last_frame_time).count();
    }
    std::ostringstream json;
    json << "{\"running\":" << (status_.running ? "true" : "false")
         << ",\"source\":\"" << JsonEscape(status_.source_label) << "\""
         << ",\"connected\":" << (status_.source_connected ? "true" : "false")
         << ",\"camera_ready\":" << (status_.sink_open ? "true" : "false")
         << ",\"replacement_scope\":\"global-front-back\""
         << ",\"replacement_verification\":\"pixel5-legacy-provider-takeover\""
         << ",\"camera_ids\":[\"0\",\"1\"]"
         << ",\"transport\":\"ffmpeg-to-v4l2-to-global-provider\""
         << ",\"frame_format\":\"" << JsonEscape(status_.frame_format) << "\""
         << ",\"received\":" << status_.frames_received
         << ",\"written\":" << status_.frames_written
         << ",\"dropped\":" << status_.frames_dropped
         << ",\"source_size\":\"" << status_.source_width << "x" << status_.source_height << "\""
         << ",\"age_ms\":" << age_ms
         << ",\"error\":\"" << JsonEscape(status_.error) << "\"}";
    return json.str();
}

void Engine::SourceLoop() {
    StreamSourceSpec source_spec;
    std::string source_parse_error;
    if (!ParseStreamSourceUrl(config_.url, &source_spec, &source_parse_error)) {
        std::lock_guard lock(mutex_);
        status_.error = std::move(source_parse_error);
        return;
    }
    FfmpegSource source;
    int backoff_seconds = 1;
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::string stream_error;
        bool connected_once = false;
        source.Stream(
                config_.ffmpeg_path,
                config_.url,
                config_.fps,
                source_spec,
                stop_requested_,
                [this](std::vector<uint8_t>&& jpeg) {
                    SourceFrame frame;
                    frame.payload = std::move(jpeg);
                    frame.format = SourceFormat::kJpeg;
                    frame.presentation_time_ns = SteadyNowNs();
                    frame.arrival_time_ns = frame.presentation_time_ns;
                    PublishFrame(std::move(frame));
                },
                [this, &connected_once]() {
                    connected_once = true;
                    std::lock_guard lock(mutex_);
                    status_.source_connected = true;
                    status_.error.clear();
                },
                &stream_error);
        if (stop_requested_.load(std::memory_order_relaxed)) {
            break;
        }
        if (connected_once) {
            backoff_seconds = 1;
        }
        {
            std::lock_guard lock(mutex_);
            status_.source_connected = false;
            status_.error = std::move(stream_error);
        }
        if (WaitForStop(std::chrono::seconds(backoff_seconds))) {
            break;
        }
        backoff_seconds = std::min(backoff_seconds * 2, 30);
    }
}

void Engine::WriterLoop() {
    std::vector<uint8_t> output_jpeg;
    uint64_t consumed_generation = 0;
    const auto frame_interval = std::chrono::nanoseconds(1'000'000'000LL / config_.fps);
    auto next_write = std::chrono::steady_clock::now();

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::shared_ptr<const SourceFrame> source_frame;
        uint64_t generation = consumed_generation;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_write,
                                  [this, consumed_generation, &output_jpeg] {
                return stop_requested_.load(std::memory_order_relaxed)
                        || (output_jpeg.empty()
                            && latest_generation_ != consumed_generation);
            });
            if (stop_requested_.load(std::memory_order_relaxed)) {
                break;
            }
            const auto before_tick = std::chrono::steady_clock::now();
            if (before_tick < next_write) {
                condition_.wait_until(lock, next_write, [this] {
                    return stop_requested_.load(std::memory_order_relaxed);
                });
                if (stop_requested_.load(std::memory_order_relaxed)) {
                    break;
                }
            }
            source_frame = latest_frame_;
            generation = latest_generation_;
            if (generation > consumed_generation + 1) {
                status_.frames_dropped += generation - consumed_generation - 1;
            }
        }

        if (source_frame != nullptr && generation != consumed_generation) {
            TransformOptions options{
                .width = config_.width,
                .height = config_.height,
                .rotation = config_.rotation,
                .mirror = config_.mirror,
                .jpeg_quality = kInternalJpegQuality,
            };
            std::string transform_error;
            bool transformed = false;
            int source_width = source_frame->width;
            int source_height = source_frame->height;
            std::vector<uint8_t> transformed_nv21;
            output_jpeg.clear();
            if (source_frame->format == SourceFormat::kJpeg) {
                if (ReadJpegDimensions(
                            source_frame->payload,
                            &source_width,
                            &source_height,
                            &transform_error)
                        && source_width == config_.width
                        && source_height == config_.height
                        && config_.rotation == 0 && !config_.mirror) {
                    output_jpeg = source_frame->payload;
                    transformed = true;
                } else {
                    transform_error.clear();
                    transformed = TransformJpegToNv21(
                            source_frame->payload,
                            options,
                            &transformed_nv21,
                            &source_width,
                            &source_height,
                            &transform_error);
                }
            } else if (source_frame->format == SourceFormat::kNv21) {
                transformed = TransformNv21(
                        source_frame->payload,
                        source_frame->width,
                        source_frame->height,
                        options,
                        &transformed_nv21,
                        &transform_error);
            }
            if (transformed && output_jpeg.empty()) {
                transformed = Nv21ToJpeg(
                        transformed_nv21,
                        config_.width,
                        config_.height,
                        kInternalJpegQuality,
                        &output_jpeg,
                        &transform_error);
            }
            if (transformed) {
                std::lock_guard lock(mutex_);
                status_.source_width = source_width;
                status_.source_height = source_height;
                status_.frame_format = "mjpeg";
                status_.error.clear();
            } else {
                std::lock_guard lock(mutex_);
                status_.error = std::move(transform_error);
                ++status_.frames_dropped;
                output_jpeg.clear();
            }
            consumed_generation = generation;
        }

        const auto now = std::chrono::steady_clock::now();
        if (output_jpeg.empty()) {
            next_write = now + frame_interval;
            continue;
        }

        std::string sink_error;
        if (!sink_.Write(output_jpeg, &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            status_.error = std::move(sink_error);
            condition_.notify_all();
            next_write = now + frame_interval;
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            status_.sink_open = true;
            ++status_.frames_written;
        }
        condition_.notify_all();
        next_write += frame_interval;
        if (next_write < now - frame_interval) {
            next_write = now + frame_interval;
        }
    }
}

void Engine::PublishFrame(SourceFrame&& frame) {
    if (frame.payload.size() < 4 || frame.payload.size() > kMaxFrameBytes) {
        std::lock_guard lock(mutex_);
        ++status_.frames_dropped;
        status_.error = "received frame is empty or exceeds safety limit";
        return;
    }
    auto next = std::make_shared<const SourceFrame>(std::move(frame));
    {
        std::lock_guard lock(mutex_);
        latest_frame_ = std::move(next);
        ++latest_generation_;
        ++status_.frames_received;
        status_.source_connected = true;
        status_.last_frame_time = std::chrono::steady_clock::now();
    }
    condition_.notify_one();
}

bool Engine::WaitForStop(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, duration, [this] {
        return stop_requested_.load(std::memory_order_relaxed);
    });
}

}  // namespace vcames
