// ============================================================================
// includes_check — a file that uses a fixed-width type must include <cstdint>.
//
// This is the lint for the bug that broke the v0.6.0 release: command_palette.h
// used uint32_t with no <cstdint>, built cleanly here, and failed on the CI
// runner.
//
// The obvious guard -- compile every header on its own -- does NOT catch it,
// and I verified that rather than assuming: on this toolchain <memory> pulls in
// <cstdint> transitively, so the header compiles alone and the missing include
// stays invisible. CI's libstdc++ does not, which is the entire problem.
// Anything that depends on WHICH standard headers leak WHICH types can only
// reproduce the bug by accident.
//
// A text scan has no such dependency: if a file names uint32_t and does not
// include <cstdint>, it is relying on somebody else's include, and that is true
// on every compiler. Crude on purpose -- the precise version is the one that
// cannot be trusted.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Types that live in <cstdint>. size_t/ptrdiff_t are deliberately absent: they
// come from <cstddef> and arrive through so many standard headers that flagging
// them would be noise, and noise is how a lint gets switched off.
const char* kFixedWidth[] = {
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t",  "int16_t",  "int32_t",  "int64_t",
    "uintptr_t", "intptr_t",
};

struct Finding {
    std::string file;
    std::string type;
    int         line = 0;
};

// Skip comments and string literals: a type named in prose is not a use, and
// flagging one teaches people the lint is wrong.
std::string strip_noise(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool in_line = false, in_block = false, in_str = false, in_chr = false;

    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char n = i + 1 < src.size() ? src[i + 1] : '\0';

        if (in_line)  { if (c == '\n') { in_line = false; out += c; } else out += ' '; continue; }
        if (in_block) { if (c == '*' && n == '/') { in_block = false; ++i; out += "  "; }
                        else out += (c == '\n' ? '\n' : ' '); continue; }
        if (in_str)   { if (c == '\\') { ++i; out += "  "; continue; }
                        if (c == '"') in_str = false; out += ' '; continue; }
        if (in_chr)   { if (c == '\\') { ++i; out += "  "; continue; }
                        if (c == '\'') in_chr = false; out += ' '; continue; }

        if (c == '/' && n == '/') { in_line  = true; out += "  "; ++i; continue; }
        if (c == '/' && n == '*') { in_block = true; out += "  "; ++i; continue; }
        if (c == '"')  { in_str = true; out += ' '; continue; }
        if (c == '\'') { in_chr = true; out += ' '; continue; }
        out += c;
    }
    return out;
}

bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Whole-word only: "my_uint32_type" is not a use of uint32_t.
bool uses_word(const std::string& hay, const std::string& word, int& out_line) {
    size_t pos = 0;
    while ((pos = hay.find(word, pos)) != std::string::npos) {
        const bool left  = pos == 0 || !is_ident_char(hay[pos - 1]);
        const bool right = pos + word.size() >= hay.size() ||
                           !is_ident_char(hay[pos + word.size()]);
        if (left && right) {
            out_line = 1;
            for (size_t i = 0; i < pos; ++i) if (hay[i] == '\n') ++out_line;
            return true;
        }
        pos += word.size();
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== includes_check ===\n\n");

    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::current_path();
    const std::vector<std::string> scan = {"engine", "editor", "tools"};

    std::vector<Finding> findings;
    size_t scanned = 0;

    for (const std::string& dir : scan) {
        const fs::path base = root / dir;
        if (!fs::exists(base)) continue;

        for (const auto& e : fs::recursive_directory_iterator(base)) {
            if (!e.is_regular_file()) continue;
            const std::string p = e.path().string();
            if (p.find("third_party") != std::string::npos) continue;
            if (p.find("build") != std::string::npos) continue;

            const std::string ext = e.path().extension().string();
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".cc") continue;

            std::ifstream in(e.path(), std::ios::binary);
            if (!in) continue;
            std::ostringstream ss; ss << in.rdbuf();
            const std::string code = strip_noise(ss.str());
            ++scanned;

            if (code.find("<cstdint>") != std::string::npos ||
                code.find("<stdint.h>") != std::string::npos)
                continue;

            for (const char* t : kFixedWidth) {
                int line = 0;
                if (uses_word(code, t, line)) {
                    findings.push_back({fs::relative(e.path(), root).string(), t, line});
                    break;      // one report per file is enough to act on
                }
            }
        }
    }

    std::printf("  scanned %zu files\n\n", scanned);

    if (findings.empty()) {
        std::printf("  OK   every file naming a fixed-width type includes <cstdint>\n");
        std::printf("\n1 passed, 0 failed\n");
        return 0;
    }

    for (const Finding& f : findings)
        std::printf("  FAIL %s:%d uses %s without including <cstdint>\n",
                    f.file.c_str(), f.line, f.type.c_str());

    std::printf("\n0 passed, %zu failed\n", findings.size());
    std::printf("FAIL includes_check\n");
    std::printf("\n  These compile only because some other header happens to pull\n"
                "  <cstdint> in first. That is not portable, and it is what broke\n"
                "  the v0.6.0 release build.\n");
    return 1;
}
