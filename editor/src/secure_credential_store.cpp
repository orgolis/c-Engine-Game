#include "secure_credential_store.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <filesystem>
#endif

namespace schizo::editor::secure_credentials {
namespace {

#ifdef _WIN32
constexpr const char* kCredentialTarget =
    "GameWorldshaper/AnthropicApiKey";
#else
constexpr const char* kService = "GameWorldshaper";
constexpr const char* kAccount = "AnthropicApiKey";
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
const char* secret_tool() {
    static const char* candidates[] = {
        "/usr/bin/secret-tool",
        "/usr/local/bin/secret-tool"
    };
    for (const char* candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }
    return nullptr;
}
#endif

}  // namespace

void Erase(std::string& secret) {
    if (!secret.empty()) {
        volatile char* bytes = secret.data();
        for (size_t i = 0; i < secret.size(); ++i) bytes[i] = 0;
    }
    secret.clear();
}

void Erase(char* secret, size_t size) {
    if (!secret) return;
    volatile char* bytes = secret;
    for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

bool Available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return true;
#else
    return secret_tool() != nullptr;
#endif
}

bool StoreAnthropicApiKey(std::string_view secret, std::string& error) {
    if (secret.empty() || secret.size() > 512) {
        error = "Enter a valid Anthropic API key.";
        return false;
    }
#ifdef _WIN32
    CREDENTIALA credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPSTR>(kCredentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPSTR>("GameWorldshaper");
    if (!CredWriteA(&credential, 0)) {
        error = "Windows Credential Manager could not store the key.";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(std::strlen(kService)), kService,
        static_cast<UInt32>(std::strlen(kAccount)), kAccount,
        nullptr, nullptr, &item);
    if (status == errSecSuccess && item) {
        status = SecKeychainItemModifyAttributesAndData(
            item, nullptr, static_cast<UInt32>(secret.size()), secret.data());
        CFRelease(item);
    } else {
        status = SecKeychainAddGenericPassword(
            nullptr,
            static_cast<UInt32>(std::strlen(kService)), kService,
            static_cast<UInt32>(std::strlen(kAccount)), kAccount,
            static_cast<UInt32>(secret.size()), secret.data(), nullptr);
    }
    if (status != errSecSuccess) {
        error = "macOS Keychain could not store the key.";
        return false;
    }
    return true;
#else
    const char* tool = secret_tool();
    if (!tool) {
        error = "Linux Secret Service is unavailable. Install libsecret tools.";
        return false;
    }
    const std::string command =
        std::string(tool) +
        " store --label='WorldShaper Anthropic API key' service " +
        kService + " account " + kAccount;
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        error = "Linux Secret Service could not be opened.";
        return false;
    }
    const size_t written = std::fwrite(secret.data(), 1, secret.size(), pipe);
    std::fputc('\n', pipe);
    const int result = pclose(pipe);
    if (written != secret.size() || result != 0) {
        error = "Linux Secret Service could not store the key.";
        return false;
    }
    return true;
#endif
}

std::optional<std::string> ReadAnthropicApiKey() {
#ifdef _WIN32
    PCREDENTIALA credential = nullptr;
    if (!CredReadA(kCredentialTarget,
                   CRED_TYPE_GENERIC, 0, &credential))
        return std::nullopt;
    std::string value(
        reinterpret_cast<const char*>(credential->CredentialBlob),
        credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob,
                     credential->CredentialBlobSize);
    CredFree(credential);
    return value;
#elif defined(__APPLE__)
    UInt32 size = 0;
    void* data = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(std::strlen(kService)), kService,
        static_cast<UInt32>(std::strlen(kAccount)), kAccount,
        &size, &data, nullptr);
    if (status != errSecSuccess || !data) return std::nullopt;
    std::string value(static_cast<const char*>(data), size);
    Erase(static_cast<char*>(data), size);
    SecKeychainItemFreeContent(nullptr, data);
    return value;
#else
    const char* tool = secret_tool();
    if (!tool) return std::nullopt;
    const std::string command =
        std::string(tool) + " lookup service " + kService +
        " account " + kAccount;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string value;
    char buffer[256];
    while (const size_t count = std::fread(buffer, 1, sizeof(buffer), pipe))
        value.append(buffer, count);
    Erase(buffer, sizeof(buffer));
    const int result = pclose(pipe);
    if (result != 0 || value.empty()) {
        Erase(value);
        return std::nullopt;
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value.empty() ? std::nullopt
                         : std::optional<std::string>(std::move(value));
#endif
}

bool HasAnthropicApiKey() {
    auto value = ReadAnthropicApiKey();
    if (!value) return false;
    Erase(*value);
    return true;
}

bool RemoveAnthropicApiKey(std::string& error) {
#ifdef _WIN32
    if (!CredDeleteA(kCredentialTarget,
                     CRED_TYPE_GENERIC, 0) &&
        GetLastError() != ERROR_NOT_FOUND) {
        error = "Windows Credential Manager could not remove the key.";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(std::strlen(kService)), kService,
        static_cast<UInt32>(std::strlen(kAccount)), kAccount,
        nullptr, nullptr, &item);
    if (status == errSecItemNotFound) return true;
    if (status != errSecSuccess || !item) {
        error = "macOS Keychain could not remove the key.";
        return false;
    }
    status = SecKeychainItemDelete(item);
    CFRelease(item);
    if (status != errSecSuccess) {
        error = "macOS Keychain could not remove the key.";
        return false;
    }
    return true;
#else
    const char* tool = secret_tool();
    if (!tool) {
        error = "Linux Secret Service is unavailable.";
        return false;
    }
    const std::string command =
        std::string(tool) + " clear service " + kService +
        " account " + kAccount;
    if (std::system(command.c_str()) != 0) {
        error = "Linux Secret Service could not remove the key.";
        return false;
    }
    return true;
#endif
}

}  // namespace schizo::editor::secure_credentials
