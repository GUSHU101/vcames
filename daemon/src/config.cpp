#include "vcames/config.h"
#include "vcames/stream_source.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vcames {
namespace {

bool ParseInt(std::string_view text, int* value) {
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseBool(std::string_view text, bool* value) {
    if (text == "1" || text == "true") {
        *value = true;
        return true;
    }
    if (text == "0" || text == "false") {
        *value = false;
        return true;
    }
    return false;
}

bool ContainsControlCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f;
    });
}

bool IsSafeProtocolValue(const std::string& value, size_t maximum_length) {
    return !value.empty() && value.size() <= maximum_length
            && !ContainsControlCharacter(value);
}

}  // namespace

bool Config::Validate(std::string* error) const {
    auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (url != "push://local" && url != "push://placeholder") {
        StreamSourceSpec source;
        if (!ParseStreamSourceUrl(url, &source, error)) {
            return false;
        }
        if (ffmpeg_path.empty() || ffmpeg_path.front() != '/'
                || ffmpeg_path.size() > 512 || ContainsControlCharacter(ffmpeg_path)) {
            return fail("FFmpeg path must be a safe absolute path");
        }
    }
    if (!video_device.starts_with("/dev/video")
            || !IsSafeProtocolValue(video_device, 64)) {
        return fail("video device must be an absolute /dev/video* path");
    }
    if (width != 1280 || height != 720 || fps != 30) {
        return fail("Pixel 5 global Provider output is fixed at 1280x720@30");
    }
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        return fail("rotation must be 0, 90, 180, or 270");
    }
    return true;
}

bool ParseCommand(const std::string& request, Command* command, std::string* error) {
    if (command == nullptr) {
        if (error != nullptr) {
            *error = "missing command output";
        }
        return false;
    }
    if (request.empty() || request.size() > 16 * 1024) {
        if (error != nullptr) {
            *error = "request is empty or too large";
        }
        return false;
    }

    std::istringstream input(request);
    std::string line;
    if (!std::getline(input, line)) {
        if (error != nullptr) {
            *error = "request has no command line";
        }
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    if (line == "STOP") {
        command->type = CommandType::kStop;
        return true;
    }
    if (line == "STATUS") {
        command->type = CommandType::kStatus;
        return true;
    }
    if (line != "START") {
        if (error != nullptr) {
            *error = "unknown command";
        }
        return false;
    }

    Config config;
    std::unordered_map<std::string, std::string> values;
    bool terminated = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == ".") {
            terminated = true;
            break;
        }
        const size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            if (error != nullptr) {
                *error = "invalid START option";
            }
            return false;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (!values.emplace(key, value).second) {
            if (error != nullptr) {
                *error = "duplicate START option: " + key;
            }
            return false;
        }
    }
    if (!terminated) {
        if (error != nullptr) {
            *error = "START request is not terminated";
        }
        return false;
    }

    auto string_option = [&values](const char* key, std::string* target) {
        const auto it = values.find(key);
        if (it != values.end()) {
            *target = it->second;
        }
    };
    string_option("url", &config.url);
    string_option("video_device", &config.video_device);

    auto int_option = [&values, error](const char* key, int* target) {
        const auto it = values.find(key);
        if (it == values.end()) {
            return true;
        }
        if (ParseInt(it->second, target)) {
            return true;
        }
        if (error != nullptr) {
            *error = std::string("invalid integer for ") + key;
        }
        return false;
    };
    if (!int_option("width", &config.width) ||
        !int_option("height", &config.height) ||
        !int_option("fps", &config.fps) ||
        !int_option("rotation", &config.rotation)) {
        return false;
    }

    auto bool_option = [&values, error](const char* key, bool* target) {
        const auto it = values.find(key);
        if (it == values.end()) {
            return true;
        }
        if (ParseBool(it->second, target)) {
            return true;
        }
        if (error != nullptr) {
            *error = std::string("invalid boolean for ") + key;
        }
        return false;
    };
    if (!bool_option("mirror", &config.mirror)) {
        return false;
    }

    static const std::unordered_map<std::string, bool> kKnownOptions = {
        {"url", true}, {"video_device", true},
        {"width", true}, {"height", true},
        {"fps", true}, {"rotation", true}, {"mirror", true},
    };
    for (const auto& [key, unused] : values) {
        (void)unused;
        if (!kKnownOptions.contains(key)) {
            if (error != nullptr) {
                *error = "unknown START option: " + key;
            }
            return false;
        }
    }

    if (!config.Validate(error)) {
        return false;
    }
    command->type = CommandType::kStart;
    command->config = std::move(config);
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    escaped += "\\u00";
                    escaped.push_back(kHex[(c >> 4) & 0x0f]);
                    escaped.push_back(kHex[c & 0x0f]);
                } else {
                    escaped.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return escaped;
}

}  // namespace vcames
