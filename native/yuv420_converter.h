#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vcames {

struct YuvPlaneView {
    const uint8_t* data = nullptr;
    size_t limit = 0;
    size_t position = 0;
    size_t row_stride = 0;
    size_t pixel_stride = 0;
};

// Converts a cropped Android YUV_420_888 image to tightly packed NV21. The
// caller owns all buffers. Crop coordinates and dimensions must be even.
bool ConvertYuv420ToNv21(
        const YuvPlaneView& y,
        const YuvPlaneView& u,
        const YuvPlaneView& v,
        uint32_t crop_left,
        uint32_t crop_top,
        uint32_t width,
        uint32_t height,
        uint8_t* output,
        size_t output_size,
        std::string* error);

}  // namespace vcames
