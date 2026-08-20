#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vcames {

class HttpMjpegSource {
public:
    using FrameCallback = std::function<void(std::vector<uint8_t>&&)>;
    using ConnectedCallback = std::function<void()>;

    bool Stream(
            const std::string& url,
            const std::atomic<bool>& stop_requested,
            FrameCallback frame_callback,
            ConnectedCallback connected_callback,
            std::string* error) const;
};

}  // namespace vcames
