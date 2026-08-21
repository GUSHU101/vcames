#include "vcames/config.h"
#include "vcames/replacement_adapter.h"
#include "vcames/shared_frame_bus.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr char kSocketName[] = "vcames-camera-adapter";

int CreateListener() {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    std::memcpy(address.sun_path + 1, kSocketName, sizeof(kSocketName) - 1);
    const socklen_t size = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + sizeof(kSocketName));
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), size) != 0
            || listen(fd, 5) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool Contains(const std::string& value, const char* field) {
    return value.find(field) != std::string::npos;
}

bool ValidateFrameBus(int frame_fd, const std::string& request) {
    struct stat info{};
    if (frame_fd < 0 || fstat(frame_fd, &info) != 0
            || info.st_size < static_cast<off_t>(sizeof(vcames::SharedFrameBus::BusHeader))) {
        return false;
    }
    void* mapping = mmap(nullptr, sizeof(vcames::SharedFrameBus::BusHeader),
                         PROT_READ, MAP_SHARED, frame_fd, 0);
    if (mapping == MAP_FAILED) {
        return false;
    }
    const auto* header = static_cast<const vcames::SharedFrameBus::BusHeader*>(mapping);
    const bool valid = std::memcmp(header->magic, "VCFBUS2\0", 8) == 0
            && header->version == 2 && header->slot_count == 4
            && Contains(request, "transport=memfd-ring-v2")
            && Contains(request, "preferred_format=nv21");
    munmap(mapping, sizeof(vcames::SharedFrameBus::BusHeader));
    return valid;
}

bool HandlePhase(int listener, int phase) {
    int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
        return false;
    }
    char request_buffer[4096]{};
    char control[CMSG_SPACE(sizeof(int))]{};
    iovec io{request_buffer, sizeof(request_buffer) - 1};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    const ssize_t count = recvmsg(client, &message, 0);
    int frame_fd = -1;
    for (cmsghdr* item = CMSG_FIRSTHDR(&message);
         item != nullptr;
         item = CMSG_NXTHDR(&message, item)) {
        if (item->cmsg_level == SOL_SOCKET && item->cmsg_type == SCM_RIGHTS
                && item->cmsg_len >= CMSG_LEN(sizeof(int))) {
            std::memcpy(&frame_fd, CMSG_DATA(item), sizeof(frame_fd));
            break;
        }
    }
    const std::string request = count > 0
            ? std::string(request_buffer, static_cast<size_t>(count))
            : "";
    bool valid = Contains(request, "adapter_protocol=2");
    const char* response = "ERROR invalid phase\n";
    switch (phase) {
        case 0:
            valid = valid && request.starts_with("GET_INFO\n") && frame_fd < 0;
            response = "OK\nadapter_protocol=2\napi=30-33\n"
                    "metadata_policy=preserve-oem\n";
            break;
        case 1:
            valid = valid && request.starts_with("PROBE\n") && frame_fd < 0
                    && Contains(request, "require_exact_build=1");
            response = "OK\nadapter_protocol=2\ncompatibility=exact-build\n"
                    "secure_stream_policy=reject\n";
            break;
        case 2:
            valid = valid && request.starts_with("ATTACH_BUS\n")
                    && ValidateFrameBus(frame_fd, request);
            response = "OK\nadapter_protocol=2\nframe_transport=attached\n"
                    "bus_version=2\n";
            break;
        case 3:
            valid = valid && request.starts_with("ACTIVATE\n") && frame_fd < 0
                    && Contains(request, "target=front")
                    && Contains(request, "metadata_policy=preserve-oem")
                    && Contains(request, "secure_stream_policy=reject")
                    && Contains(request, "failure_policy=oem-passthrough");
            response = "OK\nadapter_protocol=2\npipeline=active\nmetadata=preserved\n";
            break;
        case 4:
            valid = valid && request.starts_with("HEALTH\n") && frame_fd < 0;
            response = "OK\nadapter_protocol=2\nhealth=ready\n"
                    "frame_transport=attached\npipeline=active\n";
            break;
        default:
            valid = false;
            break;
    }
    if (frame_fd >= 0) {
        close(frame_fd);
    }
    const char* actual_response = valid ? response : "ERROR invalid request\n";
    const ssize_t ignored = write(client, actual_response, std::strlen(actual_response));
    (void)ignored;
    close(client);
    return valid;
}

void Serve(int listener, bool* valid) {
    *valid = true;
    for (int phase = 0; phase < 5; ++phase) {
        if (!HandlePhase(listener, phase)) {
            *valid = false;
            return;
        }
    }
}

}  // namespace

int main() {
    int listener = CreateListener();
    if (listener < 0) {
        std::cerr << "listener failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    bool valid = false;
    std::thread server(Serve, listener, &valid);

    vcames::SharedFrameBus bus;
    std::string error;
    vcames::Config config;
    config.target = "front";
    if (!bus.Open(4096, &error)) {
        std::cerr << error << '\n';
        close(listener);
        server.join();
        return 1;
    }
    int frame_fd = bus.DuplicateFd(&error);
    const bool activated = frame_fd >= 0 && vcames::ActivateReplacementAdapter(
            config, frame_fd, bus.Descriptor(), &error);
    if (frame_fd >= 0) {
        close(frame_fd);
    }
    server.join();
    close(listener);
    if (!activated || !valid) {
        std::cerr << "adapter activation failed: " << error << '\n';
        return 1;
    }
    std::cout << "replacement adapter phased handshake test passed\n";
    return 0;
}
