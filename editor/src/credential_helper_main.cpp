#include "secure_credential_store.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "anthropic") return 2;
    auto secret = schizo::editor::secure_credentials::ReadAnthropicApiKey();
    if (!secret) return 1;
    const size_t written =
        std::fwrite(secret->data(), 1, secret->size(), stdout);
    std::fputc('\n', stdout);
    const bool ok = written == secret->size() && std::fflush(stdout) == 0;
    schizo::editor::secure_credentials::Erase(*secret);
    return ok ? 0 : 1;
}
