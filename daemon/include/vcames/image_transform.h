#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vcames {

struct TransformOptions {
    int width = 1280;
    int height = 720;
    int rotation = 0;
    bool mirror = false;
    int jpeg_quality = 90;
};

bool CreateSolidJpeg(
        int width,
        int height,
        uint8_t red,
        uint8_t green,
        uint8_t blue,
        int quality,
        std::vector<uint8_t>* output,
        std::string* error);

bool ReadJpegDimensions(
        const std::vector<uint8_t>& jpeg,
        int* width,
        int* height,
        std::string* error);

bool TransformJpeg(
        const std::vector<uint8_t>& input,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        int* source_width,
        int* source_height,
        std::string* error);

// Decode an MJPEG source directly into the raw replacement transport. This
// avoids a JPEG re-encode when the target is an OEM front/back camera.
bool TransformJpegToNv21(
        const std::vector<uint8_t>& input,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        int* source_width,
        int* source_height,
        std::string* error);

bool TransformNv21(
        const std::vector<uint8_t>& input,
        int source_width,
        int source_height,
        const TransformOptions& options,
        std::vector<uint8_t>* output,
        std::string* error);

bool Nv21ToJpeg(
        const std::vector<uint8_t>& input,
        int width,
        int height,
        int quality,
        std::vector<uint8_t>* output,
        std::string* error);

}  // namespace vcames
