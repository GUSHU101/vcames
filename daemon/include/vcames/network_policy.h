#pragma once

#include <cstddef>
#include <cstdint>

namespace vcames {

enum class IpAddressFamily {
    kIpv4,
    kIpv6,
};

// Cleartext MJPEG is deliberately limited to local/private networks. Public
// Internet sources require a trusted VPN that exposes a private address.
bool IsPrivateNetworkAddress(
        IpAddressFamily family,
        const uint8_t* address,
        size_t size);

}  // namespace vcames
