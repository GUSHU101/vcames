#pragma once

#include "vcames/config.h"
#include "vcames/shared_frame_bus.h"

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
    void SetReplacementAttached(bool attached);
    std::string StatusJson() const;
    int DuplicateFrameBusFd(std::string* error) const;
    std::string FrameBusDescriptor() const;

private:
    struct SourceFrame {
        std::vector<uint8_t> payload;
        SharedFrameBus::PixelFormat format = SharedFrameBus::PixelFormat::kJpeg;
        int width = 0;
        int height = 0;
        int64_t presentation_time_ns = 0;
        int64_t arrival_time_ns = 0;
    };

    struct RuntimeStatus {
        bool running = false;
        bool source_connected = false;
        bool sink_open = false;
        bool frame_bus_ready = false;
        bool replacement_attached = false;
        uint64_t frames_received = 0;
        uint64_t frames_written = 0;
        uint64_t frames_dropped = 0;
        int source_width = 0;
        int source_height = 0;
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
    SharedFrameBus frame_bus_;
};

}  // namespace vcames
