#include "vcames/shared_frame_bus.h"

#include <cstdint>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

int main() {
    vcames::SharedFrameBus bus;
    std::string error;
    if (!bus.Open(1024, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::vector<uint8_t> expected{0xff, 0xd8, 1, 2, 3, 0xff, 0xd9};
    if (!bus.PublishJpeg(expected, 640, 480, 123, 456, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    vcames::SharedFrameBus::Frame actual;
    uint64_t sequence = 0;
    if (!bus.CopyLatest(&actual, &sequence, &error)
            || actual.payload != expected || sequence != 1
            || actual.format != vcames::SharedFrameBus::PixelFormat::kJpeg) {
        std::cerr << "first frame mismatch: " << error << '\n';
        return 1;
    }
    expected = {0xff, 0xd8, 9, 8, 7, 0xff, 0xd9};
    if (!bus.PublishJpeg(expected, 1280, 720, 789, 999, &error)
            || !bus.CopyLatest(&actual, &sequence, &error)
            || actual.payload != expected || sequence != 2) {
        std::cerr << "latest frame mismatch: " << error << '\n';
        return 1;
    }
    bus.Invalidate();
    if (bus.CopyLatest(&actual, &sequence, &error)) {
        std::cerr << "invalidated bus still returned a frame\n";
        return 1;
    }
    vcames::SharedFrameBus::Frame raw;
    raw.width = 4;
    raw.height = 2;
    raw.y_stride = 4;
    raw.uv_stride = 4;
    raw.format = vcames::SharedFrameBus::PixelFormat::kNv21;
    raw.presentation_time_ns = 1000;
    raw.arrival_time_ns = 1100;
    raw.payload = {16, 16, 16, 16, 16, 16, 16, 16, 128, 128, 128, 128};
    if (!bus.Publish(raw, &error)
            || !bus.CopyLatest(&actual, &sequence, &error) || sequence != 3
            || actual.payload != raw.payload
            || actual.format != vcames::SharedFrameBus::PixelFormat::kNv21
            || actual.y_stride != 4 || actual.uv_stride != 4) {
        std::cerr << "sequence did not remain monotonic after invalidation\n";
        return 1;
    }
    raw.payload.pop_back();
    if (bus.Publish(raw, &error)) {
        std::cerr << "FrameBus accepted a truncated NV21 payload\n";
        return 1;
    }
    int duplicate = bus.DuplicateFd(&error);
    if (duplicate < 0) {
        std::cerr << error << '\n';
        return 1;
    }
    if ((fcntl(duplicate, F_GETFL) & O_ACCMODE) != O_RDONLY
            || write(duplicate, "x", 1) >= 0 || errno != EBADF) {
        std::cerr << "FrameBus consumer fd is writable\n";
        close(duplicate);
        return 1;
    }
    vcames::SharedFrameBus::Frame from_fd;
    uint64_t from_fd_sequence = 0;
    vcames::SharedFrameBusReader reader;
    if (!vcames::SharedFrameBus::ValidateConsumerFd(duplicate, &error)
            || !reader.Attach(duplicate, &error)
            || !reader.CopyLatest(&from_fd, &from_fd_sequence, &error)
            || from_fd.payload != actual.payload || from_fd_sequence != sequence) {
        std::cerr << "read-only FrameBus consumer validation failed: " << error << '\n';
        close(duplicate);
        return 1;
    }
    close(duplicate);
    std::cout << "shared frame bus tests passed\n";
    return 0;
}
