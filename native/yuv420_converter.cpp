#include "yuv420_converter.h"

#include <cstring>
#include <limits>

namespace vcames {
namespace {

bool Fail(const char* message, std::string* error) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool CheckedOffset(
        const YuvPlaneView& plane,
        size_t x,
        size_t y,
        size_t samples,
        size_t* offset) {
    if (plane.data == nullptr || plane.row_stride == 0 || plane.pixel_stride == 0
            || plane.position > plane.limit || samples == 0) {
        return false;
    }
    if (y > (std::numeric_limits<size_t>::max() - plane.position)
                    / plane.row_stride) {
        return false;
    }
    const size_t row = plane.position + y * plane.row_stride;
    if (x > (std::numeric_limits<size_t>::max() - row) / plane.pixel_stride) {
        return false;
    }
    const size_t first = row + x * plane.pixel_stride;
    if (samples - 1 > (std::numeric_limits<size_t>::max() - first)
                    / plane.pixel_stride) {
        return false;
    }
    const size_t last = first + (samples - 1) * plane.pixel_stride;
    if (last >= plane.limit) {
        return false;
    }
    *offset = first;
    return true;
}

}  // namespace

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
        std::string* error) {
    if (output == nullptr || width == 0 || height == 0
            || (width & 1U) != 0 || (height & 1U) != 0
            || (crop_left & 1U) != 0 || (crop_top & 1U) != 0) {
        return Fail("YUV crop and dimensions must be non-zero and even", error);
    }
    const size_t pixels = static_cast<size_t>(width) * height;
    if (pixels / width != height || pixels > std::numeric_limits<size_t>::max() / 3
            || output_size != pixels * 3 / 2) {
        return Fail("NV21 output size is invalid", error);
    }

    for (size_t row_index = 0; row_index < height; ++row_index) {
        size_t source = 0;
        if (!CheckedOffset(y, crop_left, crop_top + row_index, width, &source)) {
            return Fail("Y plane stride exceeds its buffer", error);
        }
        uint8_t* destination = output + row_index * width;
        if (y.pixel_stride == 1) {
            std::memcpy(destination, y.data + source, width);
        } else {
            for (size_t column = 0; column < width; ++column) {
                destination[column] = y.data[source + column * y.pixel_stride];
            }
        }
    }

    const size_t chroma_width = width / 2;
    const size_t chroma_height = height / 2;
    const size_t chroma_left = crop_left / 2;
    const size_t chroma_top = crop_top / 2;
    uint8_t* chroma_output = output + pixels;
    for (size_t row_index = 0; row_index < chroma_height; ++row_index) {
        size_t u_source = 0;
        size_t v_source = 0;
        if (!CheckedOffset(u, chroma_left, chroma_top + row_index,
                           chroma_width, &u_source)
                || !CheckedOffset(v, chroma_left, chroma_top + row_index,
                                  chroma_width, &v_source)) {
            return Fail("UV plane stride exceeds its buffer", error);
        }
        uint8_t* destination = chroma_output + row_index * width;

        // ImageReader commonly exposes NV21 as two direct views into the same
        // allocation. In that case the V row already contains VU pairs.
        if (u.pixel_stride == 2 && v.pixel_stride == 2
                && v.data + v_source + 1 == u.data + u_source
                && v_source <= v.limit && width <= v.limit - v_source) {
            std::memcpy(destination, v.data + v_source, width);
            continue;
        }
        for (size_t column = 0; column < chroma_width; ++column) {
            destination[column * 2] = v.data[v_source + column * v.pixel_stride];
            destination[column * 2 + 1] = u.data[u_source + column * u.pixel_stride];
        }
    }
    return true;
}

}  // namespace vcames
