#pragma once

#include <string>
#include <vector>

namespace vcames {

struct StreamSourceSpec {
    std::string scheme;
    std::string label;
    bool rtsp_transport = false;
};

// Accepts network streaming transports only. Local files stay behind Android's
// Storage Access Framework and are delivered through push://local.
bool ParseStreamSourceUrl(
        const std::string& url,
        StreamSourceSpec* spec,
        std::string* error);

std::vector<std::string> BuildFfmpegArguments(
        const std::string& ffmpeg_path,
        const std::string& url,
        int fps,
        const StreamSourceSpec& spec);

const char* FfmpegProtocolWhitelist();

}  // namespace vcames
