#include "vcames/v4l2_sink.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <limits>

namespace vcames {
namespace {

int Ioctl(int fd, unsigned long request, void* argument) {
    int result;
    do {
        result = ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

}  // namespace

V4l2Sink::~V4l2Sink() {
    Close();
}

bool V4l2Sink::Open(
        const std::string& device,
        int width,
        int height,
        int fps,
        size_t max_frame_bytes,
        std::string* error) {
    Close();
    fd_ = open(device.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "cannot open " + device + ": " + std::strerror(errno);
        }
        return false;
    }

    v4l2_capability capability{};
    if (Ioctl(fd_, VIDIOC_QUERYCAP, &capability) != 0) {
        if (error != nullptr) {
            *error = "VIDIOC_QUERYCAP failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }
    const uint32_t capabilities = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
            ? capability.device_caps
            : capability.capabilities;
    if ((capabilities & V4L2_CAP_VIDEO_OUTPUT) == 0
            || (capabilities & V4L2_CAP_READWRITE) == 0) {
        if (error != nullptr) {
            *error = device + " is not a writable V4L2 video-output device";
        }
        Close();
        return false;
    }

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = static_cast<uint32_t>(width);
    format.fmt.pix.height = static_cast<uint32_t>(height);
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.bytesperline = 0;
    format.fmt.pix.sizeimage = static_cast<uint32_t>(std::min<size_t>(
            max_frame_bytes,
            std::numeric_limits<uint32_t>::max()));
    if (Ioctl(fd_, VIDIOC_S_FMT, &format) != 0) {
        if (error != nullptr) {
            *error = "VIDIOC_S_FMT(MJPEG) failed: " + std::string(std::strerror(errno));
        }
        Close();
        return false;
    }
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG
            || static_cast<int>(format.fmt.pix.width) != width
            || static_cast<int>(format.fmt.pix.height) != height) {
        if (error != nullptr) {
            *error = "V4L2 driver rejected the requested MJPEG geometry";
        }
        Close();
        return false;
    }

    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    parameters.parm.output.timeperframe.numerator = 1;
    parameters.parm.output.timeperframe.denominator = static_cast<uint32_t>(fps);
    Ioctl(fd_, VIDIOC_S_PARM, &parameters);
    return true;
}

bool V4l2Sink::Write(const std::vector<uint8_t>& jpeg, std::string* error) {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "V4L2 sink is not open";
        }
        return false;
    }
    size_t written = 0;
    while (written < jpeg.size()) {
        const ssize_t count = write(fd_, jpeg.data() + written, jpeg.size() - written);
        if (count > 0) {
            written += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{fd_, POLLOUT, 0};
            int ready;
            do {
                ready = poll(&descriptor, 1, 1000);
            } while (ready < 0 && errno == EINTR);
            if (ready > 0) {
                continue;
            }
        }
        if (error != nullptr) {
            *error = "V4L2 frame write failed: " + std::string(std::strerror(errno));
        }
        return false;
    }
    return true;
}

void V4l2Sink::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

}  // namespace vcames
