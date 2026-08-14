// ============================================================================
// shader_compile_service — see shader_compile_service.h.
// ============================================================================
#include "shader_compile_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace schizo::editor {

namespace {

// The surface-shader contract the generated body plugs into.
//
// Every name the graph can emit is declared here: gs_uv, gs_time,
// gs_vertex_color, gs_tex0..7, and the five gs_out_* slots. Keeping it in one
// string means the codegen and the wrapper cannot disagree about what exists --
// a mismatch would only show up as a compile error in generated code the user
// never wrote.
constexpr const char* kPreamble = R"(#version 450

layout(location = 0) in vec2 gs_uv;
layout(location = 1) in vec4 gs_vertex_color;

layout(set = 0, binding = 0) uniform sampler2D gs_tex0;
layout(set = 0, binding = 1) uniform sampler2D gs_tex1;
layout(set = 0, binding = 2) uniform sampler2D gs_tex2;
layout(set = 0, binding = 3) uniform sampler2D gs_tex3;
layout(set = 0, binding = 4) uniform sampler2D gs_tex4;
layout(set = 0, binding = 5) uniform sampler2D gs_tex5;
layout(set = 0, binding = 6) uniform sampler2D gs_tex6;
layout(set = 0, binding = 7) uniform sampler2D gs_tex7;

layout(push_constant) uniform GsPush { float time; } gs_push;

layout(location = 0) out vec4 gs_frag_albedo;
layout(location = 1) out vec4 gs_frag_material;

void main() {
    float gs_time = gs_push.time;
    vec3  gs_out_base_color;
    float gs_out_metallic;
    float gs_out_roughness;
    vec3  gs_out_emissive;
    float gs_out_alpha;
)";

constexpr const char* kEpilogue = R"(
    gs_frag_albedo   = vec4(gs_out_base_color, gs_out_alpha);
    gs_frag_material = vec4(gs_out_metallic, gs_out_roughness, 0.0, 1.0);
}
)";

uint32_t count_lines(const char* s) {
    uint32_t n = 0;
    for (const char* p = s; *p; ++p) if (*p == '\n') ++n;
    return n;
}

std::string fnv1a_hex(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// Rewrite "ERROR: <file>:47: ..." so the line refers to the generated body.
//
// Without this the user is told about a line in a preamble they never saw and
// cannot edit. A diagnostic that points at the wrong place is worse than a
// vague one, because it sends people looking in the wrong file.
std::string remap_lines(const std::string& raw, uint32_t preamble_lines) {
    std::istringstream in(raw);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        // glslang emits "ERROR: 0:47: '...' : message"
        const size_t c1 = line.find(':');
        if (c1 != std::string::npos) {
            const size_t c2 = line.find(':', c1 + 1);
            const size_t c3 = (c2 == std::string::npos) ? std::string::npos
                                                        : line.find(':', c2 + 1);
            if (c2 != std::string::npos && c3 != std::string::npos) {
                const std::string num = line.substr(c2 + 1, c3 - c2 - 1);
                bool numeric = !num.empty();
                for (char ch : num) if (ch < '0' || ch > '9') numeric = false;
                if (numeric) {
                    const long n = std::strtol(num.c_str(), nullptr, 10);
                    const long mapped = n - static_cast<long>(preamble_lines);
                    out << line.substr(0, c2 + 1)
                        << (mapped > 0 ? std::to_string(mapped)
                                       : std::string("<preamble>"))
                        << line.substr(c3) << '\n';
                    continue;
                }
            }
        }
        out << line << '\n';
    }
    return out.str();
}

bool run_capture(const std::string& cmd, std::string& out) {
    // popen rather than CreateProcess: the command is built here from paths we
    // control, there is no user-supplied text in it, and the portability is
    // worth more than the extra hundred lines.
    out.clear();
#ifdef _WIN32
    FILE* p = _popen(cmd.c_str(), "r");
#else
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) return false;
    std::array<char, 512> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), p)) out += buf.data();
#ifdef _WIN32
    const int rc = _pclose(p);
#else
    const int rc = pclose(p);
#endif
    return rc == 0;
}

}  // namespace

std::string material_shader_preamble() { return kPreamble; }
std::string material_shader_epilogue() { return kEpilogue; }
uint32_t    material_preamble_line_count() { return count_lines(kPreamble); }

bool ShaderCompileService::init(const std::string& exe_dir) {
    const char* name =
#ifdef _WIN32
        "glslangValidator.exe";
#else
        "glslangValidator";
#endif

    std::vector<fs::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(fs::path(exe_dir) / name);            // shipped beside the editor
        candidates.push_back(fs::path(exe_dir) / "tools" / name);
    }
    if (const char* sdk = std::getenv("VULKAN_SDK"))
        candidates.push_back(fs::path(sdk) / "Bin" / name);

    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) { compiler_ = c.string(); break; }
    }
    if (compiler_.empty()) {
        // Last resort: rely on the PATH. Probed rather than assumed, so a
        // missing compiler is reported once at startup instead of as a
        // mysterious failure the first time somebody edits a material.
        std::string ignored;
        if (run_capture(std::string(name) + " --version", ignored)) compiler_ = name;
    }

    std::error_code ec;
    temp_dir_ = (fs::temp_directory_path(ec) / "gws_shadergraph").string();
    fs::create_directories(temp_dir_, ec);

    if (compiler_.empty())
        spdlog::warn("[shadergraph] glslangValidator not found — material graphs can be "
                     "edited but not compiled");
    else
        spdlog::info("[shadergraph] shader compiler: {}", compiler_);
    return !compiler_.empty();
}

ShaderCompileResult ShaderCompileService::compile_material(const std::string& body) {
    ShaderCompileResult r;

    last_source_ = std::string(kPreamble) + body + kEpilogue;

    // Skip the compile when nothing changed. This is why codegen determinism is
    // a correctness requirement rather than a nicety: if an unchanged graph
    // regenerated different text, every frame would spawn a process.
    const std::string hash = fnv1a_hex(last_source_);
    if (hash == last_hash_ && !last_spirv_.empty()) {
        r.ok = true;
        r.spirv = last_spirv_;
        r.from_cache = true;
        return r;
    }

    if (compiler_.empty()) {
        r.compiler_missing = true;
        r.error = "no GLSL compiler available (glslangValidator was not found beside the "
                  "editor, in VULKAN_SDK, or on PATH)";
        return r;
    }

    const fs::path src = fs::path(temp_dir_) / ("mat_" + hash + ".frag");
    const fs::path spv = fs::path(temp_dir_) / ("mat_" + hash + ".spv");

    {
        std::ofstream f(src, std::ios::binary);
        if (!f) { r.error = "could not write the shader to " + src.string(); return r; }
        f << last_source_;
    }

    // 2>&1 so diagnostics come back through the same pipe; glslang writes them
    // to stdout anyway, but a compiler that changed its mind would otherwise
    // produce a silent failure.
    const std::string cmd = "\"\"" + compiler_ + "\" --target-env vulkan1.2 -S frag -o \"" +
                            spv.string() + "\" \"" + src.string() + "\" 2>&1\"";
    std::string output;
    const bool rc = run_capture(cmd, output);
    ++compiles_;

    std::error_code ec;
    if (!rc || !fs::exists(spv, ec)) {
        r.error = remap_lines(output, material_preamble_line_count());
        if (r.error.empty()) r.error = "the shader compiler failed without a message";
        fs::remove(src, ec);
        return r;
    }

    std::ifstream in(spv, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    fs::remove(src, ec);
    fs::remove(spv, ec);

    if (bytes.size() < 4 || (bytes.size() % 4) != 0) {
        r.error = "the compiler produced a SPIR-V module of an impossible size";
        return r;
    }

    r.spirv.resize(bytes.size() / 4);
    std::memcpy(r.spirv.data(), bytes.data(), bytes.size());

    last_hash_  = hash;
    last_spirv_ = r.spirv;
    r.ok = true;
    return r;
}

}  // namespace schizo::editor
