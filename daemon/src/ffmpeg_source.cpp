#include "vcames/ffmpeg_source.h"

#include "vcames/mjpeg_parser.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace vcames {
namespace {

constexpr size_t kReadBufferBytes = 64 * 1024;
constexpr size_t kMaxDiagnosticBytes = 8 * 1024;
constexpr auto kInitialFrameTimeout = std::chrono::seconds(20);
constexpr auto kConnectedFrameTimeout = std::chrono::seconds(15);

class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { Reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.Release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }
    int get() const { return value_; }
    int Release() {
        const int value = value_;
        value_ = -1;
        return value;
    }
    void Reset(int value = -1) {
        if (value_ >= 0) {
            close(value_);
        }
        value_ = value;
    }

private:
    int value_ = -1;
};

void TerminateChild(pid_t child) {
    if (child <= 0) {
        return;
    }
    // FFmpeg is placed in its own process group. Terminating the group also
    // covers protocol helpers if a future exact build enables any.
    kill(-child, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(-child, SIGKILL);
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
}

bool SetNonBlocking(int descriptor, std::string* error) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0) {
        return true;
    }
    if (error != nullptr) {
        *error = std::string("unable to configure FFmpeg pipe: ") + std::strerror(errno);
    }
    return false;
}

void DrainDiagnostic(int descriptor, std::string* diagnostic) {
    std::array<char, 2048> buffer{};
    for (;;) {
        const ssize_t count = read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            const size_t available = kMaxDiagnosticBytes > diagnostic->size()
                    ? kMaxDiagnosticBytes - diagnostic->size() : 0;
            diagnostic->append(buffer.data(), std::min(available, static_cast<size_t>(count)));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

}  // namespace

bool FfmpegSource::Stream(
        const std::string& ffmpeg_path,
        const std::string& url,
        int fps,
        const StreamSourceSpec& spec,
        const std::atomic<bool>& stop_requested,
        FrameCallback frame_callback,
        ConnectedCallback connected_callback,
        std::string* error) const {
    if (ffmpeg_path.empty() || ffmpeg_path.front() != '/' || access(ffmpeg_path.c_str(), X_OK) != 0) {
        if (error != nullptr) {
            *error = "configured FFmpeg executable is missing or not executable";
        }
        return false;
    }
    std::vector<std::string> argument_storage =
            BuildFfmpegArguments(ffmpeg_path, url, fps, spec);
    std::vector<char*> arguments;
    arguments.reserve(argument_storage.size() + 1);
    for (std::string& argument : argument_storage) {
        arguments.push_back(argument.data());
    }
    arguments.push_back(nullptr);
    std::vector<std::string> environment_storage = {
        "ANDROID_DATA=/data",
        "ANDROID_ROOT=/system",
        "AV_LOG_FORCE_NOCOLOR=1",
        "HOME=/data/adb/vcames",
        "LC_ALL=C",
        "PATH=/system/bin:/system/xbin",
        "TMPDIR=/data/adb/vcames",
    };
    std::vector<char*> child_environment;
    child_environment.reserve(environment_storage.size() + 1);
    for (std::string& value : environment_storage) {
        child_environment.push_back(value.data());
    }
    child_environment.push_back(nullptr);

    int output_pipe[2] = {-1, -1};
    int diagnostic_pipe[2] = {-1, -1};
    if (pipe2(output_pipe, O_CLOEXEC) != 0 || pipe2(diagnostic_pipe, O_CLOEXEC) != 0) {
        const int saved_errno = errno;
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        if (diagnostic_pipe[0] >= 0) close(diagnostic_pipe[0]);
        if (diagnostic_pipe[1] >= 0) close(diagnostic_pipe[1]);
        if (error != nullptr) {
            *error = std::string("unable to create FFmpeg pipes: ") + std::strerror(saved_errno);
        }
        return false;
    }
    FileDescriptor output_read(output_pipe[0]);
    FileDescriptor output_write(output_pipe[1]);
    FileDescriptor diagnostic_read(diagnostic_pipe[0]);
    FileDescriptor diagnostic_write(diagnostic_pipe[1]);

    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    const bool actions_initialized = spawn_error == 0;
    if (spawn_error == 0) spawn_error = posix_spawn_file_actions_adddup2(
            &actions, output_write.get(), STDOUT_FILENO);
    if (spawn_error == 0) spawn_error = posix_spawn_file_actions_adddup2(
            &actions, diagnostic_write.get(), STDERR_FILENO);
    if (spawn_error == 0) spawn_error = posix_spawn_file_actions_addclose(
            &actions, output_read.get());
    if (spawn_error == 0) spawn_error = posix_spawn_file_actions_addclose(
            &actions, diagnostic_read.get());
    posix_spawnattr_t attributes;
    int attribute_error = posix_spawnattr_init(&attributes);
    const bool attributes_initialized = attribute_error == 0;
    sigset_t default_signals;
    sigset_t signal_mask;
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGINT);
    sigaddset(&default_signals, SIGPIPE);
    sigaddset(&default_signals, SIGTERM);
    sigemptyset(&signal_mask);
    if (attribute_error == 0) {
        attribute_error = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (attribute_error == 0) {
        attribute_error = posix_spawnattr_setsigdefault(&attributes, &default_signals);
    }
    if (attribute_error == 0) {
        attribute_error = posix_spawnattr_setsigmask(&attributes, &signal_mask);
    }
    if (attribute_error == 0) {
        attribute_error = posix_spawnattr_setflags(
                &attributes,
                POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF
                        | POSIX_SPAWN_SETSIGMASK);
    }
    if (spawn_error == 0 && attribute_error != 0) {
        spawn_error = attribute_error;
    }
    pid_t child = -1;
    if (spawn_error == 0) {
        spawn_error = posix_spawn(&child, ffmpeg_path.c_str(), &actions, &attributes,
                                  arguments.data(), child_environment.data());
    }
    if (actions_initialized) {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (attributes_initialized) {
        posix_spawnattr_destroy(&attributes);
    }
    if (spawn_error != 0) {
        if (error != nullptr) {
            *error = std::string("unable to start FFmpeg: ") + std::strerror(spawn_error);
        }
        return false;
    }
    output_write.Reset();
    diagnostic_write.Reset();
    if (!SetNonBlocking(output_read.get(), error)
            || !SetNonBlocking(diagnostic_read.get(), error)) {
        TerminateChild(child);
        return false;
    }

    bool connected = false;
    bool parser_failed = false;
    std::string parser_error;
    const auto started = std::chrono::steady_clock::now();
    auto last_frame = started;
    MjpegParser parser([&](std::vector<uint8_t>&& frame) {
        last_frame = std::chrono::steady_clock::now();
        if (!connected) {
            connected = true;
            connected_callback();
        }
        frame_callback(std::move(frame));
    });
    std::array<uint8_t, kReadBufferBytes> buffer{};
    std::string diagnostic;
    bool child_exited = false;
    int child_status = 0;

    while (!stop_requested.load(std::memory_order_relaxed) && !parser_failed) {
        pollfd descriptors[2] = {
            {output_read.get(), POLLIN | POLLHUP | POLLERR, 0},
            {diagnostic_read.get(), POLLIN | POLLHUP | POLLERR, 0},
        };
        const int poll_result = poll(descriptors, 2, 250);
        if (poll_result < 0 && errno != EINTR) {
            parser_error = std::string("FFmpeg pipe polling failed: ") + std::strerror(errno);
            parser_failed = true;
            break;
        }
        if ((descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            DrainDiagnostic(diagnostic_read.get(), &diagnostic);
        }
        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            for (;;) {
                const ssize_t count = read(output_read.get(), buffer.data(), buffer.size());
                if (count > 0) {
                    if (!parser.Append(buffer.data(), static_cast<size_t>(count), &parser_error)) {
                        parser_failed = true;
                        break;
                    }
                    continue;
                }
                if (count < 0 && errno == EINTR) continue;
                break;
            }
        }
        const pid_t wait_result = waitpid(child, &child_status, WNOHANG);
        if (wait_result == child || (wait_result < 0 && errno == ECHILD)) {
            child_exited = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if ((!connected && now - started > kInitialFrameTimeout)
                || (connected && now - last_frame > kConnectedFrameTimeout)) {
            parser_error = connected
                    ? "FFmpeg stream produced no frame for 15 seconds"
                    : "FFmpeg stream produced no first frame within 20 seconds";
            parser_failed = true;
        }
    }

    DrainDiagnostic(diagnostic_read.get(), &diagnostic);
    if (!child_exited) {
        TerminateChild(child);
    }
    if (stop_requested.load(std::memory_order_relaxed)) {
        return true;
    }
    if (error != nullptr) {
        if (parser_failed && !parser_error.empty()) {
            *error = std::move(parser_error);
        } else if (child_exited && WIFEXITED(child_status)) {
            *error = "FFmpeg source ended with exit code "
                    + std::to_string(WEXITSTATUS(child_status));
        } else if (child_exited && WIFSIGNALED(child_status)) {
            *error = "FFmpeg source stopped by signal "
                    + std::to_string(WTERMSIG(child_status));
        } else {
            *error = diagnostic.empty()
                    ? "FFmpeg source ended before stop was requested"
                    : "FFmpeg source failed; diagnostic output was suppressed to protect stream credentials";
        }
    }
    return false;
}

}  // namespace vcames
