#include "vcames/mjpeg_parser.h"

#include <algorithm>
#include <array>
#include <utility>

namespace vcames {

MjpegParser::MjpegParser(FrameCallback callback, size_t max_frame_bytes)
    : callback_(std::move(callback)), max_frame_bytes_(max_frame_bytes) {
    buffer_.reserve(256 * 1024);
}

bool MjpegParser::Append(const uint8_t* data, size_t size, std::string* error) {
    if (data == nullptr && size != 0) {
        if (error != nullptr) {
            *error = "MJPEG input pointer is null";
        }
        return false;
    }
    if (size == 0) {
        return true;
    }
    buffer_.insert(buffer_.end(), data, data + size);

    for (;;) {
        static constexpr std::array<uint8_t, 2> kSoiMarker{0xff, 0xd8};
        const auto soi = std::search(
                buffer_.begin(), buffer_.end(),
                kSoiMarker.begin(), kSoiMarker.end());
        if (soi == buffer_.end()) {
            const bool trailing_ff = !buffer_.empty() && buffer_.back() == 0xff;
            buffer_.clear();
            if (trailing_ff) {
                buffer_.push_back(0xff);
            }
            return true;
        }
        if (soi != buffer_.begin()) {
            buffer_.erase(buffer_.begin(), soi);
        }

        const std::array<uint8_t, 2> eoi_marker{0xff, 0xd9};
        const auto eoi = std::search(
                buffer_.begin() + 2, buffer_.end(), eoi_marker.begin(), eoi_marker.end());
        if (eoi == buffer_.end()) {
            if (buffer_.size() > max_frame_bytes_) {
                buffer_.clear();
                if (error != nullptr) {
                    *error = "MJPEG frame exceeds configured safety limit";
                }
                return false;
            }
            return true;
        }

        const size_t frame_size = static_cast<size_t>(eoi - buffer_.begin()) + 2;
        std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + frame_size);
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
        callback_(std::move(frame));
    }
}

void MjpegParser::Reset() {
    buffer_.clear();
}

}  // namespace vcames
