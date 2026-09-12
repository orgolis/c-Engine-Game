#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace schizo::editor::secure_credentials {

// Credentials are stored only in the current user's OS credential vault:
// Windows Credential Manager, macOS Keychain, or Linux Secret Service.
// There is deliberately no plaintext-file fallback.
bool Available();
bool StoreAnthropicApiKey(std::string_view secret, std::string& error);
bool HasAnthropicApiKey();
std::optional<std::string> ReadAnthropicApiKey();
bool RemoveAnthropicApiKey(std::string& error);

// Best-effort explicit memory clearing for short-lived credential buffers.
void Erase(std::string& secret);
void Erase(char* secret, size_t size);

}  // namespace schizo::editor::secure_credentials
