#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vcames {

// Writes an MJPEG stream into the exact-kernel v4l2loopback device consumed by
// Android's external camera provider. Opening the sink is part of START so the
// command fails before the global Provider can consume frames when unavailable.
class V4l2Sink {
public:
    V4l2Sink() = default;
    ~V4l2Sink();
    V4l2Sink(const V4l2Sink&) = delete;
    V4l2Sink& operator=(const V4l2Sink&) = delete;

    bool Open(
            const std::string& device,
            int width,
            int height,
            int fps,
            size_t max_frame_bytes,
            std::string* error);
    bool Write(const std::vector<uint8_t>& jpeg, std::string* error);
    void Close();
    bool is_open() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

}  // namespace vcames
