#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace vcames {

// Versioned latest-frame shared-memory transport between vcamesd and an
// exact-build Camera HAL adapter. The producer passes a duplicate memfd over
// SCM_RIGHTS; a reader must validate every public header field before mapping.
class SharedFrameBus {
public:
    enum class PixelFormat : uint32_t {
        kJpeg = 1,
        kNv21 = 2,
        kNv12 = 3,
        kI420 = 4,
        kRgba8888 = 5,
    };

    struct Frame {
        std::vector<uint8_t> payload;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t y_stride = 0;
        uint32_t uv_stride = 0;
        PixelFormat format = PixelFormat::kJpeg;
        uint32_t rotation = 0;
        uint32_t flags = 0;
        int64_t presentation_time_ns = 0;
        int64_t arrival_time_ns = 0;
    };

    static constexpr uint32_t kVersion = 2;
    static constexpr uint32_t kSlotCount = 4;
    static constexpr size_t kDefaultSlotCapacity = 16 * 1024 * 1024;

    SharedFrameBus() = default;
    ~SharedFrameBus();
    SharedFrameBus(const SharedFrameBus&) = delete;
    SharedFrameBus& operator=(const SharedFrameBus&) = delete;

    bool Open(size_t slot_capacity, std::string* error);
    void Close();
    bool Publish(const Frame& frame, std::string* error);
    bool PublishJpeg(
            const std::vector<uint8_t>& jpeg,
            int width,
            int height,
            int64_t presentation_time_ns,
            int64_t arrival_time_ns,
            std::string* error);
    void Invalidate();
    int DuplicateFd(std::string* error) const;
    std::string Descriptor() const;
    bool is_open() const { return mapping_ != nullptr; }

    // Used by tests and diagnostic readers. Production adapters map the
    // received fd and consume the public wire layout below directly.
    bool CopyLatest(Frame* frame, uint64_t* sequence, std::string* error) const;

    struct SlotHeader {
        uint64_t write_epoch;
        uint64_t sequence;
        int64_t presentation_time_ns;
        int64_t arrival_time_ns;
        uint32_t payload_size;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t y_stride;
        uint32_t uv_stride;
        uint32_t rotation;
        uint32_t flags;
    };

    struct BusHeader {
        char magic[8];
        uint32_t version;
        uint32_t header_size;
        uint32_t slot_count;
        uint32_t slot_capacity;
        uint64_t producer_generation;
        uint64_t published_sequence;
        uint64_t reserved[4];
        SlotHeader slots[kSlotCount];
    };

    static_assert(std::is_standard_layout_v<BusHeader>);
    static_assert(sizeof(BusHeader) <= 4096);

private:
    uint8_t* SlotData(uint32_t slot) const;

    int fd_ = -1;
    void* mapping_ = nullptr;
    size_t mapping_size_ = 0;
    size_t slot_capacity_ = 0;
    uint64_t next_sequence_ = 1;
};

}  // namespace vcames
