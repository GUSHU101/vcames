#include "vcames/config.h"
#include "vcames/mjpeg_parser.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void TestCommand() {
    vcames::Command command;
    std::string error;
    const std::string request =
            "START\n"
            "url=http://127.0.0.1:8888/live.mjpg?token=a=b\n"
            "video_device=/dev/video100\n"
            "width=1280\nheight=720\nfps=30\nrotation=90\n"
            "mirror=1\n.\n";
    assert(vcames::ParseCommand(request, &command, &error));
    assert(command.type == vcames::CommandType::kStart);
    assert(command.config.width == 1280);
    assert(command.config.height == 720);
    assert(command.config.rotation == 90);
    assert(command.config.mirror);

    assert(!vcames::ParseCommand("START\nwidth=nope\n.\n", &command, &error));
    assert(!vcames::ParseCommand(
            "START\nwidth=641\nheight=480\n.\n", &command, &error));
    assert(!vcames::ParseCommand(
            "START\nwidth=1920\nheight=1080\n.\n", &command, &error));
    assert(!vcames::ParseCommand(
            "START\ntarget=front\n.\n", &command, &error));
    assert(!vcames::ParseCommand(
            "START\npackage_name=com.example.camera\n.\n", &command, &error));
    assert(vcames::ParseCommand("START\nurl=rtmp://example.test/live/key\n.\n",
                                &command, &error));
    assert(vcames::ParseCommand("START\nurl=srt://example.test:9000\n.\n",
                                &command, &error));
    assert(!vcames::ParseCommand("START\nurl=file:///sdcard/video.mp4\n.\n",
                                 &command, &error));
    assert(!vcames::ParseCommand("UNKNOWN\n.\n", &command, &error));
    assert(vcames::ParseCommand("STATUS\n.\n", &command, &error));
}

void TestMjpegParser() {
    std::vector<std::vector<uint8_t>> frames;
    vcames::MjpegParser parser([&frames](std::vector<uint8_t>&& frame) {
        frames.emplace_back(std::move(frame));
    });
    const std::vector<uint8_t> input = {
        'x', 'x', 0xff, 0xd8, 1, 2, 3, 0xff, 0xd9,
        '-', '-', 0xff, 0xd8, 4, 5, 0xff, 0xd9, 'z',
    };
    std::string error;
    assert(parser.Append(nullptr, 0, &error));
    assert(parser.Append(input.data(), 4, &error));
    assert(parser.Append(input.data() + 4, input.size() - 4, &error));
    assert(frames.size() == 2);
    assert(frames[0].front() == 0xff && frames[0].back() == 0xd9);
    assert(frames[1].size() == 6);
}

void TestFrameLimit() {
    vcames::MjpegParser parser([](std::vector<uint8_t>&&) {}, 5);
    const std::vector<uint8_t> input = {0xff, 0xd8, 1, 2, 3, 4};
    std::string error;
    assert(!parser.Append(input.data(), input.size(), &error));
    assert(!error.empty());
}

}  // namespace

int main() {
    TestCommand();
    TestMjpegParser();
    TestFrameLimit();
    std::cout << "vcames parser tests passed\n";
    return 0;
}
