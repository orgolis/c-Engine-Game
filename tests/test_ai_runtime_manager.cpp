#include <catch2/catch_test_macros.hpp>

#include "ai_runtime_manager.h"

// The fixture redirects Linux's XDG data path; other platforms use different
// paths and must never reach a real user's runtime during these tests.
#ifdef __linux__
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {
namespace fs = std::filesystem;

struct ScopedEnvironment {
    std::string name;
    std::optional<std::string> previous;
    ScopedEnvironment(const char* key, const std::string& value) : name(key) {
        if (const char* old = std::getenv(key)) previous = old;
        ::setenv(key, value.c_str(), 1);
    }
    ~ScopedEnvironment() {
        if (previous) ::setenv(name.c_str(), previous->c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};

struct RuntimeFixture {
    fs::path root = fs::temp_directory_path() / ("gws-runtime-update-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path codex = root / "gameworldshaper" / "ai-runtime" / "codex";
    ScopedEnvironment data{"XDG_DATA_HOME", root.string()};
    ScopedEnvironment path{"PATH", (root / "helpers").string() + ":/usr/bin:/bin"};

    RuntimeFixture() {
        fs::create_directories(codex / "bin");
        fs::create_directories(root / "helpers");
        std::ofstream(codex / "bin" / "codex") << "existing-runtime";
    }
    ~RuntimeFixture() { std::error_code ec; fs::remove_all(root, ec); }

    void download_helper(bool success) const {
        std::ofstream fake(root / "helpers" / "curl");
        fake << "#!/bin/sh\n"
                "printf 'attempt\\n' >> \"$XDG_DATA_HOME/attempts\"\n";
        if (!success) {
            fake << "exit 22\n";
        } else {
            fake << "while test \"$#\" -gt 0; do\n"
                    "  if test \"$1\" = '-o'; then shift; printf 'exit 0\\n' > \"$1\"; break; fi\n"
                    "  shift\n"
                    "done\n";
        }
        fake.close();
        fs::permissions(root / "helpers" / "curl", fs::perms::owner_all);
    }

    int attempts() const {
        std::ifstream input(root / "attempts");
        int count = 0;
        std::string line;
        while (std::getline(input, line)) ++count;
        return count;
    }
};
}  // namespace

TEST_CASE("Failed AI update keeps the installed runtime and remains retryable", "[editor][ai-runtime]") {
    RuntimeFixture fixture;
    fixture.download_helper(false);
    using namespace schizo::editor;
    REQUIRE_FALSE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE_FALSE(fs::exists(fixture.codex / "last-update-check.txt"));
    REQUIRE(FindAiRuntime(AiRuntimeProvider::Codex) == fixture.codex / "bin" / "codex");
    REQUIRE_FALSE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE(fixture.attempts() == 2);
}

TEST_CASE("Successful AI updates are throttled across repeated checks and old markers expire", "[editor][ai-runtime]") {
    RuntimeFixture fixture;
    fixture.download_helper(true);
    using namespace schizo::editor;
    REQUIRE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE(fs::exists(fixture.codex / "last-update-check.txt"));
    REQUIRE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE(fixture.attempts() == 1);
    std::ofstream(fixture.codex / "last-update-check.txt") << 1;
    REQUIRE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE(fixture.attempts() == 2);
}

TEST_CASE("Automatic AI updates never install missing runtimes or replace external installations", "[editor][ai-runtime]") {
    RuntimeFixture fixture;
    fixture.download_helper(true);
    using namespace schizo::editor;
    fs::remove(fixture.codex / "bin" / "codex");
    REQUIRE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Codex).ok);
    REQUIRE(UpdateManagedAiRuntimeIfDue(AiRuntimeProvider::Claude).ok);
    REQUIRE(fixture.attempts() == 0);
}
#endif
