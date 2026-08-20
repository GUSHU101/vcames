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
            || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void ServeOnce(int listener, bool* valid) {
    int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
        return;
    }
    char request[4096]{};
    char control[CMSG_SPACE(sizeof(int))]{};
    iovec io{request, sizeof(request) - 1};
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
    std::string text = count > 0 ? std::string(request, static_cast<size_t>(count)) : "";
    struct stat info{};
    if (frame_fd >= 0 && fstat(frame_fd, &info) == 0
            && info.st_size >= static_cast<off_t>(sizeof(vcames::SharedFrameBus::BusHeader))) {
        void* mapping = mmap(nullptr, sizeof(vcames::SharedFrameBus::BusHeader),
                             PROT_READ, MAP_SHARED, frame_fd, 0);
        if (mapping != MAP_FAILED) {
            const auto* header = static_cast<const vcames::SharedFrameBus::BusHeader*>(mapping);
            *valid = std::memcmp(header->magic, "VCFBUS1\0", 8) == 0
                    && header->version == 1 && header->slot_count == 3
                    && text.find("transport=memfd-ring-v1") != std::string::npos
                    && text.find("target=front") != std::string::npos;
            munmap(mapping, sizeof(vcames::SharedFrameBus::BusHeader));
        }
    }
    if (frame_fd >= 0) {
        close(frame_fd);
    }
    const char* response = *valid ? "OK\n" : "ERROR invalid transport\n";
    const ssize_t ignored = write(client, response, std::strlen(response));
    (void)ignored;
    close(client);
}

}  // namespace

int main() {
    int listener = CreateListener();
    if (listener < 0) {
        std::cerr << "listener failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    bool valid = false;
    std::thread server(ServeOnce, listener, &valid);

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
    std::cout << "replacement adapter fd integration test passed\n";
    return 0;
}
