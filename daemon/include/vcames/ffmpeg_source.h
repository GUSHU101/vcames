#pragma once

#include "vcames/stream_source.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vcames {

class FfmpegSource {
public:
    using FrameCallback = std::function<void(std::vector<uint8_t>&&)>;
    using ConnectedCallback = std::function<void()>;

    bool Stream(
            const std::string& ffmpeg_path,
            const std::string& url,
            int fps,
            const StreamSourceSpec& spec,
            const std::atomic<bool>& stop_requested,
            FrameCallback frame_callback,
            ConnectedCallback connected_callback,
            std::string* error) const;
};

}  // namespace vcames
