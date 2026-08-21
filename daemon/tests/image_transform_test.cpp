#include "vcames/image_transform.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    const std::vector<uint8_t> input{
        1, 2, 3, 4,
        5, 6, 7, 8,
        10, 20, 30, 40,
    };
    vcames::TransformOptions options;
    options.width = 4;
    options.height = 2;
    std::vector<uint8_t> output;
    std::string error;
    if (!vcames::TransformNv21(input, 4, 2, options, &output, &error)
            || output != input) {
        std::cerr << "NV21 identity failed: " << error << '\n';
        return 1;
    }

    options.mirror = true;
    const std::vector<uint8_t> mirrored{
        4, 3, 2, 1,
        8, 7, 6, 5,
        30, 40, 10, 20,
    };
    if (!vcames::TransformNv21(input, 4, 2, options, &output, &error)
            || output != mirrored) {
        std::cerr << "NV21 mirror failed: " << error << '\n';
        return 1;
    }

    std::vector<uint8_t> jpeg;
    if (!vcames::Nv21ToJpeg(input, 4, 2, 85, &jpeg, &error)
            || jpeg.size() < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        std::cerr << "NV21 JPEG conversion failed: " << error << '\n';
        return 1;
    }
    options.mirror = false;
    std::vector<uint8_t> decoded_nv21;
    int source_width = 0;
    int source_height = 0;
    if (!vcames::TransformJpegToNv21(
                jpeg,
                options,
                &decoded_nv21,
                &source_width,
                &source_height,
                &error)
            || decoded_nv21.size() != input.size()
            || source_width != 4 || source_height != 2) {
        std::cerr << "JPEG to NV21 conversion failed: " << error << '\n';
        return 1;
    }
    std::cout << "image transform tests passed\n";
    return 0;
}
