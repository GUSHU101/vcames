#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace vcames {

// Versioned, latest-frame shared-memory transport between vcamesd and a
// build-specific Camera HAL adapter. The producer owns the mapping and passes
// a duplicate of the memfd over SCM_RIGHTS.
class SharedFrameBus {
public:
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kSlotCount = 3;
    static constexpr size_t kDefaultSlotCapacity = 16 * 1024 * 1024;

    SharedFrameBus() = default;
    ~SharedFrameBus();
    SharedFrameBus(const SharedFrameBus&) = delete;
    SharedFrameBus& operator=(const SharedFrameBus&) = delete;

    bool Open(size_t slot_capacity, std::string* error);
    void Close();
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

    // Used by host tests and diagnostic readers. Camera adapters should map
    // the received fd using the public wire layout below.
    bool CopyLatest(std::vector<uint8_t>* jpeg, uint64_t* sequence, std::string* error) const;

    struct SlotHeader {
        uint64_t write_epoch;
        uint64_t sequence;
        int64_t presentation_time_ns;
        int64_t arrival_time_ns;
        uint32_t payload_size;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t flags;
        uint32_t reserved;
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
