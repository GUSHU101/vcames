#include "vcames/stream_source.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_map>

namespace vcames {
namespace {

constexpr size_t kMaxUrlBytes = 4096;

const std::unordered_map<std::string_view, std::string_view> kSchemes = {
    {"http", "HTTP/HLS/DASH/MJPEG"},
    {"https", "HTTPS/HLS/DASH/MJPEG"},
    {"rtmp", "RTMP"}, {"rtmps", "RTMPS"}, {"rtmpe", "RTMPE"},
    {"rtmpt", "RTMPT"}, {"rtmpte", "RTMPTE"}, {"rtmpts", "RTMPTS"},
    {"rtsp", "RTSP"}, {"rtsps", "RTSPS"},
    {"srt", "SRT"}, {"rist", "RIST"},
    {"rtp", "RTP"}, {"srtp", "SRTP"},
    {"udp", "UDP MPEG-TS/RTP"}, {"tcp", "TCP MPEG-TS"},
    {"mmsh", "MMSH"}, {"mmst", "MMST"},
};

bool HasControlCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f;
    });
}

}  // namespace

const char* FfmpegProtocolWhitelist() {
    // Deliberately excludes file, concat, concatf, data, subfile, unix and pipe
    // as input protocols. pipe is used only for the fixed stdout destination.
    return "http,https,httpproxy,tcp,tls,crypto,ffrtmpcrypt,ffrtmphttp,"
           "rtmp,rtmps,rtmpe,rtmpt,"
           "rtmpte,rtmpts,rtsp,rtsps,rtp,srtp,udp,srt,rist,mmsh,mmst";
}

bool ParseStreamSourceUrl(
        const std::string& url,
        StreamSourceSpec* spec,
        std::string* error) {
    auto fail = [error](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (spec == nullptr) {
        return fail("stream source output is missing");
    }
    if (url.empty() || url.size() > kMaxUrlBytes || HasControlCharacter(url)) {
        return fail("stream URL is empty, too long, or contains control characters");
    }
    const size_t separator = url.find("://");
    if (separator == std::string::npos || separator == 0 || separator + 3 >= url.size()) {
        return fail("stream URL must contain a supported scheme and authority");
    }
    std::string scheme = url.substr(0, separator);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                           : static_cast<char>(value);
    });
    const auto match = kSchemes.find(scheme);
    if (match == kSchemes.end()) {
        return fail("unsupported or unsafe stream URL scheme");
    }
    if (!std::all_of(scheme.begin(), scheme.end(), [](unsigned char value) {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
        })) {
        return fail("stream URL scheme is malformed");
    }
    const size_t authority_end = url.find_first_of("/?#", separator + 3);
    const std::string_view authority(
            url.data() + separator + 3,
            (authority_end == std::string::npos ? url.size() : authority_end)
                    - separator - 3);
    if (authority.empty() || authority.size() > 1024) {
        return fail("stream URL authority is missing or too long");
    }
    spec->scheme = std::move(scheme);
    spec->label = std::string(match->second);
    spec->rtsp_transport = spec->scheme == "rtsp" || spec->scheme == "rtsps";
    return true;
}

std::vector<std::string> BuildFfmpegArguments(
        const std::string& ffmpeg_path,
        const std::string& url,
        int fps,
        const StreamSourceSpec& spec) {
    std::vector<std::string> arguments = {
        ffmpeg_path,
        "-hide_banner", "-nostdin", "-loglevel", "error",
        "-protocol_whitelist", FfmpegProtocolWhitelist(),
        "-rw_timeout", "15000000",
        "-fflags", "nobuffer",
        "-flags", "low_delay",
        "-probesize", "1048576",
        "-analyzeduration", "2000000",
    };
    if (spec.scheme == "http" || spec.scheme == "https") {
        arguments.insert(arguments.end(), {
            "-user_agent", "VCamES/3.2",
            "-reconnect", "1",
            "-reconnect_at_eof", "1",
            "-reconnect_streamed", "1",
            "-reconnect_on_network_error", "1",
            "-reconnect_on_http_error", "4xx,5xx",
            "-reconnect_delay_max", "10",
        });
    }
    if (spec.rtsp_transport) {
        arguments.insert(arguments.end(), {"-rtsp_transport", "tcp"});
    }
    arguments.insert(arguments.end(), {
        "-thread_queue_size", "8",
        "-i", url,
        "-map", "0:v:0",
        "-an", "-sn", "-dn",
        "-vf", "fps=" + std::to_string(fps)
                + ",scale=1280:720:force_original_aspect_ratio=decrease"
                  ",pad=1280:720:(ow-iw)/2:(oh-ih)/2:black",
        "-threads", "2",
        "-c:v", "mjpeg",
        "-q:v", "3",
        "-f", "image2pipe",
        "pipe:1",
    });
    return arguments;
}

}  // namespace vcames
