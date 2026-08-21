#include "vcames/replacement_adapter.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace vcames {
namespace {

constexpr char kAdapterSocketName[] = "vcames-camera-adapter";
constexpr size_t kMaxResponseBytes = 4096;

bool WriteAll(int fd, const std::string& value) {
    size_t written = 0;
    while (written < value.size()) {
        const ssize_t count = write(fd, value.data() + written, value.size() - written);
        if (count > 0) {
            written += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool SendWithOptionalFd(int socket_fd, const std::string& request, int passed_fd) {
    if (passed_fd < 0) {
        return WriteAll(socket_fd, request);
    }
    iovec io{};
    io.iov_base = const_cast<char*>(request.data());
    io.iov_len = request.size();
    char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(header), &passed_fd, sizeof(passed_fd));
    const ssize_t sent = sendmsg(socket_fd, &message, MSG_NOSIGNAL);
    if (sent <= 0) {
        return false;
    }
    return static_cast<size_t>(sent) == request.size()
            || WriteAll(socket_fd, request.substr(static_cast<size_t>(sent)));
}

bool Request(
        const std::string& request,
        int passed_fd,
        std::string* response,
        std::string* error) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    std::memcpy(address.sun_path + 1, kAdapterSocketName,
                sizeof(kAdapterSocketName) - 1);
    const socklen_t address_length = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + sizeof(kAdapterSocketName));

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error != nullptr) {
            *error = std::string("replacement adapter socket failed: ")
                    + std::strerror(errno);
        }
        return false;
    }
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    bool ok = false;
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), address_length) == 0) {
        ucred credentials{};
        socklen_t credential_size = sizeof(credentials);
        bool trusted = false;
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials,
                       &credential_size) == 0) {
#ifdef __ANDROID__
            trusted = credentials.uid == 0 || credentials.uid == 1000
                    || credentials.uid == 1047;
#else
            trusted = credentials.uid == getuid();
#endif
        }
        if (!trusted) {
            if (error != nullptr) {
                *error = "replacement adapter peer has an untrusted Android UID";
            }
            close(fd);
            return false;
        }
        if (!SendWithOptionalFd(fd, request, passed_fd)) {
            if (error != nullptr) {
                *error = std::string("replacement adapter write failed: ")
                        + std::strerror(errno);
            }
            close(fd);
            return false;
        }
        shutdown(fd, SHUT_WR);
        std::string result;
        char buffer[512];
        for (;;) {
            const ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count > 0) {
                result.append(buffer, static_cast<size_t>(count));
                if (result.size() > kMaxResponseBytes) {
                    break;
                }
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        if (result.starts_with("OK\n") || result == "OK") {
            if (response != nullptr) {
                *response = std::move(result);
            }
            ok = true;
        } else if (error != nullptr) {
            *error = result.empty() ? "replacement adapter returned no readiness response"
                                    : "replacement adapter rejected request: " + result;
        }
    } else if (error != nullptr) {
        *error = std::string("no compatible front/back camera adapter: ")
                + std::strerror(errno);
    }
    close(fd);
    return ok;
}

bool RequestAndRequire(
        const std::string& request,
        int passed_fd,
        const std::vector<std::string_view>& required_fields,
        const char* phase,
        std::string* error) {
    std::string response;
    if (!Request(request, passed_fd, &response, error)) {
        return false;
    }
    for (std::string_view field : required_fields) {
        if (response.find(field) == std::string::npos) {
            if (error != nullptr) {
                *error = std::string("replacement adapter ") + phase
                        + " response is missing " + std::string(field);
            }
            return false;
        }
    }
    return true;
}

}  // namespace

bool ActivateReplacementAdapter(
        const Config& config,
        int frame_bus_fd,
        const std::string& frame_bus_descriptor,
        std::string* error) {
    if (frame_bus_fd < 0 || frame_bus_descriptor.empty()) {
        if (error != nullptr) {
            *error = "replacement mode requires a shared frame transport";
        }
        return false;
    }
    if (!RequestAndRequire(
                "GET_INFO\nadapter_protocol=2\n.\n",
                -1,
                {"adapter_protocol=2", "api=30-33", "metadata_policy=preserve-oem"},
                "GET_INFO",
                error)
            || !RequestAndRequire(
                "PROBE\nadapter_protocol=2\nrequire_exact_build=1\n.\n",
                -1,
                {"adapter_protocol=2", "compatibility=exact-build",
                 "secure_stream_policy=reject"},
                "PROBE",
                error)) {
        DeactivateReplacementAdapter();
        return false;
    }

    std::ostringstream attach;
    attach << "ATTACH_BUS\n"
            << "adapter_protocol=2\n"
            << frame_bus_descriptor
            << ".\n";
    if (!RequestAndRequire(
                attach.str(),
                frame_bus_fd,
                {"adapter_protocol=2", "frame_transport=attached", "bus_version=2",
                 "memory_access=read-only"},
                "ATTACH_BUS",
                error)) {
        DeactivateReplacementAdapter();
        return false;
    }

    std::ostringstream activate;
    activate << "ACTIVATE\n"
            << "adapter_protocol=2\n"
            << "target=" << config.target << '\n'
            << "width=" << config.width << '\n'
            << "height=" << config.height << '\n'
            << "fps=" << config.fps << '\n'
            << "metadata_policy=preserve-oem\n"
            << "secure_stream_policy=reject\n"
            << "failure_policy=oem-passthrough\n"
            << ".\n";
    if (!RequestAndRequire(
                activate.str(),
                -1,
                {"adapter_protocol=2", "pipeline=active", "metadata=preserved"},
                "ACTIVATE",
                error)
            || !RequestAndRequire(
                "HEALTH\nadapter_protocol=2\n.\n",
                -1,
                {"adapter_protocol=2", "health=ready",
                 "frame_transport=attached", "pipeline=active"},
                "HEALTH",
                error)) {
        DeactivateReplacementAdapter();
        return false;
    }
    return true;
}

bool CheckReplacementAdapterHealth(std::string* error) {
    return RequestAndRequire(
            "HEALTH\nadapter_protocol=2\n.\n",
            -1,
            {"adapter_protocol=2", "health=ready",
             "frame_transport=attached", "pipeline=active"},
            "HEALTH",
            error);
}

void DeactivateReplacementAdapter() {
    std::string ignored;
    Request("DEACTIVATE\n.\n", -1, nullptr, &ignored);
}

}  // namespace vcames
