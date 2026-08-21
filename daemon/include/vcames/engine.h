#pragma once

#include "vcames/config.h"
#include "vcames/v4l2_sink.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vcames {

class Engine {
public:
    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Start(const Config& config, std::string* error);
    void Stop();
    bool PushFrame(std::vector<uint8_t>&& jpeg, std::string* error);
    bool PushNv21Frame(
            std::vector<uint8_t>&& nv21,
            int width,
            int height,
            int64_t presentation_time_ns,
            std::string* error);
    bool WaitForFirstFrameWritten(
            std::chrono::milliseconds timeout,
            std::string* error);
    std::string StatusJson() const;

private:
    enum class SourceFormat {
        kJpeg,
        kNv21,
    };

    struct SourceFrame {
        std::vector<uint8_t> payload;
        SourceFormat format = SourceFormat::kJpeg;
        int width = 0;
        int height = 0;
        int64_t presentation_time_ns = 0;
        int64_t arrival_time_ns = 0;
    };

    struct RuntimeStatus {
        bool running = false;
        bool source_connected = false;
        bool sink_open = false;
        uint64_t frames_received = 0;
        uint64_t frames_written = 0;
        uint64_t frames_dropped = 0;
        int source_width = 0;
        int source_height = 0;
        std::string source_label = "none";
        std::string frame_format = "none";
        std::string error;
        std::chrono::steady_clock::time_point last_frame_time{};
    };

    void SourceLoop();
    void WriterLoop();
    void PublishFrame(SourceFrame&& frame);
    bool WaitForStop(std::chrono::milliseconds duration);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Config config_;
    RuntimeStatus status_;
    std::shared_ptr<const SourceFrame> latest_frame_;
    uint64_t latest_generation_ = 0;
    std::atomic<bool> stop_requested_{false};
    std::thread source_thread_;
    std::thread writer_thread_;
    V4l2Sink sink_;
};

}  // namespace vcames
