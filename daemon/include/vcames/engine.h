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
    std::string StatusJson() const;
    int DuplicateFrameBusFd(std::string* error) const;
    std::string FrameBusDescriptor() const;

private:
    struct RuntimeStatus {
        bool running = false;
        bool source_connected = false;
        bool sink_open = false;
        bool frame_bus_ready = false;
        uint64_t frames_received = 0;
        uint64_t frames_written = 0;
        uint64_t frames_dropped = 0;
        int source_width = 0;
        int source_height = 0;
        std::string error;
        std::chrono::steady_clock::time_point last_frame_time{};
    };

    void SourceLoop();
    void WriterLoop();
    void PublishFrame(std::vector<uint8_t>&& jpeg);
    bool WaitForStop(std::chrono::milliseconds duration);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Config config_;
    RuntimeStatus status_;
    std::shared_ptr<const std::vector<uint8_t>> latest_frame_;
    uint64_t latest_generation_ = 0;
    std::atomic<bool> stop_requested_{false};
    std::thread source_thread_;
    std::thread writer_thread_;
    SharedFrameBus frame_bus_;
};

}  // namespace vcames
