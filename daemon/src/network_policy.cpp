#include "vcames/network_policy.h"

#include <algorithm>

namespace vcames {
namespace {

bool PrivateIpv4(const uint8_t* value) {
    return value[0] == 10
            || value[0] == 127
            || (value[0] == 100 && (value[1] & 0xc0U) == 0x40U)
            || (value[0] == 169 && value[1] == 254)
            || (value[0] == 172 && (value[1] & 0xf0U) == 16U)
            || (value[0] == 192 && value[1] == 168);
}

}  // namespace

bool IsPrivateNetworkAddress(
        IpAddressFamily family,
        const uint8_t* address,
        size_t size) {
    if (address == nullptr) {
        return false;
    }
    if (family == IpAddressFamily::kIpv4) {
        return size == 4 && PrivateIpv4(address);
    }
    if (size != 16) {
        return false;
    }
    const bool loopback = std::all_of(address, address + 15, [](uint8_t byte) {
        return byte == 0;
    }) && address[15] == 1;
    const bool unique_local = (address[0] & 0xfeU) == 0xfcU;
    const bool link_local = address[0] == 0xfe && (address[1] & 0xc0U) == 0x80U;
    const bool mapped_ipv4 = std::all_of(address, address + 10, [](uint8_t byte) {
        return byte == 0;
    }) && address[10] == 0xff && address[11] == 0xff
            && PrivateIpv4(address + 12);
    return loopback || unique_local || link_local || mapped_ipv4;
}

}  // namespace vcames
