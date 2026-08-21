#include "yuv420_converter.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::string error;
    std::vector<uint8_t> y = {
        0, 1, 2, 3, 4, 0,
        0, 5, 6, 7, 8, 0,
        0, 9, 10, 11, 12, 0,
        0, 13, 14, 15, 16, 0,
    };
    std::vector<uint8_t> u = {0, 20, 21, 0, 0, 22, 23, 0};
    std::vector<uint8_t> v = {0, 30, 31, 0, 0, 32, 33, 0};
    vcames::YuvPlaneView y_plane{y.data(), y.size(), 1, 6, 1};
    vcames::YuvPlaneView u_plane{u.data(), u.size(), 1, 4, 1};
    vcames::YuvPlaneView v_plane{v.data(), v.size(), 1, 4, 1};
    std::vector<uint8_t> output(24);
    const std::vector<uint8_t> expected = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16,
        30, 20, 31, 21, 32, 22, 33, 23,
    };
    if (!vcames::ConvertYuv420ToNv21(
                y_plane, u_plane, v_plane, 0, 0,
                4, 4, output.data(), output.size(), &error)
            || output != expected) {
        std::cerr << "planar conversion failed: " << error << '\n';
        return 1;
    }

    output.assign(6, 0);
    if (!vcames::ConvertYuv420ToNv21(
                y_plane, u_plane, v_plane, 2, 2,
                2, 2, output.data(), output.size(), &error)
            || output != std::vector<uint8_t>({11, 12, 15, 16, 33, 23})) {
        std::cerr << "cropped conversion failed: " << error << '\n';
        return 1;
    }

    std::vector<uint8_t> interleaved = {40, 50, 41, 51};
    vcames::YuvPlaneView v_interleaved{
        interleaved.data(), interleaved.size(), 0, 4, 2};
    vcames::YuvPlaneView u_interleaved{
        interleaved.data() + 1, interleaved.size() - 1, 0, 4, 2};
    std::vector<uint8_t> small_y = {1, 2, 3, 4, 5, 6, 7, 8};
    vcames::YuvPlaneView small_y_plane{
        small_y.data(), small_y.size(), 0, 4, 1};
    output.assign(12, 0);
    if (!vcames::ConvertYuv420ToNv21(
                small_y_plane, u_interleaved, v_interleaved,
                0, 0, 4, 2, output.data(), output.size(), &error)
            || output != std::vector<uint8_t>({1, 2, 3, 4, 5, 6, 7, 8,
                                               40, 50, 41, 51})) {
        std::cerr << "interleaved conversion failed: " << error << '\n';
        return 1;
    }
    u_interleaved.limit = 2;
    if (vcames::ConvertYuv420ToNv21(
                small_y_plane, u_interleaved, v_interleaved,
                0, 0, 4, 2, output.data(), output.size(), &error)) {
        std::cerr << "converter accepted a truncated U plane\n";
        return 1;
    }
    std::cout << "YUV_420_888 native conversion tests passed\n";
    return 0;
}
