#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vcames {

class MjpegParser {
public:
    using FrameCallback = std::function<void(std::vector<uint8_t>&&)>;

    explicit MjpegParser(FrameCallback callback, size_t max_frame_bytes = 16 * 1024 * 1024);
    bool Append(const uint8_t* data, size_t size, std::string* error);
    void Reset();

private:
    FrameCallback callback_;
    size_t max_frame_bytes_;
    std::vector<uint8_t> buffer_;
};

}  // namespace vcames
