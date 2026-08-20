#pragma once

#include <string>

namespace vcames {

struct Config {
    std::string url = "http://192.168.1.10:8888/live.mjpg";
    std::string device = "/dev/video100";
    int width = 1280;
    int height = 720;
    int fps = 30;
    int rotation = 0;
    bool mirror = false;
    bool hold_last = true;
    int stale_timeout_ms = 3000;
    int jpeg_quality = 90;

    bool Validate(std::string* error) const;
};

enum class CommandType {
    kInvalid,
    kStart,
    kStop,
    kStatus,
};

struct Command {
    CommandType type = CommandType::kInvalid;
    Config config;
};

bool ParseCommand(const std::string& request, Command* command, std::string* error);
std::string JsonEscape(const std::string& value);

}  // namespace vcames
