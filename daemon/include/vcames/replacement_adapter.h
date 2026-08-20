#pragma once

#include "vcames/config.h"

#include <string>

namespace vcames {

// Front/back replacement is supplied by a device-build-specific Camera HAL
// adapter. The generic daemon only activates it through this narrow protocol;
// it never guesses private cameraserver symbols or disables SELinux.
bool ActivateReplacementAdapter(const Config& config, std::string* error);
void DeactivateReplacementAdapter();

}  // namespace vcames
