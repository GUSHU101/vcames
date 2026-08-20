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

constexpr size_t kMaxJpegBytes = 16 * 1024 * 1024;

}  // namespace

Engine::~Engine() {
    Stop();
}

bool Engine::Start(const Config& config, std::string* error) {
    if (!config.Validate(error)) {
        return false;
    }
    Stop();
    {
        std::lock_guard lock(mutex_);
        config_ = config;
        status_ = RuntimeStatus{};
        status_.running = true;
        status_.source_connected = config.url == "push://local";
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
    latest_frame_.reset();
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
    if (jpeg.size() < 4 || jpeg.size() > kMaxJpegBytes) {
        if (error != nullptr) {
            *error = "local JPEG frame is empty or too large";
        }
        return false;
    }
    PublishFrame(std::move(jpeg));
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
         << ",\"source\":\"" << (config_.url == "push://local" ? "local" : "mjpeg") << "\""
         << ",\"connected\":" << (status_.source_connected ? "true" : "false")
         << ",\"camera_ready\":" << (status_.sink_open ? "true" : "false")
         << ",\"target\":\"" << JsonEscape(config_.target) << "\""
         << ",\"received\":" << status_.frames_received
         << ",\"written\":" << status_.frames_written
         << ",\"dropped\":" << status_.frames_dropped
         << ",\"source_size\":\"" << status_.source_width << "x" << status_.source_height << "\""
         << ",\"age_ms\":" << age_ms
         << ",\"error\":\"" << JsonEscape(status_.error) << "\"}";
    return json.str();
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
                [this](std::vector<uint8_t>&& jpeg) { PublishFrame(std::move(jpeg)); },
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
        // A source that was healthy should get a fast first reconnect. Without
        // this reset, an old outage leaves later reconnects at the 30 s ceiling.
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
    std::vector<uint8_t> processed_frame;
    uint64_t consumed_generation = 0;
    const auto frame_interval = std::chrono::nanoseconds(1'000'000'000LL / config_.fps);
    auto next_write = std::chrono::steady_clock::now();

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::shared_ptr<const std::vector<uint8_t>> source_frame;
        uint64_t generation = consumed_generation;
        std::chrono::steady_clock::time_point last_frame_time;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_write,
                                  [this, consumed_generation, &processed_frame] {
                return stop_requested_.load(std::memory_order_relaxed)
                        || (processed_frame.empty()
                            && latest_generation_ != consumed_generation);
            });
            if (stop_requested_.load(std::memory_order_relaxed)) {
                break;
            }

            // A newly published frame can wake an idle writer before its next
            // scheduled tick. Keep the configured FPS authoritative and take
            // the newest snapshot only when that tick is actually due.
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
            int source_width = 0;
            int source_height = 0;
            if (TransformJpeg(
                        *source_frame,
                        options,
                        &processed_frame,
                        &source_width,
                        &source_height,
                        &transform_error)) {
                std::lock_guard lock(mutex_);
                status_.source_width = source_width;
                status_.source_height = source_height;
                status_.error.clear();
            } else {
                std::lock_guard lock(mutex_);
                status_.error = std::move(transform_error);
                ++status_.frames_dropped;
                processed_frame.clear();
            }
            consumed_generation = generation;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool stale = last_frame_time.time_since_epoch().count() == 0
                || now - last_frame_time > std::chrono::milliseconds(config_.stale_timeout_ms);
        if (stale && !config_.hold_last) {
            sink.Close();
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            next_write = now + frame_interval;
            continue;
        }
        if (processed_frame.empty()) {
            next_write = now + frame_interval;
            continue;
        }

        std::string sink_error;
        if (!sink.is_open()
                && !sink.Open(
                        config_.device,
                        config_.width,
                        config_.height,
                        config_.fps,
                        kMaxJpegBytes,
                        &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.sink_open = false;
            status_.error = std::move(sink_error);
            next_write = now + std::chrono::seconds(1);
            continue;
        }
        if (sink.Write(processed_frame, &sink_error)) {
            std::lock_guard lock(mutex_);
            status_.sink_open = true;
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

void Engine::PublishFrame(std::vector<uint8_t>&& jpeg) {
    if (jpeg.size() < 4 || jpeg.size() > kMaxJpegBytes) {
        std::lock_guard lock(mutex_);
        ++status_.frames_dropped;
        status_.error = "received JPEG frame is empty or exceeds safety limit";
        return;
    }
    auto frame = std::make_shared<const std::vector<uint8_t>>(std::move(jpeg));
    {
        std::lock_guard lock(mutex_);
        latest_frame_ = std::move(frame);
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
