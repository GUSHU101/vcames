#include "vcames/network_policy.h"

#include <array>
#include <cstdint>
#include <iostream>

int main() {
    using vcames::IpAddressFamily;
    const std::array<uint8_t, 4> loopback{127, 0, 0, 1};
    const std::array<uint8_t, 4> private_lan{192, 168, 5, 20};
    const std::array<uint8_t, 4> cgnat{100, 64, 0, 1};
    const std::array<uint8_t, 4> public_dns{8, 8, 8, 8};
    const std::array<uint8_t, 16> unique_local{
        0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const std::array<uint8_t, 16> link_local{
        0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const std::array<uint8_t, 16> mapped_private{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 1, 10};
    const std::array<uint8_t, 16> mapped_public{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 8, 8, 8, 8};
    const std::array<uint8_t, 16> public_v6{
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88};
    if (!vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv4, loopback.data(), loopback.size())
            || !vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv4, private_lan.data(), private_lan.size())
            || !vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv4, cgnat.data(), cgnat.size())
            || vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv4, public_dns.data(), public_dns.size())
            || !vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv6, unique_local.data(), unique_local.size())
            || !vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv6, link_local.data(), link_local.size())
            || !vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv6, mapped_private.data(), mapped_private.size())
            || vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv6, mapped_public.data(), mapped_public.size())
            || vcames::IsPrivateNetworkAddress(
                IpAddressFamily::kIpv6, public_v6.data(), public_v6.size())) {
        std::cerr << "private-network policy classification failed\n";
        return 1;
    }
    std::cout << "network source policy tests passed\n";
    return 0;
}
