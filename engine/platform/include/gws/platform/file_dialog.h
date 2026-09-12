#pragma once

#include <string>

namespace gws::platform {

// Opens the platform-native (or desktop-native) folder picker.
// Returns an empty string when the user cancels or no picker is available.
std::string browse_folder(const char* title);

// Opens the native file picker for importing one existing file.
// Returns an empty string when the user cancels or no picker is available.
std::string browse_file(const char* title);

// Opens the native save-file picker and suggests `default_filename`.
std::string save_file(const char* title, const char* default_filename);

} // namespace gws::platform
