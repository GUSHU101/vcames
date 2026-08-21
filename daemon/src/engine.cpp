#include "vcames/engine.h"

#include "vcames/http_mjpeg_source.h"
#include "vcames/image_transform.h"
#include "vcames/v4l2_sink.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <system_error>
#include <utility>

namespace vcames {
namespace {

constexpr size_t kMaxFrameBytes = 16 * 1024 * 1024;

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
    if (!frame_bus_.Open(SharedFrameBus::kDefaultSlotCapacity, error)) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        config_ = config;
        status_ = RuntimeStatus{};
        status_.running = true;
        status_.source_connected = config.url == "push://local";
        status_.frame_bus_ready = true;
        latest_frame_.reset();
        latest_generation_ = 0;
        stop_requested_.store(false, std::memory_order_relaxed);
    }
    try {
        writer_thread_ = std::thread(&Engine::WriterLoop, this);
        if (config.url != "push://local") {
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
    status_.frame_bus_ready = false;
    status_.replacement_attached = false;
    latest_frame_.reset();
    frame_bus_.Close();
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
    frame.format = SharedFrameBus::PixelFormat::kJpeg;
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
    frame.format = SharedFrameBus::PixelFormat::kNv21;
    frame.width = width;
    frame.height = height;
    frame.presentation_time_ns = presentation_time_ns;
    frame.arrival_time_ns = SteadyNowNs();
    PublishFrame(std::move(frame));
    return true;
}

void Engine::SetReplacementAttached(bool attached) {
    std::lock_guard lock(mutex_);
    status_.replacement_attached = attached;
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
         << ",\"source\":\"" << (config_.url == "push://local" ? "local" : "mjpeg") << "\""
         << ",\"connected\":" << (status_.source_connected ? "true" : "false")
         << ",\"camera_ready\":" << (status_.sink_open ? "true" : "false")
         << ",\"frame_bus_ready\":" << (status_.frame_bus_ready ? "true" : "false")
         << ",\"replacement_attached\":"
         << (status_.replacement_attached ? "true" : "false")
         << ",\"transport\":\"memfd-ring-v2\""
         << ",\"frame_format\":\"" << JsonEscape(status_.frame_format) << "\""
         << ",\"target\":\"" << JsonEscape(config_.target) << "\""
         << ",\"received\":" << status_.frames_received
         << ",\"written\":" << status_.frames_written
         << ",\"dropped\":" << status_.frames_dropped
         << ",\"source_size\":\"" << status_.source_width << "x" << status_.source_height << "\""
         << ",\"age_ms\":" << age_ms
         << ",\"error\":\"" << JsonEscape(status_.error) << "\"}";
    return json.str();
}

int Engine::DuplicateFrameBusFd(std::string* error) const {
    return frame_bus_.DuplicateFd(error);
}

std::string Engine::FrameBusDescriptor() const {
    return frame_bus_.Descriptor();
}

void Engine::SourceLoop() {
    HttpMjpegSource source;
    int backoff_seconds = 1;
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::string stream_error;
        bool connected_once = false;
        source.Stream(
                config_.url,
                stop_requested_,
                [this](std::vector<uint8_t>&& jpeg) {
                    SourceFrame frame;
                    frame.payload = std::move(jpeg);
                    frame.format = SharedFrameBus::PixelFormat::kJpeg;
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
    V4l2Sink sink;
    SharedFrameBus::Frame bus_frame;
    std::vector<uint8_t> external_frame;
    uint64_t consumed_generation = 0;
    const auto frame_interval = std::chrono::nanoseconds(1'000'000'000LL / config_.fps);
    auto next_write = std::chrono::steady_clock::now();

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::shared_ptr<const SourceFrame> source_frame;
        uint64_t generation = consumed_generation;
        std::chrono::steady_clock::time_point last_frame_time;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_write,
                                  [this, consumed_generation, &bus_frame] {
                return stop_requested_.load(std::memory_order_relaxed)
                        || (bus_frame.payload.empty()
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
            last_frame_time = status_.last_frame_time;
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
                .jpeg_quality = config_.jpeg_quality,
            };
            std::string transform_error;
            bool transformed = false;
            int source_width = source_frame->width;
            int source_height = source_frame->height;
            bus_frame = SharedFrameBus::Frame{};
            external_frame.clear();
            if (source_frame->format == SharedFrameBus::PixelFormat::kJpeg) {
                if (config_.target == "external") {
                    transformed = TransformJpeg(
                            source_frame->payload,
                            options,
                            &bus_frame.payload,
                            &source_width,
                            &source_height,
                            &transform_error);
                    bus_frame.format = SharedFrameBus::PixelFormat::kJpeg;
                    external_frame = bus_frame.payload;
                } else {
                    transformed = TransformJpegToNv21(
                            source_frame->payload,
                            options,
                            &bus_frame.payload,
                            &source_width,
                            &source_height,
                            &transform_error);
                    bus_frame.format = SharedFrameBus::PixelFormat::kNv21;
                    bus_frame.y_stride = static_cast<uint32_t>(config_.width);
                    bus_frame.uv_stride = static_cast<uint32_t>(config_.width);
                }
            } else if (source_frame->format == SharedFrameBus::PixelFormat::kNv21) {
                transformed = TransformNv21(
                        source_frame->payload,
                        source_frame->width,
                        source_frame->height,
                        options,
                        &bus_frame.payload,
                        &transform_error);
                bus_frame.format = SharedFrameBus::PixelFormat::kNv21;
                bus_frame.y_stride = static_cast<uint32_t>(config_.width);
                bus_frame.uv_stride = static_cast<uint32_t>(config_.width);
                if (transformed && config_.target == "external") {
                    transformed = Nv21ToJpeg(
                            bus_frame.payload,
                            config_.width,
                            config_.height,
                            config_.jpeg_quality,
                            &external_frame,
                            &transform_error);
                }
            }
            if (transformed) {
                bus_frame.width = static_cast<uint32_t>(config_.width);
                bus_frame.height = static_cast<uint32_t>(config_.height);
                bus_frame.presentation_time_ns = source_frame->presentation_time_ns;
                bus_frame.arrival_time_ns = source_frame->arrival_time_ns;
                std::lock_guard lock(mutex_);
                status_.source_width = source_width;
                status_.source_height = source_height;
                status_.frame_format = bus_frame.format == SharedFrameBus::PixelFormat::kNv21
                        ? "nv21"
                        : "jpeg";
                status_.error.clear();
            } else {
                std::lock_guard lock(mutex_);
                status_.error = std::move(transform_error);
                ++status_.frames_dropped;
                bus_frame.payload.clear();
                external_frame.clear();
            }
            consumed_generation = generation;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool stale = last_frame_time.time_since_epoch().count() == 0
                || now - last_frame_time > std::chrono::milliseconds(config_.stale_timeout_ms);
        if (stale && !config_.hold_last) {
            sink.Close();
            frame_bus_.Invalidate();
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            next_write = now + frame_interval;
            continue;
        }
        if (bus_frame.payload.empty()) {
            next_write = now + frame_interval;
            continue;
        }

        std::string sink_error;
        if (!frame_bus_.Publish(bus_frame, &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.frame_bus_ready = false;
            status_.sink_open = false;
            status_.error = std::move(sink_error);
            next_write = now + frame_interval;
            continue;
        }
        if (config_.target != "external") {
            std::lock_guard lock(mutex_);
            status_.frame_bus_ready = true;
            status_.sink_open = true;
            ++status_.frames_written;
            next_write += frame_interval;
            if (next_write < now - frame_interval) {
                next_write = now + frame_interval;
            }
            continue;
        }
        if (external_frame.empty()) {
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            status_.error = "external backend has no MJPEG frame";
            next_write = now + frame_interval;
            continue;
        }
        if (!sink.is_open()
                && !sink.Open(
                        config_.device,
                        config_.width,
                        config_.height,
                        config_.fps,
                        kMaxFrameBytes,
                        &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            status_.error = std::move(sink_error);
            next_write = now + std::chrono::seconds(1);
            continue;
        }
        if (sink.Write(external_frame, &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.sink_open = true;
            status_.frame_bus_ready = true;
            ++status_.frames_written;
        } else {
            sink.Close();
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            status_.error = std::move(sink_error);
        }
        next_write += frame_interval;
        if (next_write < now - frame_interval) {
            next_write = now + frame_interval;
        }
    }
    sink.Close();
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
