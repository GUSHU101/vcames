#include "vcames/replacement_adapter.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <sstream>
#include <string>

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

bool Request(const std::string& request, std::string* response, std::string* error) {
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
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials,
                       &credential_size) != 0
                || (credentials.uid != 0 && credentials.uid != 1000
                    && credentials.uid != 1047)) {
            if (error != nullptr) {
                *error = "replacement adapter peer has an untrusted Android UID";
            }
            close(fd);
            return false;
        }
        if (!WriteAll(fd, request)) {
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

}  // namespace

bool ActivateReplacementAdapter(const Config& config, std::string* error) {
    if (config.target == "external") {
        DeactivateReplacementAdapter();
        return true;
    }
    std::ostringstream request;
    request << "ACTIVATE\n"
            << "target=" << config.target << '\n'
            << "device=" << config.device << '\n'
            << "width=" << config.width << '\n'
            << "height=" << config.height << '\n'
            << "fps=" << config.fps << "\n.\n";
    return Request(request.str(), nullptr, error);
}

void DeactivateReplacementAdapter() {
    std::string ignored;
    Request("DEACTIVATE\n.\n", nullptr, &ignored);
}

}  // namespace vcames
