#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <grp.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

struct Options {
    uid_t allowed_uid = static_cast<uid_t>(-1);
    std::string public_control = "vcamesd";
    std::string private_control = "vcamesd_private";
    std::string public_frames = "vcamesd_frames";
    std::string private_frames = "vcamesd_frames_private";
    bool drop_to_system = false;
};

std::atomic<bool> g_stop{false};
std::array<std::atomic<int>, 2> g_listeners{-1, -1};

bool SafeSocketName(std::string_view name) {
    return !name.empty()
            && name.size() < sizeof(((sockaddr_un*)nullptr)->sun_path) - 1
            && name.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")
                    == std::string_view::npos;
}

bool ParseUid(std::string_view text, uid_t* uid) {
    unsigned long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc()
            || result.ptr != text.data() + text.size()
            || value < 10000 || value > 2'000'000) {
        return false;
    }
    *uid = static_cast<uid_t>(value);
    return true;
}

bool ParseOptions(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--drop-to-system") {
            options->drop_to_system = true;
            continue;
        }
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view value(argv[++index]);
        if (argument == "--allowed-uid") {
            if (!ParseUid(value, &options->allowed_uid)) {
                return false;
            }
            continue;
        }
        std::string* target = nullptr;
        if (argument == "--public-control") target = &options->public_control;
        if (argument == "--private-control") target = &options->private_control;
        if (argument == "--public-frames") target = &options->public_frames;
        if (argument == "--private-frames") target = &options->private_frames;
        if (target == nullptr || !SafeSocketName(value)) {
            return false;
        }
        *target = value;
    }
    return options->allowed_uid != static_cast<uid_t>(-1)
            && options->public_control != options->private_control
            && options->public_frames != options->private_frames;
}

void SignalHandler(int) {
    g_stop.store(true, std::memory_order_relaxed);
    for (auto& listener : g_listeners) {
        const int fd = listener.exchange(-1, std::memory_order_relaxed);
        if (fd >= 0) {
            close(fd);
        }
    }
}

socklen_t MakeAddress(const std::string& name, sockaddr_un* address) {
    *address = sockaddr_un{};
    address->sun_family = AF_UNIX;
    address->sun_path[0] = '\0';
    std::memcpy(address->sun_path + 1, name.data(), name.size());
    return static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + 1 + name.size());
}

int CreateListener(const std::string& name) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    const socklen_t size = MakeAddress(name, &address);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), size) != 0
            || listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int Connect(const std::string& name) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    const socklen_t size = MakeAddress(name, &address);
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool AllowedPeer(int fd, uid_t allowed_uid) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0
            && (credentials.uid == allowed_uid
                || credentials.uid == 0 || credentials.uid == 1000);
}

bool WriteAll(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const ssize_t count = send(
                fd, data + written, size - written, MSG_NOSIGNAL);
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

void Relay(int client, int target) {
    bool client_readable = true;
    bool target_readable = true;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (!g_stop.load(std::memory_order_relaxed)
            && (client_readable || target_readable)) {
        pollfd descriptors[2] = {
            {client, static_cast<short>(client_readable ? POLLIN : 0), 0},
            {target, static_cast<short>(target_readable ? POLLIN : 0), 0},
        };
        const int result = poll(descriptors, 2, 1000);
        if (result < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (result == 0) continue;
        if (client_readable && (descriptors[0].revents & (POLLIN | POLLHUP)) != 0) {
            const ssize_t count = read(client, buffer.data(), buffer.size());
            if (count > 0) {
                if (!WriteAll(target, buffer.data(), static_cast<size_t>(count))) break;
            } else {
                client_readable = false;
                shutdown(target, SHUT_WR);
            }
        }
        if (target_readable && (descriptors[1].revents & (POLLIN | POLLHUP)) != 0) {
            const ssize_t count = read(target, buffer.data(), buffer.size());
            if (count > 0) {
                if (!WriteAll(client, buffer.data(), static_cast<size_t>(count))) break;
            } else {
                target_readable = false;
                shutdown(client, SHUT_WR);
            }
        }
        if ((descriptors[0].revents & (POLLERR | POLLNVAL)) != 0
                || (descriptors[1].revents & (POLLERR | POLLNVAL)) != 0) {
            break;
        }
    }
    close(target);
    close(client);
}

void Serve(int listener, const std::string& target_name, uid_t allowed_uid) {
    while (!g_stop.load(std::memory_order_relaxed)) {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!AllowedPeer(client, allowed_uid)) {
            close(client);
            continue;
        }
        const int target = Connect(target_name);
        if (target < 0) {
            close(client);
            continue;
        }
        Relay(client, target);
    }
}

bool DropPrivileges() {
    constexpr uid_t kSystemUid = 1000;
    constexpr gid_t kSystemGid = 1000;
    if (getuid() != 0 || setgroups(0, nullptr) != 0
            || setgid(kSystemGid) != 0 || setuid(kSystemUid) != 0
            || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return false;
    }
    return getuid() == kSystemUid && geteuid() == kSystemUid;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        return 64;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    const int control = CreateListener(options.public_control);
    const int frames = CreateListener(options.public_frames);
    if (control < 0 || frames < 0) {
        if (control >= 0) close(control);
        if (frames >= 0) close(frames);
        return 1;
    }
    g_listeners[0].store(control, std::memory_order_relaxed);
    g_listeners[1].store(frames, std::memory_order_relaxed);
    if (options.drop_to_system && !DropPrivileges()) {
        close(frames);
        close(control);
        return 1;
    }
    std::thread frame_thread(Serve, frames, options.private_frames, options.allowed_uid);
    Serve(control, options.private_control, options.allowed_uid);
    g_stop.store(true, std::memory_order_relaxed);
    const int frame_listener = g_listeners[1].exchange(-1, std::memory_order_relaxed);
    if (frame_listener >= 0) close(frame_listener);
    if (frame_thread.joinable()) frame_thread.join();
    const int control_listener = g_listeners[0].exchange(-1, std::memory_order_relaxed);
    if (control_listener >= 0) close(control_listener);
    return 0;
}
