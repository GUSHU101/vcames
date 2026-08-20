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

#if defined(__linux__)
#include <linux/memfd.h>
#endif

namespace vcames {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'F', 'B', 'U', 'S', '1', '\0'};
constexpr uint32_t kFormatJpeg = 1;
constexpr size_t kHeaderAlignment = 4096;

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
            "vcames-frame-bus",
            MFD_CLOEXEC | MFD_ALLOW_SEALING));
#else
    errno = ENOSYS;
    return -1;
#endif
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

bool SharedFrameBus::PublishJpeg(
        const std::vector<uint8_t>& jpeg,
        int width,
        int height,
        int64_t presentation_time_ns,
        int64_t arrival_time_ns,
        std::string* error) {
    if (!is_open()) {
        if (error != nullptr) {
            *error = "shared frame bus is not open";
        }
        return false;
    }
    if (jpeg.empty() || jpeg.size() > slot_capacity_) {
        if (error != nullptr) {
            *error = "JPEG frame exceeds shared frame slot capacity";
        }
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
    std::memcpy(SlotData(slot_index), jpeg.data(), jpeg.size());
    slot->sequence = sequence;
    slot->presentation_time_ns = presentation_time_ns;
    slot->arrival_time_ns = arrival_time_ns;
    slot->payload_size = static_cast<uint32_t>(jpeg.size());
    slot->width = static_cast<uint32_t>(width);
    slot->height = static_cast<uint32_t>(height);
    slot->format = kFormatJpeg;
    slot->flags = 0;
    StoreRelease(&slot->write_epoch, epoch + 2);
    StoreRelease(&header->published_sequence, sequence);
    return true;
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
    const int duplicate = fcntl(fd_, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0 && error != nullptr) {
        *error = std::string("frame bus fd duplication failed: ") + std::strerror(errno);
    }
    return duplicate;
}

std::string SharedFrameBus::Descriptor() const {
    if (!is_open()) {
        return "";
    }
    const auto* header = static_cast<const BusHeader*>(mapping_);
    std::ostringstream value;
    value << "transport=memfd-ring-v1\n"
          << "bus_version=" << header->version << '\n'
          << "header_size=" << header->header_size << '\n'
          << "slot_count=" << header->slot_count << '\n'
          << "slot_capacity=" << header->slot_capacity << '\n'
          << "format=jpeg\n";
    return value.str();
}

bool SharedFrameBus::CopyLatest(
        std::vector<uint8_t>* jpeg,
        uint64_t* sequence,
        std::string* error) const {
    if (!is_open() || jpeg == nullptr) {
        if (error != nullptr) {
            *error = "shared frame bus reader is invalid";
        }
        return false;
    }
    const auto* header = static_cast<const BusHeader*>(mapping_);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const uint64_t published = LoadAcquire(&header->published_sequence);
        if (published == 0) {
            if (error != nullptr) {
                *error = "shared frame bus has no frame";
            }
            return false;
        }
        const uint32_t slot_index = static_cast<uint32_t>(published % kSlotCount);
        const SlotHeader* slot = &header->slots[slot_index];
        const uint64_t before = LoadAcquire(&slot->write_epoch);
        if ((before & 1U) != 0 || slot->sequence != published
                || slot->payload_size == 0 || slot->payload_size > slot_capacity_) {
            continue;
        }
        jpeg->assign(SlotData(slot_index), SlotData(slot_index) + slot->payload_size);
        const uint64_t after = LoadAcquire(&slot->write_epoch);
        if (before == after && (after & 1U) == 0) {
            if (sequence != nullptr) {
                *sequence = published;
            }
            return true;
        }
    }
    if (error != nullptr) {
        *error = "shared frame changed during read";
    }
    return false;
}

uint8_t* SharedFrameBus::SlotData(uint32_t slot) const {
    const auto* header = static_cast<const BusHeader*>(mapping_);
    return static_cast<uint8_t*>(mapping_) + header->header_size + slot * slot_capacity_;
}

}  // namespace vcames
