#pragma once

#include <string>

namespace gws::platform {

// Opens the platform-native (or desktop-native) folder picker.
// Returns an empty string when the user cancels or no picker is available.
std::string browse_folder(const char* title);

} // namespace gws::platform
