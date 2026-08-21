#include "vcames/stream_source.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void TestSupportedTransports() {
    const std::vector<std::string> schemes = {
        "http", "https", "rtmp", "rtmps", "rtmpe", "rtmpt", "rtmpte",
        "rtmpts", "rtsp", "rtsps", "srt", "rist", "rtp", "srtp",
        "udp", "tcp", "mmsh", "mmst",
    };
    for (const std::string& scheme : schemes) {
        vcames::StreamSourceSpec spec;
        std::string error;
        assert(vcames::ParseStreamSourceUrl(
                scheme + "://user:secret@example.test:9000/live", &spec, &error));
        assert(spec.scheme == scheme);
        assert(!spec.label.empty());
    }
}

void TestUnsafeInputsFailClosed() {
    const std::vector<std::string> inputs = {
        "file:///sdcard/video.mp4", "concat:http://a|http://b",
        "data:text/plain,hello", "pipe://0", "unix:///tmp/source",
        "http://", "not-a-url", "http://example.test/line\nbreak",
    };
    for (const std::string& input : inputs) {
        vcames::StreamSourceSpec spec;
        std::string error;
        assert(!vcames::ParseStreamSourceUrl(input, &spec, &error));
        assert(!error.empty());
    }
}

void TestArgumentsAreLiteralAndBounded() {
    vcames::StreamSourceSpec spec;
    std::string error;
    const std::string url = "rtsp://user:p@ss;$(id)@example.test/live?x=a=b";
    assert(vcames::ParseStreamSourceUrl(url, &spec, &error));
    const std::vector<std::string> args = vcames::BuildFfmpegArguments(
            "/data/adb/modules/vcames_root_bridge/bin/ffmpeg", url, 30, spec);
    assert(args.front().ends_with("/ffmpeg"));
    assert(Contains(args, url));
    assert(Contains(args, "-rtsp_transport"));
    assert(Contains(args, "-protocol_whitelist"));
    const std::string whitelist = vcames::FfmpegProtocolWhitelist();
    for (const char* forbidden : {"file", "concat", "data", "subfile", "unix"}) {
        assert(whitelist.find(forbidden) == std::string::npos);
    }
    assert(args.back() == "pipe:1");

    vcames::StreamSourceSpec http_spec;
    assert(vcames::ParseStreamSourceUrl(
            "https://example.test/live.m3u8", &http_spec, &error));
    const std::vector<std::string> http_args = vcames::BuildFfmpegArguments(
            "/data/adb/modules/vcames_root_bridge/bin/ffmpeg",
            "https://example.test/live.m3u8", 30, http_spec);
    assert(Contains(http_args, "-reconnect_on_network_error"));
    assert(Contains(http_args, "-reconnect_on_http_error"));
    assert(Contains(http_args, "-reconnect_delay_max"));
}

}  // namespace

int main() {
    TestSupportedTransports();
    TestUnsafeInputsFailClosed();
    TestArgumentsAreLiteralAndBounded();
    std::cout << "vcames stream source tests passed\n";
    return 0;
}
