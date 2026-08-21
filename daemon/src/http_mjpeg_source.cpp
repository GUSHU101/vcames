#include "vcames/http_mjpeg_source.h"

#include "vcames/mjpeg_parser.h"
#include "vcames/network_policy.h"

#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vcames {
namespace {

constexpr size_t kMaxHeaderBytes = 64 * 1024;
constexpr int kIoTimeoutSeconds = 2;

struct HttpUrl {
    std::string host;
    std::string port = "80";
    std::string target = "/";

    bool Parse(const std::string& url, std::string* error) {
        constexpr std::string_view prefix = "http://";
        if (!url.starts_with(prefix)) {
            if (error != nullptr) {
                *error = "only http:// MJPEG sources are supported";
            }
            return false;
        }
        std::string_view remainder(url.data() + prefix.size(), url.size() - prefix.size());
        const size_t slash = remainder.find('/');
        std::string_view authority = slash == std::string_view::npos
                ? remainder
                : remainder.substr(0, slash);
        target = slash == std::string_view::npos ? "/" : std::string(remainder.substr(slash));
        if (authority.empty()) {
            if (error != nullptr) {
                *error = "HTTP URL has no host";
            }
            return false;
        }
        if (authority.find('@') != std::string_view::npos
                || authority.find_first_of("\r\n\t ") != std::string_view::npos) {
            if (error != nullptr) {
                *error = "HTTP credentials and whitespace are not allowed in the source URL";
            }
            return false;
        }

        if (authority.front() == '[') {
            const size_t closing = authority.find(']');
            if (closing == std::string_view::npos) {
                if (error != nullptr) {
                    *error = "invalid bracketed IPv6 host";
                }
                return false;
            }
            host = std::string(authority.substr(1, closing - 1));
            if (closing + 1 < authority.size()) {
                if (authority[closing + 1] != ':') {
                    if (error != nullptr) {
                        *error = "invalid IPv6 authority";
                    }
                    return false;
                }
                port = std::string(authority.substr(closing + 2));
            }
        } else {
            const size_t colon = authority.rfind(':');
            if (colon != std::string_view::npos && authority.find(':') == colon) {
                host = std::string(authority.substr(0, colon));
                port = std::string(authority.substr(colon + 1));
            } else {
                host = std::string(authority);
            }
        }
        if (host.empty() || port.empty() || target.find_first_of("\r\n") != std::string::npos) {
            if (error != nullptr) {
                *error = "invalid HTTP URL";
            }
            return false;
        }
        int port_number = 0;
        const auto parsed = std::from_chars(port.data(), port.data() + port.size(), port_number);
        if (parsed.ec != std::errc() || parsed.ptr != port.data() + port.size()
                || port_number < 1 || port_number > 65535) {
            if (error != nullptr) {
                *error = "invalid HTTP port";
            }
            return false;
        }
        return true;
    }

    std::string HostHeader() const {
        const bool ipv6 = host.find(':') != std::string::npos;
        const std::string displayed_host = ipv6 ? "[" + host + "]" : host;
        return port == "80" ? displayed_host : displayed_host + ":" + port;
    }
};

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            close(value_);
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return value_; }
    int release() {
        const int value = value_;
        value_ = -1;
        return value;
    }
    void reset(int value = -1) {
        if (value_ >= 0) {
            close(value_);
        }
        value_ = value;
    }

private:
    int value_;
};

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool WaitForSocket(int fd, short events, int timeout_ms, std::string* error) {
    pollfd descriptor{fd, events, 0};
    int result;
    do {
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result > 0 && (descriptor.revents & events) != 0) {
        return true;
    }
    if (error != nullptr) {
        if (result == 0) {
            *error = "network operation timed out";
        } else {
            *error = std::string("network poll failed: ") + std::strerror(errno);
        }
    }
    return false;
}

bool Connect(const HttpUrl& url, FileDescriptor* connected, std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const int lookup = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        if (error != nullptr) {
            *error = std::string("DNS lookup failed: ") + gai_strerror(lookup);
        }
        return false;
    }

    std::string last_error = "no usable address";
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        bool private_address = false;
        if (address->ai_family == AF_INET
                && address->ai_addrlen >= sizeof(sockaddr_in)) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
            private_address = IsPrivateNetworkAddress(
                    IpAddressFamily::kIpv4,
                    reinterpret_cast<const uint8_t*>(&ipv4->sin_addr),
                    sizeof(ipv4->sin_addr));
        } else if (address->ai_family == AF_INET6
                && address->ai_addrlen >= sizeof(sockaddr_in6)) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address->ai_addr);
            private_address = IsPrivateNetworkAddress(
                    IpAddressFamily::kIpv6,
                    reinterpret_cast<const uint8_t*>(&ipv6->sin6_addr),
                    sizeof(ipv6->sin6_addr));
        }
        if (!private_address) {
            last_error = "cleartext MJPEG is restricted to private or local addresses";
            continue;
        }
        FileDescriptor candidate(socket(
                address->ai_family,
                address->ai_socktype | SOCK_CLOEXEC,
                address->ai_protocol));
        if (candidate.get() < 0) {
            last_error = std::strerror(errno);
            continue;
        }
        const int old_flags = fcntl(candidate.get(), F_GETFL, 0);
        if (old_flags < 0 || fcntl(candidate.get(), F_SETFL, old_flags | O_NONBLOCK) != 0) {
            last_error = std::strerror(errno);
            continue;
        }
        int result = connect(candidate.get(), address->ai_addr, address->ai_addrlen);
        if (result != 0 && errno == EINPROGRESS) {
            if (!WaitForSocket(candidate.get(), POLLOUT, 5000, &last_error)) {
                continue;
            }
            int socket_error = 0;
            socklen_t socket_error_size = sizeof(socket_error);
            if (getsockopt(candidate.get(), SOL_SOCKET, SO_ERROR, &socket_error,
                           &socket_error_size) != 0 || socket_error != 0) {
                last_error = std::strerror(socket_error == 0 ? errno : socket_error);
                continue;
            }
        } else if (result != 0) {
            last_error = std::strerror(errno);
            continue;
        }
        if (fcntl(candidate.get(), F_SETFL, old_flags) != 0) {
            last_error = std::strerror(errno);
            continue;
        }
        timeval timeout{kIoTimeoutSeconds, 0};
        setsockopt(candidate.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(candidate.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        int no_delay = 1;
        setsockopt(candidate.get(), IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
        connected->reset(candidate.release());
        freeaddrinfo(addresses);
        return true;
    }
    freeaddrinfo(addresses);
    if (error != nullptr) {
        *error = "unable to connect: " + last_error;
    }
    return false;
}

bool SendAll(int fd, const std::string& request, std::string* error) {
    size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(
                fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count > 0) {
            sent += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (error != nullptr) {
            *error = std::string("HTTP request send failed: ") + std::strerror(errno);
        }
        return false;
    }
    return true;
}

class ChunkedDecoder {
public:
    using BodyCallback = std::function<bool(const uint8_t*, size_t, std::string*)>;

    explicit ChunkedDecoder(BodyCallback callback) : callback_(std::move(callback)) {}

    bool Append(const uint8_t* data, size_t size, std::string* error) {
        if (complete_) {
            return true;
        }
        if (size == 0) {
            return true;
        }
        if (data == nullptr) {
            if (error != nullptr) {
                *error = "HTTP chunk input pointer is null";
            }
            return false;
        }
        buffer_.insert(buffer_.end(), data, data + size);
        for (;;) {
            if (reading_size_) {
                static const std::array<uint8_t, 2> delimiter{'\r', '\n'};
                const auto end = std::search(
                        buffer_.begin(), buffer_.end(), delimiter.begin(), delimiter.end());
                if (end == buffer_.end()) {
                    if (buffer_.size() > 128) {
                        if (error != nullptr) {
                            *error = "HTTP chunk size line is too long";
                        }
                        return false;
                    }
                    return true;
                }
                std::string line(buffer_.begin(), end);
                const size_t extension = line.find(';');
                if (extension != std::string::npos) {
                    line.resize(extension);
                }
                unsigned long long parsed_size = 0;
                const auto parsed = std::from_chars(
                        line.data(), line.data() + line.size(), parsed_size, 16);
                if (line.empty() || parsed.ec != std::errc()
                        || parsed.ptr != line.data() + line.size()
                        || parsed_size > 32ULL * 1024ULL * 1024ULL) {
                    if (error != nullptr) {
                        *error = "invalid HTTP chunk size";
                    }
                    return false;
                }
                buffer_.erase(buffer_.begin(), end + 2);
                remaining_ = static_cast<size_t>(parsed_size);
                reading_size_ = false;
                if (remaining_ == 0) {
                    complete_ = true;
                    return true;
                }
            }

            if (remaining_ > 0) {
                if (buffer_.empty()) {
                    return true;
                }
                const size_t available = std::min(remaining_, buffer_.size());
                if (!callback_(buffer_.data(), available, error)) {
                    return false;
                }
                buffer_.erase(buffer_.begin(), buffer_.begin() + available);
                remaining_ -= available;
                if (remaining_ > 0) {
                    return true;
                }
            }

            if (buffer_.size() < 2) {
                return true;
            }
            if (buffer_[0] != '\r' || buffer_[1] != '\n') {
                if (error != nullptr) {
                    *error = "HTTP chunk data is missing its terminator";
                }
                return false;
            }
            buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
            reading_size_ = true;
        }
    }

private:
    BodyCallback callback_;
    std::vector<uint8_t> buffer_;
    size_t remaining_ = 0;
    bool reading_size_ = true;
    bool complete_ = false;
};

bool ParseHeaders(
        const std::string& header_text,
        int* status_code,
        std::unordered_map<std::string, std::string>* headers,
        std::string* error) {
    std::istringstream stream(header_text);
    std::string line;
    if (!std::getline(stream, line)) {
        if (error != nullptr) {
            *error = "HTTP response has no status line";
        }
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream status_line(line);
    std::string version;
    if (!(status_line >> version >> *status_code) || !version.starts_with("HTTP/")) {
        if (error != nullptr) {
            *error = "invalid HTTP status line";
        }
        return false;
    }
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        std::string key = Lower(line.substr(0, separator));
        size_t value_start = separator + 1;
        while (value_start < line.size()
                && std::isspace(static_cast<unsigned char>(line[value_start]))) {
            ++value_start;
        }
        (*headers)[std::move(key)] = line.substr(value_start);
    }
    return true;
}

}  // namespace

bool HttpMjpegSource::Stream(
        const std::string& url_text,
        const std::atomic<bool>& stop_requested,
        FrameCallback frame_callback,
        ConnectedCallback connected_callback,
        std::string* error) const {
    HttpUrl url;
    if (!url.Parse(url_text, error)) {
        return false;
    }
    FileDescriptor socket_fd;
    if (!Connect(url, &socket_fd, error)) {
        return false;
    }

    const std::string request =
            "GET " + url.target + " HTTP/1.1\r\n"
            "Host: " + url.HostHeader() + "\r\n"
            "User-Agent: VCamES/1.0\r\n"
            "Accept: multipart/x-mixed-replace,image/jpeg,*/*\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n\r\n";
    if (!SendAll(socket_fd.get(), request, error)) {
        return false;
    }

    std::vector<uint8_t> pending;
    pending.reserve(64 * 1024);
    std::array<uint8_t, 64 * 1024> receive_buffer{};
    static const std::array<uint8_t, 4> header_end{'\r', '\n', '\r', '\n'};
    size_t body_offset = 0;
    for (;;) {
        const ssize_t count = recv(socket_fd.get(), receive_buffer.data(), receive_buffer.size(), 0);
        if (count > 0) {
            pending.insert(pending.end(), receive_buffer.begin(), receive_buffer.begin() + count);
            const auto delimiter = std::search(
                    pending.begin(), pending.end(), header_end.begin(), header_end.end());
            if (delimiter != pending.end()) {
                body_offset = static_cast<size_t>(delimiter - pending.begin()) + header_end.size();
                break;
            }
            if (pending.size() > kMaxHeaderBytes) {
                if (error != nullptr) {
                    *error = "HTTP response headers exceed safety limit";
                }
                return false;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (error != nullptr) {
            *error = "HTTP server closed before sending complete headers";
        }
        return false;
    }

    const std::string headers_text(pending.begin(), pending.begin() + body_offset);
    int status_code = 0;
    std::unordered_map<std::string, std::string> headers;
    if (!ParseHeaders(headers_text, &status_code, &headers, error)) {
        return false;
    }
    if (status_code != 200) {
        if (error != nullptr) {
            *error = "HTTP source returned status " + std::to_string(status_code);
        }
        return false;
    }

    MjpegParser parser(std::move(frame_callback));
    auto body_callback = [&parser](const uint8_t* data, size_t size, std::string* callback_error) {
        return parser.Append(data, size, callback_error);
    };
    const auto transfer = headers.find("transfer-encoding");
    const bool chunked = transfer != headers.end()
            && Lower(transfer->second).find("chunked") != std::string::npos;
    ChunkedDecoder chunk_decoder(body_callback);
    auto feed = [&](const uint8_t* data, size_t size) {
        return chunked
                ? chunk_decoder.Append(data, size, error)
                : body_callback(data, size, error);
    };

    connected_callback();
    if (body_offset < pending.size()
            && !feed(pending.data() + body_offset, pending.size() - body_offset)) {
        return false;
    }
    pending.clear();

    while (!stop_requested.load(std::memory_order_relaxed)) {
        const ssize_t count = recv(socket_fd.get(), receive_buffer.data(), receive_buffer.size(), 0);
        if (count > 0) {
            if (!feed(receive_buffer.data(), static_cast<size_t>(count))) {
                return false;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (error != nullptr) {
            *error = count == 0
                    ? "HTTP MJPEG stream ended"
                    : std::string("HTTP receive failed: ") + std::strerror(errno);
        }
        return false;
    }
    return true;
}

}  // namespace vcames
