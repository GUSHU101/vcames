#include "vcames/shared_frame_bus.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <limits>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <linux/memfd.h>
#endif

namespace vcames {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'F', 'B', 'U', 'S', '2', '\0'};
constexpr size_t kHeaderAlignment = 4096;
constexpr uint32_t kMaximumDimension = 8192;

size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

uint64_t LoadAcquire(const uint64_t* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

void StoreRelease(uint64_t* value, uint64_t next) {
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

int CreateMemfd() {
#if defined(__linux__) && defined(SYS_memfd_create)
    return static_cast<int>(syscall(
            SYS_memfd_create,
            "vcames-frame-bus-v2",
            MFD_CLOEXEC | MFD_ALLOW_SEALING));
#else
    errno = ENOSYS;
    return -1;
#endif
}

bool IsSupportedFormat(SharedFrameBus::PixelFormat format) {
    switch (format) {
        case SharedFrameBus::PixelFormat::kJpeg:
        case SharedFrameBus::PixelFormat::kNv21:
        case SharedFrameBus::PixelFormat::kNv12:
        case SharedFrameBus::PixelFormat::kI420:
        case SharedFrameBus::PixelFormat::kRgba8888:
            return true;
        default:
            return false;
    }
}

bool ValidateFrame(const SharedFrameBus::Frame& frame, size_t capacity, std::string* error) {
    if (frame.payload.empty() || frame.payload.size() > capacity) {
        if (error != nullptr) {
            *error = "frame payload is empty or exceeds shared slot capacity";
        }
        return false;
    }
    if (frame.width == 0 || frame.height == 0
            || frame.width > kMaximumDimension || frame.height > kMaximumDimension) {
        if (error != nullptr) {
            *error = "frame dimensions are invalid";
        }
        return false;
    }
    if (!IsSupportedFormat(frame.format)) {
        if (error != nullptr) {
            *error = "frame format is not supported by FrameBus v2";
        }
        return false;
    }
    if (frame.rotation != 0 && frame.rotation != 90
            && frame.rotation != 180 && frame.rotation != 270) {
        if (error != nullptr) {
            *error = "frame rotation is invalid";
        }
        return false;
    }
    if (frame.format == SharedFrameBus::PixelFormat::kNv21
            || frame.format == SharedFrameBus::PixelFormat::kNv12) {
        if ((frame.width & 1U) != 0 || (frame.height & 1U) != 0
                || frame.y_stride < frame.width || frame.uv_stride < frame.width) {
            if (error != nullptr) {
                *error = "YUV frame dimensions or strides are invalid";
            }
            return false;
        }
        const size_t expected = static_cast<size_t>(frame.y_stride) * frame.height
                + static_cast<size_t>(frame.uv_stride) * (frame.height / 2);
        if (frame.payload.size() != expected) {
            if (error != nullptr) {
                *error = "NV21/NV12 payload does not match its declared strides";
            }
            return false;
        }
    }
    if (frame.format == SharedFrameBus::PixelFormat::kI420
            && ((frame.width & 1U) != 0 || (frame.height & 1U) != 0
                || frame.y_stride < frame.width || frame.uv_stride < frame.width / 2)) {
        if (error != nullptr) {
            *error = "I420 frame dimensions or strides are invalid";
        }
        return false;
    }
    if (frame.format == SharedFrameBus::PixelFormat::kI420) {
        const size_t expected = static_cast<size_t>(frame.y_stride) * frame.height
                + 2 * static_cast<size_t>(frame.uv_stride) * (frame.height / 2);
        if (frame.payload.size() != expected) {
            if (error != nullptr) {
                *error = "I420 payload does not match its declared strides";
            }
            return false;
        }
    }
    if (frame.format == SharedFrameBus::PixelFormat::kRgba8888) {
        if (frame.y_stride < frame.width * 4U
                || frame.payload.size()
                        != static_cast<size_t>(frame.y_stride) * frame.height) {
            if (error != nullptr) {
                *error = "RGBA payload does not match its declared stride";
            }
            return false;
        }
    }
    return true;
}

bool ValidateHeader(
        const SharedFrameBus::BusHeader* header,
        size_t mapping_size,
        std::string* error) {
    if (header == nullptr || mapping_size < sizeof(*header)
            || std::memcmp(header->magic, kMagic, sizeof(kMagic)) != 0
            || header->version != SharedFrameBus::kVersion
            || header->slot_count != SharedFrameBus::kSlotCount
            || header->header_size != AlignUp(sizeof(*header), kHeaderAlignment)
            || header->header_size > mapping_size
            || header->slot_capacity == 0
            || header->slot_capacity > SharedFrameBus::kDefaultSlotCapacity) {
        if (error != nullptr) {
            *error = "FrameBus v2 header is invalid";
        }
        return false;
    }
    const size_t header_size = header->header_size;
    const size_t slot_capacity = header->slot_capacity;
    if (slot_capacity > (std::numeric_limits<size_t>::max() - header_size)
                    / SharedFrameBus::kSlotCount
            || header_size + slot_capacity * SharedFrameBus::kSlotCount
                    != mapping_size) {
        if (error != nullptr) {
            *error = "FrameBus v2 mapping size does not match its header";
        }
        return false;
    }
    return true;
}

const uint8_t* SlotData(
        const void* mapping,
        const SharedFrameBus::BusHeader* header,
        uint32_t slot) {
    return static_cast<const uint8_t*>(mapping)
            + header->header_size
            + static_cast<size_t>(slot) * header->slot_capacity;
}

bool CopyLatestFromMapping(
        const void* mapping,
        size_t mapping_size,
        SharedFrameBus::Frame* frame,
        uint64_t* sequence,
        std::string* error) {
    if (mapping == nullptr || frame == nullptr) {
        if (error != nullptr) {
            *error = "shared frame bus reader is invalid";
        }
        return false;
    }
    const auto* header = static_cast<const SharedFrameBus::BusHeader*>(mapping);
    if (!ValidateHeader(header, mapping_size, error)) {
        return false;
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        const uint64_t published = LoadAcquire(&header->published_sequence);
        if (published == 0) {
            if (error != nullptr) {
                *error = "shared frame bus has no frame";
            }
            return false;
        }
        const uint32_t slot_index = static_cast<uint32_t>(
                published % SharedFrameBus::kSlotCount);
        const SharedFrameBus::SlotHeader* slot = &header->slots[slot_index];
        const uint64_t before = LoadAcquire(&slot->write_epoch);
        if ((before & 1U) != 0 || slot->sequence != published
                || slot->payload_size == 0
                || slot->payload_size > header->slot_capacity) {
            continue;
        }
        SharedFrameBus::Frame candidate;
        const uint8_t* payload = SlotData(mapping, header, slot_index);
        candidate.payload.assign(payload, payload + slot->payload_size);
        candidate.width = slot->width;
        candidate.height = slot->height;
        candidate.format = static_cast<SharedFrameBus::PixelFormat>(slot->format);
        candidate.y_stride = slot->y_stride;
        candidate.uv_stride = slot->uv_stride;
        candidate.rotation = slot->rotation;
        candidate.flags = slot->flags;
        candidate.presentation_time_ns = slot->presentation_time_ns;
        candidate.arrival_time_ns = slot->arrival_time_ns;
        const uint64_t after = LoadAcquire(&slot->write_epoch);
        if (before != after || (after & 1U) != 0) {
            continue;
        }
        if (!ValidateFrame(candidate, header->slot_capacity, error)) {
            return false;
        }
        *frame = std::move(candidate);
        if (sequence != nullptr) {
            *sequence = published;
        }
        return true;
    }
    if (error != nullptr) {
        *error = "shared frame changed during read";
    }
    return false;
}

}  // namespace

SharedFrameBus::~SharedFrameBus() {
    Close();
}

bool SharedFrameBus::Open(size_t slot_capacity, std::string* error) {
    Close();
    if (slot_capacity == 0 || slot_capacity > std::numeric_limits<uint32_t>::max()) {
        if (error != nullptr) {
            *error = "invalid shared frame slot capacity";
        }
        return false;
    }
    const size_t header_size = AlignUp(sizeof(BusHeader), kHeaderAlignment);
    if (slot_capacity > (std::numeric_limits<size_t>::max() - header_size) / kSlotCount) {
        if (error != nullptr) {
            *error = "shared frame bus size overflow";
        }
        return false;
    }
    const size_t mapping_size = header_size + slot_capacity * kSlotCount;
    const int fd = CreateMemfd();
    if (fd < 0) {
        if (error != nullptr) {
            *error = std::string("memfd_create failed: ") + std::strerror(errno);
        }
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(mapping_size)) != 0) {
        if (error != nullptr) {
            *error = std::string("frame bus resize failed: ") + std::strerror(errno);
        }
        close(fd);
        return false;
    }
    void* mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        if (error != nullptr) {
            *error = std::string("frame bus mmap failed: ") + std::strerror(errno);
        }
        close(fd);
        return false;
    }
#ifdef F_ADD_SEALS
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
        if (error != nullptr) {
            *error = std::string("frame bus sealing failed: ") + std::strerror(errno);
        }
        munmap(mapping, mapping_size);
        close(fd);
        return false;
    }
#endif

    fd_ = fd;
    mapping_ = mapping;
    mapping_size_ = mapping_size;
    slot_capacity_ = slot_capacity;
    next_sequence_ = 1;
    std::memset(mapping_, 0, mapping_size_);
    auto* header = static_cast<BusHeader*>(mapping_);
    std::memcpy(header->magic, kMagic, sizeof(kMagic));
    header->version = kVersion;
    header->header_size = static_cast<uint32_t>(header_size);
    header->slot_count = kSlotCount;
    header->slot_capacity = static_cast<uint32_t>(slot_capacity);
    header->producer_generation = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    return true;
}

void SharedFrameBus::Close() {
    if (mapping_ != nullptr) {
        munmap(mapping_, mapping_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    fd_ = -1;
    mapping_ = nullptr;
    mapping_size_ = 0;
    slot_capacity_ = 0;
    next_sequence_ = 1;
}

bool SharedFrameBus::Publish(const Frame& frame, std::string* error) {
    if (!is_open()) {
        if (error != nullptr) {
            *error = "shared frame bus is not open";
        }
        return false;
    }
    if (!ValidateFrame(frame, slot_capacity_, error)) {
        return false;
    }
    auto* header = static_cast<BusHeader*>(mapping_);
    const uint64_t sequence = next_sequence_++;
    const uint32_t slot_index = static_cast<uint32_t>(sequence % kSlotCount);
    SlotHeader* slot = &header->slots[slot_index];
    uint64_t epoch = LoadAcquire(&slot->write_epoch);
    if ((epoch & 1U) != 0) {
        ++epoch;
    }
    StoreRelease(&slot->write_epoch, epoch + 1);
    std::memcpy(SlotData(slot_index), frame.payload.data(), frame.payload.size());
    slot->sequence = sequence;
    slot->presentation_time_ns = frame.presentation_time_ns;
    slot->arrival_time_ns = frame.arrival_time_ns;
    slot->payload_size = static_cast<uint32_t>(frame.payload.size());
    slot->width = frame.width;
    slot->height = frame.height;
    slot->format = static_cast<uint32_t>(frame.format);
    slot->y_stride = frame.y_stride;
    slot->uv_stride = frame.uv_stride;
    slot->rotation = frame.rotation;
    slot->flags = frame.flags;
    StoreRelease(&slot->write_epoch, epoch + 2);
    StoreRelease(&header->published_sequence, sequence);
    return true;
}

bool SharedFrameBus::PublishJpeg(
        const std::vector<uint8_t>& jpeg,
        int width,
        int height,
        int64_t presentation_time_ns,
        int64_t arrival_time_ns,
        std::string* error) {
    Frame frame;
    frame.payload = jpeg;
    frame.width = static_cast<uint32_t>(width);
    frame.height = static_cast<uint32_t>(height);
    frame.format = PixelFormat::kJpeg;
    frame.presentation_time_ns = presentation_time_ns;
    frame.arrival_time_ns = arrival_time_ns;
    return Publish(frame, error);
}

void SharedFrameBus::Invalidate() {
    if (is_open()) {
        auto* header = static_cast<BusHeader*>(mapping_);
        StoreRelease(&header->published_sequence, 0);
    }
}

int SharedFrameBus::DuplicateFd(std::string* error) const {
    if (fd_ < 0) {
        if (error != nullptr) {
            *error = "shared frame bus is not open";
        }
        return -1;
    }
    const std::string descriptor_path = "/proc/self/fd/" + std::to_string(fd_);
    const int duplicate = open(descriptor_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (duplicate < 0 && error != nullptr) {
        *error = std::string("read-only frame bus fd open failed: ")
                + std::strerror(errno);
    }
    return duplicate;
}

std::string SharedFrameBus::Descriptor() const {
    if (!is_open()) {
        return "";
    }
    const auto* header = static_cast<const BusHeader*>(mapping_);
    std::ostringstream value;
    value << "transport=memfd-ring-v2\n"
          << "bus_version=" << header->version << '\n'
          << "header_size=" << header->header_size << '\n'
          << "slot_count=" << header->slot_count << '\n'
          << "slot_capacity=" << header->slot_capacity << '\n'
          << "memory_access=read-only\n"
          << "formats=jpeg,nv21,nv12,i420,rgba8888\n"
          << "preferred_format=nv21\n";
    return value.str();
}

bool SharedFrameBus::CopyLatest(
        Frame* frame,
        uint64_t* sequence,
        std::string* error) const {
    if (!is_open()) {
        if (error != nullptr) {
            *error = "shared frame bus is not open";
        }
        return false;
    }
    return CopyLatestFromMapping(mapping_, mapping_size_, frame, sequence, error);
}

bool SharedFrameBus::ValidateConsumerFd(int fd, std::string* error) {
    if (fd < 0) {
        if (error != nullptr) {
            *error = "FrameBus consumer fd is invalid";
        }
        return false;
    }
    const int access_flags = fcntl(fd, F_GETFL);
    if (access_flags < 0 || (access_flags & O_ACCMODE) != O_RDONLY) {
        if (error != nullptr) {
            *error = "FrameBus consumer fd must be read-only";
        }
        return false;
    }
#ifdef F_GET_SEALS
    const int seals = fcntl(fd, F_GET_SEALS);
    constexpr int kRequiredSeals = F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
    if (seals < 0 || (seals & kRequiredSeals) != kRequiredSeals) {
        if (error != nullptr) {
            *error = "FrameBus consumer fd is not size-sealed";
        }
        return false;
    }
#endif
    struct stat info{};
    if (fstat(fd, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(BusHeader))
            || static_cast<uintmax_t>(info.st_size)
                    > std::numeric_limits<size_t>::max()) {
        if (error != nullptr) {
            *error = "FrameBus consumer fd size is invalid";
        }
        return false;
    }
    const size_t mapping_size = static_cast<size_t>(info.st_size);
    void* mapping = mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        if (error != nullptr) {
            *error = std::string("FrameBus consumer mmap failed: ")
                    + std::strerror(errno);
        }
        return false;
    }
    const bool valid = ValidateHeader(static_cast<const BusHeader*>(mapping),
                                      mapping_size, error);
    munmap(mapping, mapping_size);
    return valid;
}

bool SharedFrameBus::CopyLatestFromFd(
        int fd,
        Frame* frame,
        uint64_t* sequence,
        std::string* error) {
    SharedFrameBusReader reader;
    return reader.Attach(fd, error) && reader.CopyLatest(frame, sequence, error);
}

uint8_t* SharedFrameBus::SlotData(uint32_t slot) const {
    const auto* header = static_cast<const BusHeader*>(mapping_);
    return static_cast<uint8_t*>(mapping_) + header->header_size + slot * slot_capacity_;
}

SharedFrameBusReader::~SharedFrameBusReader() {
    Close();
}

bool SharedFrameBusReader::Attach(int read_only_fd, std::string* error) {
    Close();
    if (!SharedFrameBus::ValidateConsumerFd(read_only_fd, error)) {
        return false;
    }
    struct stat info{};
    if (fstat(read_only_fd, &info) != 0) {
        if (error != nullptr) {
            *error = "FrameBus consumer stat failed";
        }
        return false;
    }
    const int duplicate = fcntl(read_only_fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) {
        if (error != nullptr) {
            *error = std::string("FrameBus reader fd duplication failed: ")
                    + std::strerror(errno);
        }
        return false;
    }
    const size_t mapping_size = static_cast<size_t>(info.st_size);
    void* mapping = mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, duplicate, 0);
    if (mapping == MAP_FAILED) {
        if (error != nullptr) {
            *error = std::string("FrameBus reader mmap failed: ")
                    + std::strerror(errno);
        }
        close(duplicate);
        return false;
    }
    if (!ValidateHeader(static_cast<const SharedFrameBus::BusHeader*>(mapping),
                        mapping_size, error)) {
        munmap(mapping, mapping_size);
        close(duplicate);
        return false;
    }
    fd_ = duplicate;
    mapping_ = mapping;
    mapping_size_ = mapping_size;
    return true;
}

void SharedFrameBusReader::Close() {
    if (mapping_ != nullptr) {
        munmap(mapping_, mapping_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    fd_ = -1;
    mapping_ = nullptr;
    mapping_size_ = 0;
}

bool SharedFrameBusReader::CopyLatest(
        SharedFrameBus::Frame* frame,
        uint64_t* sequence,
        std::string* error) const {
    if (!is_attached()) {
        if (error != nullptr) {
            *error = "FrameBus reader is not attached";
        }
        return false;
    }
    return CopyLatestFromMapping(mapping_, mapping_size_, frame, sequence, error);
}

}  // namespace vcames
