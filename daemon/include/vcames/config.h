#pragma once

#include <string>

namespace vcames {

struct Config {
    std::string url = "http://192.168.1.10:8888/live.mjpg";
    std::string target = "front";
    int width = 1280;
    int height = 720;
    int fps = 30;
    int rotation = 0;
    bool mirror = false;

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
