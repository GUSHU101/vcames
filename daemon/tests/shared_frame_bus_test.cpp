#include "vcames/shared_frame_bus.h"

#include <cstdint>
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
    std::vector<uint8_t> actual;
    uint64_t sequence = 0;
    if (!bus.CopyLatest(&actual, &sequence, &error) || actual != expected || sequence != 1) {
        std::cerr << "first frame mismatch: " << error << '\n';
        return 1;
    }
    expected = {0xff, 0xd8, 9, 8, 7, 0xff, 0xd9};
    if (!bus.PublishJpeg(expected, 1280, 720, 789, 999, &error)
            || !bus.CopyLatest(&actual, &sequence, &error)
            || actual != expected || sequence != 2) {
        std::cerr << "latest frame mismatch: " << error << '\n';
        return 1;
    }
    bus.Invalidate();
    if (bus.CopyLatest(&actual, &sequence, &error)) {
        std::cerr << "invalidated bus still returned a frame\n";
        return 1;
    }
    if (!bus.PublishJpeg(expected, 1280, 720, 1000, 1100, &error)
            || !bus.CopyLatest(&actual, &sequence, &error) || sequence != 3) {
        std::cerr << "sequence did not remain monotonic after invalidation\n";
        return 1;
    }
    int duplicate = bus.DuplicateFd(&error);
    if (duplicate < 0) {
        std::cerr << error << '\n';
        return 1;
    }
    close(duplicate);
    std::cout << "shared frame bus tests passed\n";
    return 0;
}
