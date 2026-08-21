#include "vcames/image_transform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vcames {
namespace {

bool IsStartOfFrame(uint8_t marker) {
    switch (marker) {
        case 0xc0:
        case 0xc1:
        case 0xc2:
        case 0xc3:
        case 0xc5:
        case 0xc6:
        case 0xc7:
        case 0xc9:
        case 0xca:
        case 0xcb:
        case 0xcd:
        case 0xce:
        case 0xcf:
            return true;
        default:
            return false;
    }
}

uint16_t BigEndian16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void WriteToVector(void* context, void* data, int size) {
    if (context == nullptr || data == nullptr || size <= 0) {
        return;
    }
    auto* output = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

struct SourceCoordinate {
    float x;
    float y;
};

SourceCoordinate InvertOrientation(
        float oriented_x,
        float oriented_y,
        int source_width,
        int source_height,
        int rotation) {
    switch (rotation) {
        case 90:
            return {oriented_y, static_cast<float>(source_height - 1) - oriented_x};
        case 180:
            return {
                    static_cast<float>(source_width - 1) - oriented_x,
                    static_cast<float>(source_height - 1) - oriented_y};
        case 270:
            return {static_cast<float>(source_width - 1) - oriented_y, oriented_x};
        default:
            return {oriented_x, oriented_y};
    }
}

uint8_t SampleChannel(
        const uint8_t* image,
        int width,
        int height,
        float x,
        float y,
        int channel) {
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const auto at = [image, width, channel](int sample_x, int sample_y) {
        return static_cast<float>(image[(sample_y * width + sample_x) * 3 + channel]);
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return static_cast<uint8_t>(std::clamp(top + (bottom - top) * fy, 0.0f, 255.0f));
}

uint8_t ClampByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

SourceCoordinate MapOutputToSource(
        int output_x,
        int output_y,
        int output_width,
        int output_height,
        int source_width,
        int source_height,
        int rotation,
        bool mirror) {
    const int oriented_width = (rotation == 90 || rotation == 270)
            ? source_height
            : source_width;
    const int oriented_height = (rotation == 90 || rotation == 270)
            ? source_width
            : source_height;
    const float scale = std::max(
            static_cast<float>(output_width) / static_cast<float>(oriented_width),
            static_cast<float>(output_height) / static_cast<float>(oriented_height));
    float oriented_x = (static_cast<float>(output_x) + 0.5f
            - static_cast<float>(output_width) * 0.5f) / scale
            + static_cast<float>(oriented_width) * 0.5f - 0.5f;
    const float oriented_y = (static_cast<float>(output_y) + 0.5f
            - static_cast<float>(output_height) * 0.5f) / scale
            + static_cast<float>(oriented_height) * 0.5f - 0.5f;
    if (mirror) {
        oriented_x = static_cast<float>(oriented_width - 1) - oriented_x;
    }
    return InvertOrientation(
            oriented_x, oriented_y, source_width, source_height, rotation);
}

}  // namespace

bool CreateSolidJpeg(
        int width,
        int height,
        uint8_t red,
        uint8_t green,
        uint8_t blue,
        int quality,
        std::vector<uint8_t>* output,
        std::string* error) {
    if (output == nullptr || width <= 0 || height <= 0
            || quality < 1 || quality > 100) {
        if (error != nullptr) {
            *error = "invalid solid JPEG parameters";
        }
        return false;
    }
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixel_count > 3840u * 2160u) {
        if (error != nullptr) {
            *error = "solid JPEG dimensions exceed the safety limit";
        }
        return false;
    }
    std::vector<uint8_t> pixels(pixel_count * 3);
    for (size_t offset = 0; offset < pixels.size(); offset += 3) {
        pixels[offset] = red;
        pixels[offset + 1] = green;
        pixels[offset + 2] = blue;
    }
    output->clear();
    if (stbi_write_jpg_to_func(
                WriteToVector,
                output,
                width,
                height,
                3,
                pixels.data(),
                quality) == 0) {
        output->clear();
        if (error != nullptr) {
            *error = "solid JPEG encode failed";
        }
        return false;
    }
    return true;
}

bool ReadJpegDimensions(
        const std::vector<uint8_t>& jpeg,
        int* width,
        int* height,
        std::string* error) {
    if (jpeg.size() < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        if (error != nullptr) {
            *error = "input is not a JPEG frame";
        }
        return false;
    }
    size_t offset = 2;
    while (offset + 4 <= jpeg.size()) {
        while (offset < jpeg.size() && jpeg[offset] == 0xff) {
            ++offset;
        }
        if (offset >= jpeg.size()) {
            break;
        }
        const uint8_t marker = jpeg[offset++];
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            continue;
        }
        if (marker == 0xd9 || marker == 0xda || offset + 2 > jpeg.size()) {
            break;
        }
        const uint16_t segment_length = BigEndian16(jpeg.data() + offset);
        if (segment_length < 2 || offset + segment_length > jpeg.size()) {
            break;
        }
        if (IsStartOfFrame(marker) && segment_length >= 7) {
            const int parsed_height = BigEndian16(jpeg.data() + offset + 3);
            const int parsed_width = BigEndian16(jpeg.data() + offset + 5);
            if (parsed_width <= 0 || parsed_height <= 0) {
                break;
            }
            if (width != nullptr) {
                *width = parsed_width;
            }
            if (height != nullptr) {
                *height = parsed_height;
            }
            return true;
        }
        offset += segment_length;
    }
    if (error != nullptr) {
        *error = "JPEG frame has no valid dimensions";
    }
    return false;
}

bool TransformJpeg(
        const std::vector<uint8_t>& input,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        int* source_width,
        int* source_height,
        std::string* error) {
    if (output == nullptr) {
        if (error != nullptr) {
            *error = "missing JPEG output buffer";
        }
        return false;
    }
    if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (error != nullptr) {
            *error = "JPEG frame is too large to decode";
        }
        return false;
    }
    int declared_width = 0;
    int declared_height = 0;
    if (!ReadJpegDimensions(
                input, &declared_width, &declared_height, error)
            || declared_width > 3840 || declared_height > 2160
            || static_cast<size_t>(declared_width)
                    * static_cast<size_t>(declared_height) > 3840u * 2160u) {
        if (error != nullptr && declared_width > 0 && declared_height > 0) {
            *error = "JPEG dimensions exceed the decode safety limit";
        }
        return false;
    }
    int decoded_width = 0;
    int decoded_height = 0;
    int components = 0;
    stbi_uc* decoded = stbi_load_from_memory(
            input.data(),
            static_cast<int>(input.size()),
            &decoded_width,
            &decoded_height,
            &components,
            3);
    if (decoded == nullptr || decoded_width <= 0 || decoded_height <= 0) {
        if (error != nullptr) {
            const char* reason = stbi_failure_reason();
            *error = std::string("JPEG decode failed") + (reason == nullptr ? "" : ": ")
                    + (reason == nullptr ? "" : reason);
        }
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    if (source_width != nullptr) {
        *source_width = decoded_width;
    }
    if (source_height != nullptr) {
        *source_height = decoded_height;
    }

    if (options.rotation == 0 && !options.mirror
            && options.width == decoded_width && options.height == decoded_height) {
        *output = input;
        stbi_image_free(decoded);
        return true;
    }

    const int oriented_width = (options.rotation == 90 || options.rotation == 270)
            ? decoded_height
            : decoded_width;
    const int oriented_height = (options.rotation == 90 || options.rotation == 270)
            ? decoded_width
            : decoded_height;
    const float scale = std::max(
            static_cast<float>(options.width) / static_cast<float>(oriented_width),
            static_cast<float>(options.height) / static_cast<float>(oriented_height));
    if (!std::isfinite(scale) || scale <= 0.0f) {
        stbi_image_free(decoded);
        if (error != nullptr) {
            *error = "invalid image scale";
        }
        return false;
    }

    const size_t output_pixels = static_cast<size_t>(options.width)
            * static_cast<size_t>(options.height);
    std::vector<uint8_t> transformed(output_pixels * 3);
    for (int y = 0; y < options.height; ++y) {
        for (int x = 0; x < options.width; ++x) {
            float oriented_x = (static_cast<float>(x) + 0.5f
                    - static_cast<float>(options.width) * 0.5f) / scale
                    + static_cast<float>(oriented_width) * 0.5f - 0.5f;
            const float oriented_y = (static_cast<float>(y) + 0.5f
                    - static_cast<float>(options.height) * 0.5f) / scale
                    + static_cast<float>(oriented_height) * 0.5f - 0.5f;
            if (options.mirror) {
                oriented_x = static_cast<float>(oriented_width - 1) - oriented_x;
            }
            const SourceCoordinate source = InvertOrientation(
                    oriented_x,
                    oriented_y,
                    decoded_width,
                    decoded_height,
                    options.rotation);
            const size_t output_offset = (static_cast<size_t>(y) * options.width + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                transformed[output_offset + channel] = SampleChannel(
                        decoded,
                        decoded_width,
                        decoded_height,
                        source.x,
                        source.y,
                        channel);
            }
        }
    }
    stbi_image_free(decoded);

    output->clear();
    output->reserve(std::max<size_t>(64 * 1024, output_pixels / 2));
    if (stbi_write_jpg_to_func(
                WriteToVector,
                output,
                options.width,
                options.height,
                3,
                transformed.data(),
                options.jpeg_quality) == 0) {
        output->clear();
        if (error != nullptr) {
            *error = "JPEG encode failed";
        }
        return false;
    }
    return true;
}

bool TransformJpegToNv21(
        const std::vector<uint8_t>& input,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        int* source_width,
        int* source_height,
        std::string* error) {
    if (output == nullptr || options.width <= 0 || options.height <= 0
            || (options.width & 1) != 0 || (options.height & 1) != 0
            || (options.rotation != 0 && options.rotation != 90
                && options.rotation != 180 && options.rotation != 270)) {
        if (error != nullptr) {
            *error = "invalid JPEG to NV21 transform parameters";
        }
        return false;
    }
    if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (error != nullptr) {
            *error = "JPEG frame is too large to decode";
        }
        return false;
    }
    int declared_width = 0;
    int declared_height = 0;
    if (!ReadJpegDimensions(
                input, &declared_width, &declared_height, error)
            || declared_width > 3840 || declared_height > 2160
            || static_cast<size_t>(declared_width)
                    * static_cast<size_t>(declared_height) > 3840u * 2160u) {
        if (error != nullptr && declared_width > 0 && declared_height > 0) {
            *error = "JPEG dimensions exceed the decode safety limit";
        }
        return false;
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int components = 0;
    stbi_uc* decoded = stbi_load_from_memory(
            input.data(), static_cast<int>(input.size()),
            &decoded_width, &decoded_height, &components, 3);
    if (decoded == nullptr || decoded_width != declared_width
            || decoded_height != declared_height) {
        if (error != nullptr) {
            const char* reason = stbi_failure_reason();
            *error = std::string("JPEG decode failed") + (reason == nullptr ? "" : ": ")
                    + (reason == nullptr ? "" : reason);
        }
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    if (source_width != nullptr) {
        *source_width = decoded_width;
    }
    if (source_height != nullptr) {
        *source_height = decoded_height;
    }

    const size_t output_pixels = static_cast<size_t>(options.width)
            * static_cast<size_t>(options.height);
    output->assign(output_pixels * 3 / 2, 0);
    for (int y = 0; y < options.height; ++y) {
        for (int x = 0; x < options.width; ++x) {
            const SourceCoordinate source = MapOutputToSource(
                    x, y, options.width, options.height,
                    decoded_width, decoded_height, options.rotation, options.mirror);
            const int red = SampleChannel(
                    decoded, decoded_width, decoded_height, source.x, source.y, 0);
            const int green = SampleChannel(
                    decoded, decoded_width, decoded_height, source.x, source.y, 1);
            const int blue = SampleChannel(
                    decoded, decoded_width, decoded_height, source.x, source.y, 2);
            (*output)[static_cast<size_t>(y) * options.width + x] = ClampByte(
                    ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < options.height; y += 2) {
        for (int x = 0; x < options.width; x += 2) {
            int u_total = 0;
            int v_total = 0;
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 2; ++column) {
                    const SourceCoordinate source = MapOutputToSource(
                            x + column, y + row, options.width, options.height,
                            decoded_width, decoded_height,
                            options.rotation, options.mirror);
                    const int red = SampleChannel(
                            decoded, decoded_width, decoded_height,
                            source.x, source.y, 0);
                    const int green = SampleChannel(
                            decoded, decoded_width, decoded_height,
                            source.x, source.y, 1);
                    const int blue = SampleChannel(
                            decoded, decoded_width, decoded_height,
                            source.x, source.y, 2);
                    u_total += ((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128;
                    v_total += ((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128;
                }
            }
            const size_t chroma = output_pixels
                    + static_cast<size_t>(y / 2) * options.width + x;
            (*output)[chroma] = ClampByte(v_total / 4);
            (*output)[chroma + 1] = ClampByte(u_total / 4);
        }
    }
    stbi_image_free(decoded);
    return true;
}

bool TransformNv21(
        const std::vector<uint8_t>& input,
        int source_width,
        int source_height,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        std::string* error) {
    if (output == nullptr || source_width <= 0 || source_height <= 0
            || options.width <= 0 || options.height <= 0
            || (source_width & 1) != 0 || (source_height & 1) != 0
            || (options.width & 1) != 0 || (options.height & 1) != 0) {
        if (error != nullptr) {
            *error = "NV21 dimensions must be positive and even";
        }
        return false;
    }
    const size_t source_pixels = static_cast<size_t>(source_width)
            * static_cast<size_t>(source_height);
    if (source_pixels > 3840u * 2160u || input.size() != source_pixels * 3 / 2) {
        if (error != nullptr) {
            *error = "NV21 payload size does not match its dimensions";
        }
        return false;
    }
    if (options.rotation != 0 && options.rotation != 90
            && options.rotation != 180 && options.rotation != 270) {
        if (error != nullptr) {
            *error = "NV21 rotation is invalid";
        }
        return false;
    }
    if (options.rotation == 0 && !options.mirror
            && options.width == source_width && options.height == source_height) {
        *output = input;
        return true;
    }

    const size_t output_pixels = static_cast<size_t>(options.width)
            * static_cast<size_t>(options.height);
    output->assign(output_pixels * 3 / 2, 0);
    for (int y = 0; y < options.height; ++y) {
        for (int x = 0; x < options.width; ++x) {
            SourceCoordinate source = MapOutputToSource(
                    x, y, options.width, options.height,
                    source_width, source_height, options.rotation, options.mirror);
            const int source_x = std::clamp(
                    static_cast<int>(std::lround(source.x)), 0, source_width - 1);
            const int source_y = std::clamp(
                    static_cast<int>(std::lround(source.y)), 0, source_height - 1);
            (*output)[static_cast<size_t>(y) * options.width + x] =
                    input[static_cast<size_t>(source_y) * source_width + source_x];
        }
    }

    const size_t output_chroma = output_pixels;
    const size_t source_chroma = source_pixels;
    for (int y = 0; y < options.height / 2; ++y) {
        for (int x = 0; x < options.width / 2; ++x) {
            SourceCoordinate source = MapOutputToSource(
                    x * 2, y * 2, options.width, options.height,
                    source_width, source_height, options.rotation, options.mirror);
            int source_x = std::clamp(
                    static_cast<int>(std::lround(source.x)), 0, source_width - 2);
            int source_y = std::clamp(
                    static_cast<int>(std::lround(source.y)), 0, source_height - 2);
            source_x &= ~1;
            source_y &= ~1;
            const size_t source_offset = source_chroma
                    + static_cast<size_t>(source_y / 2) * source_width + source_x;
            const size_t output_offset = output_chroma
                    + static_cast<size_t>(y) * options.width + x * 2;
            (*output)[output_offset] = input[source_offset];
            (*output)[output_offset + 1] = input[source_offset + 1];
        }
    }
    return true;
}

bool Nv21ToJpeg(
        const std::vector<uint8_t>& input,
        int width,
        int height,
        int quality,
        std::vector<uint8_t>* output,
        std::string* error) {
    if (output == nullptr || width <= 0 || height <= 0
            || (width & 1) != 0 || (height & 1) != 0
            || quality < 1 || quality > 100) {
        if (error != nullptr) {
            *error = "invalid NV21 JPEG parameters";
        }
        return false;
    }
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels > 3840u * 2160u || input.size() != pixels * 3 / 2) {
        if (error != nullptr) {
            *error = "NV21 JPEG payload size mismatch";
        }
        return false;
    }
    std::vector<uint8_t> rgb(pixels * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int luma = input[static_cast<size_t>(y) * width + x];
            const size_t chroma = pixels + static_cast<size_t>(y / 2) * width + (x & ~1);
            const int v = static_cast<int>(input[chroma]) - 128;
            const int u = static_cast<int>(input[chroma + 1]) - 128;
            const int c = std::max(0, luma - 16);
            const size_t offset = (static_cast<size_t>(y) * width + x) * 3;
            rgb[offset] = ClampByte((298 * c + 409 * v + 128) >> 8);
            rgb[offset + 1] = ClampByte((298 * c - 100 * u - 208 * v + 128) >> 8);
            rgb[offset + 2] = ClampByte((298 * c + 516 * u + 128) >> 8);
        }
    }
    output->clear();
    output->reserve(std::max<size_t>(64 * 1024, pixels / 2));
    if (stbi_write_jpg_to_func(
                WriteToVector, output, width, height, 3, rgb.data(), quality) == 0) {
        output->clear();
        if (error != nullptr) {
            *error = "NV21 JPEG encode failed";
        }
        return false;
    }
    return true;
}

}  // namespace vcames
