#include "vcames/config.h"
#include "vcames/engine.h"
#include "vcames/image_transform.h"
#include "vcames/replacement_adapter.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <grp.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {

std::string g_control_socket_name = "vcamesd";
std::string g_frame_socket_name = "vcamesd_frames";
constexpr size_t kMaxCommandBytes = 16 * 1024;
constexpr uint32_t kMaxFrameBytes = 16 * 1024 * 1024;
std::atomic<bool> g_stop{false};
std::atomic<int> g_control_listener{-1};
std::atomic<int> g_frame_listener{-1};
std::atomic<uid_t> g_extra_allowed_uid{static_cast<uid_t>(-1)};
bool g_drop_to_system = false;

void Log(const std::string& message) {
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_INFO, "vcamesd", message.c_str());
#else
    std::cerr << "vcamesd: " << message << '\n';
#endif
}

void SignalHandler(int) {
    g_stop.store(true, std::memory_order_relaxed);
    const int control = g_control_listener.exchange(-1, std::memory_order_relaxed);
    const int frames = g_frame_listener.exchange(-1, std::memory_order_relaxed);
    if (control >= 0) {
        close(control);
    }
    if (frames >= 0) {
        close(frames);
    }
}

int CreateAbstractListener(const char* name, std::string* error) {
    const size_t name_length = std::strlen(name);
    sockaddr_un address{};
    if (name_length + 1 >= sizeof(address.sun_path)) {
        if (error != nullptr) {
            *error = "local socket name is too long";
        }
        return -1;
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error != nullptr) {
            *error = std::string("socket failed: ") + std::strerror(errno);
        }
        return -1;
    }
    address.sun_family = AF_UNIX;
    address.sun_path[0] = '\0';
    std::memcpy(address.sun_path + 1, name, name_length);
    const socklen_t address_length = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + 1 + name_length);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), address_length) != 0
            || listen(fd, 4) != 0) {
        if (error != nullptr) {
            *error = std::string("bind/listen failed for ") + name + ": " + std::strerror(errno);
        }
        close(fd);
        return -1;
    }
    return fd;
}

bool IsAllowedPeer(int fd) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0) {
        return false;
    }
#ifdef __ANDROID__
    return credentials.uid == 0 || credentials.uid == 1000
            || credentials.uid == g_extra_allowed_uid.load(std::memory_order_relaxed);
#else
    return credentials.uid == getuid()
            || credentials.uid == g_extra_allowed_uid.load(std::memory_order_relaxed);
#endif
}

bool ParseArguments(int argc, char** argv, std::string* error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--drop-to-system") {
            g_drop_to_system = true;
            continue;
        }
        if ((argument == "--control-socket" || argument == "--frame-socket")
                && index + 1 < argc) {
            const std::string_view socket_name(argv[++index]);
            if (socket_name.empty()
                    || socket_name.size()
                            >= sizeof(((sockaddr_un*)nullptr)->sun_path) - 1
                    || socket_name.find_first_not_of(
                            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")
                            != std::string_view::npos) {
                if (error != nullptr) {
                    *error = "local socket names must use safe ASCII characters";
                }
                return false;
            }
            (argument == "--control-socket" ? g_control_socket_name
                                             : g_frame_socket_name) = socket_name;
            continue;
        }
        if (argument != "--allowed-uid" || index + 1 >= argc) {
            if (error != nullptr) {
                *error = "usage: vcamesd [--allowed-uid APP_UID] [--drop-to-system] "
                        "[--control-socket NAME] [--frame-socket NAME]";
            }
            return false;
        }
        const std::string_view text(argv[++index]);
        unsigned long parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (text.empty() || result.ec != std::errc()
                || result.ptr != text.data() + text.size()
                || parsed < 10000 || parsed > 2'000'000) {
            if (error != nullptr) {
                *error = "allowed app UID must be an Android application UID";
            }
            return false;
        }
        g_extra_allowed_uid.store(static_cast<uid_t>(parsed), std::memory_order_relaxed);
    }
    return true;
}

bool DropRootPrivileges(std::string* error) {
    if (!g_drop_to_system) {
        return true;
    }
    constexpr uid_t kSystemUid = 1000;
    constexpr gid_t kSystemGid = 1000;
    const gid_t groups[] = {1003, 3003};  // AID_CAMERA, AID_INET.
    if (getuid() != 0) {
        if (error != nullptr) {
            *error = "--drop-to-system requires vcamesd to start as uid 0";
        }
        return false;
    }
    if (setgroups(sizeof(groups) / sizeof(groups[0]), groups) != 0
            || setgid(kSystemGid) != 0
            || setuid(kSystemUid) != 0
            || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0
            || getuid() != kSystemUid || geteuid() != kSystemUid) {
        if (error != nullptr) {
            *error = std::string("unable to drop daemon privileges: ")
                    + std::strerror(errno);
        }
        return false;
    }
    return true;
}

bool ReadExact(int fd, void* output, size_t size) {
    auto* bytes = static_cast<uint8_t*>(output);
    size_t received = 0;
    while (received < size && !g_stop.load(std::memory_order_relaxed)) {
        const ssize_t count = read(fd, bytes + received, size - received);
        if (count > 0) {
            received += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // SO_RCVTIMEO expired. Abandon an incomplete frame so a stalled
            // producer cannot monopolize the single frame endpoint forever.
            return false;
        }
        return false;
    }
    return received == size;
}

void WriteAll(int fd, const std::string& response) {
    size_t written = 0;
    while (written < response.size()) {
        const ssize_t count = write(fd, response.data() + written, response.size() - written);
        if (count > 0) {
            written += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

void HandleControlClient(int fd, vcames::Engine* engine) {
    if (!IsAllowedPeer(fd)) {
        WriteAll(fd, "{\"ok\":false,\"error\":\"permission denied\"}\n");
        return;
    }
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string request;
    std::array<char, 2048> buffer{};
    for (;;) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            request.append(buffer.data(), static_cast<size_t>(count));
            if (request.size() > kMaxCommandBytes) {
                WriteAll(fd, "{\"ok\":false,\"error\":\"command too large\"}\n");
                return;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    vcames::Command command;
    std::string error;
    if (!vcames::ParseCommand(request, &command, &error)) {
        WriteAll(fd, "{\"ok\":false,\"error\":\"" + vcames::JsonEscape(error) + "\"}\n");
        return;
    }
    switch (command.type) {
        case vcames::CommandType::kStart:
            // Stop the previous health monitor before deactivating its
            // adapter, otherwise a concurrent reconnect can race a reconfigure.
            engine->Stop();
            vcames::DeactivateReplacementAdapter();
            if (!engine->Start(command.config, &error)) {
                WriteAll(fd, "{\"ok\":false,\"error\":\""
                        + vcames::JsonEscape(error) + "\"}\n");
                return;
            }
            if (command.config.target != "external") {
                const int frame_bus_fd = engine->DuplicateFrameBusFd(&error);
                if (frame_bus_fd < 0 || !vcames::ActivateReplacementAdapter(
                            command.config,
                            frame_bus_fd,
                            engine->FrameBusDescriptor(),
                            &error)) {
                    if (frame_bus_fd >= 0) {
                        close(frame_bus_fd);
                    }
                    engine->Stop();
                    WriteAll(fd, "{\"ok\":false,\"error\":\""
                            + vcames::JsonEscape(error) + "\"}\n");
                    return;
                }
                close(frame_bus_fd);
                engine->SetReplacementAttached(true);
            }
            WriteAll(fd, engine->StatusJson() + "\n");
            return;
        case vcames::CommandType::kStop:
            engine->Stop();
            vcames::DeactivateReplacementAdapter();
            WriteAll(fd, engine->StatusJson() + "\n");
            return;
        case vcames::CommandType::kStatus:
            WriteAll(fd, engine->StatusJson() + "\n");
            return;
        default:
            WriteAll(fd, "{\"ok\":false,\"error\":\"invalid command\"}\n");
            return;
    }
}

void ControlServer(int listener, vcames::Engine* engine) {
    while (!g_stop.load(std::memory_order_relaxed)) {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!g_stop.load(std::memory_order_relaxed)) {
                Log(std::string("control accept failed: ") + std::strerror(errno));
            }
            break;
        }
        HandleControlClient(client, engine);
        close(client);
    }
}

void HandleFrameClient(int fd, vcames::Engine* engine) {
    if (!IsAllowedPeer(fd)) {
        return;
    }
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::array<char, 4> magic{};
    if (!ReadExact(fd, magic.data(), magic.size())) {
        return;
    }
    const bool legacy_jpeg = magic == std::array<char, 4>{'V', 'C', 'F', '1'};
    const bool typed_frames = magic == std::array<char, 4>{'V', 'C', 'F', '2'};
    if (!legacy_jpeg && !typed_frames) {
        return;
    }
    while (!g_stop.load(std::memory_order_relaxed)) {
        uint32_t format = 1;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t y_stride = 0;
        uint32_t uv_stride = 0;
        int64_t presentation_time_ns = 0;
        if (typed_frames) {
            std::array<uint32_t, 6> fields{};
            if (!ReadExact(fd, fields.data(), sizeof(fields))) {
                return;
            }
            format = ntohl(fields[0]);
            width = ntohl(fields[1]);
            height = ntohl(fields[2]);
            y_stride = ntohl(fields[3]);
            uv_stride = ntohl(fields[4]);
            fields[5] = ntohl(fields[5]);
            std::array<uint8_t, 8> timestamp{};
            if (!ReadExact(fd, timestamp.data(), timestamp.size())) {
                return;
            }
            uint64_t parsed_timestamp = 0;
            for (uint8_t byte : timestamp) {
                parsed_timestamp = (parsed_timestamp << 8) | byte;
            }
            presentation_time_ns = static_cast<int64_t>(parsed_timestamp);
            if (format != 2 || width == 0 || height == 0
                    || y_stride != width || uv_stride != width) {
                Log("local producer sent unsupported typed frame metadata");
                return;
            }
            const uint32_t frame_size = fields[5];
            if (frame_size < 4 || frame_size > kMaxFrameBytes) {
                Log("local producer sent an invalid NV21 frame size");
                return;
            }
            std::vector<uint8_t> frame(frame_size);
            if (!ReadExact(fd, frame.data(), frame.size())) {
                return;
            }
            std::string error;
            if (!engine->PushNv21Frame(
                        std::move(frame),
                        static_cast<int>(width),
                        static_cast<int>(height),
                        presentation_time_ns,
                        &error)) {
                Log("local NV21 frame rejected: " + error);
                return;
            }
            continue;
        }
        uint32_t network_size = 0;
        if (!ReadExact(fd, &network_size, sizeof(network_size))) {
            return;
        }
        const uint32_t frame_size = ntohl(network_size);
        if (frame_size < 4 || frame_size > kMaxFrameBytes) {
            Log("local producer sent an invalid frame size");
            return;
        }
        std::vector<uint8_t> frame(frame_size);
        if (!ReadExact(fd, frame.data(), frame.size())) {
            return;
        }
        std::string error;
        if (!engine->PushFrame(std::move(frame), &error)) {
            Log("local frame rejected: " + error);
            return;
        }
    }
}

void FrameServer(int listener, vcames::Engine* engine) {
    while (!g_stop.load(std::memory_order_relaxed)) {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!g_stop.load(std::memory_order_relaxed)) {
                Log(std::string("frame accept failed: ") + std::strerror(errno));
            }
            break;
        }
        HandleFrameClient(client, engine);
        close(client);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string error;
    if (!ParseArguments(argc, argv, &error)) {
        Log(error);
        return 64;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    const int control_listener = CreateAbstractListener(g_control_socket_name.c_str(), &error);
    if (control_listener < 0) {
        Log(error);
        return 1;
    }
    const int frame_listener = CreateAbstractListener(g_frame_socket_name.c_str(), &error);
    if (frame_listener < 0) {
        Log(error);
        close(control_listener);
        return 1;
    }
    g_control_listener.store(control_listener, std::memory_order_relaxed);
    g_frame_listener.store(frame_listener, std::memory_order_relaxed);
    if (!DropRootPrivileges(&error)) {
        Log(error);
        close(frame_listener);
        close(control_listener);
        return 1;
    }
    if (g_drop_to_system) {
        Log("dropped root privileges to Android system uid with camera/inet groups");
    }

    vcames::Engine engine;
    // Configure the loopback device before the external Camera Provider scans
    // /dev/video*. Holding a neutral frame also gives camera clients a defined
    // image until the controller supplies a real source.
    vcames::Config standby_config;
    standby_config.url = "push://local";
    std::vector<uint8_t> standby_frame;
    if (!engine.Start(standby_config, &error)
            || !vcames::CreateSolidJpeg(
                    standby_config.width,
                    standby_config.height,
                    16,
                    16,
                    16,
                    standby_config.jpeg_quality,
                    &standby_frame,
                    &error)
            || !engine.PushFrame(std::move(standby_frame), &error)) {
        Log("standby initialization failed: " + error);
    }
    Log("ready");
    std::thread frame_server(FrameServer, frame_listener, &engine);
    ControlServer(control_listener, &engine);
    g_stop.store(true, std::memory_order_relaxed);
    const int frames = g_frame_listener.exchange(-1, std::memory_order_relaxed);
    if (frames >= 0) {
        close(frames);
    }
    if (frame_server.joinable()) {
        frame_server.join();
    }
    engine.Stop();
    vcames::DeactivateReplacementAdapter();
    const int control = g_control_listener.exchange(-1, std::memory_order_relaxed);
    if (control >= 0) {
        close(control);
    }
    Log("stopped");
    return 0;
}
