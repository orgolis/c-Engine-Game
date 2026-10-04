#pragma once

#include <filesystem>
#include <string>

namespace schizo::editor {

enum class AiRuntimeProvider { Codex, Claude };

struct AiRuntimeInstallResult {
    bool ok = false;
    std::string error;
};

// Resolves an engine-managed runtime first, then an existing official runtime.
// Credentials are deliberately not kept anywhere in this runtime directory.
std::filesystem::path FindAiRuntime(AiRuntimeProvider provider);

// Downloads the provider's official native runtime. Codex is installed into a
// private per-user GameWorldshaper directory; Claude uses Anthropic's official
// per-user location because that installer does not expose a custom directory.
AiRuntimeInstallResult InstallAiRuntime(AiRuntimeProvider provider);

// Checks once per day, across editor restarts. Only the engine-managed Codex
// installation is updated; credentials remain in the OS keyring.
AiRuntimeInstallResult UpdateManagedAiRuntimeIfDue(AiRuntimeProvider provider);

}  // namespace schizo::editor
