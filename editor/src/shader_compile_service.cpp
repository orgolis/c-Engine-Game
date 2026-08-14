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
// The surface-shader contract, and it is NOT invented here -- it mirrors
// gbuffer_scene.frag exactly.
//
// My first version of this declared its own inputs (gs_uv at location 0), its
// own descriptors (set 0, bindings 0-7) and TWO colour outputs. It compiled
// perfectly and could never have been used: the real G-Buffer takes five vertex
// inputs, binds a MaterialUBO plus five texture maps at SET 1, and writes FOUR
// attachments. A pipeline built from that module would have been rejected the
// moment it met the real render pass and descriptor layout.
//
// That is the trap this whole feature invites -- a generated shader that
// compiles is not a generated shader that can be bound. Keeping the interface
// identical to the shader it replaces is what makes the graph's output a
// drop-in.
//
// The graph writes the five surface values; everything else (world position,
// normal mapping through the TBN basis, alpha cutout) stays exactly as the
// stock shader does it, so a graph material behaves like a normal one in every
// respect the graph does not explicitly change.
constexpr const char* kPreamble = R"(#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

// The graph's texture slots map onto the material's existing maps, so a
// TextureSample node samples something real without new descriptors.
#define gs_tex0 albedoMap
#define gs_tex1 normalMap
#define gs_tex2 mrMap
#define gs_tex3 aoMap
#define gs_tex4 emissiveMap
#define gs_tex5 albedoMap
#define gs_tex6 albedoMap
#define gs_tex7 albedoMap

void main() {
    vec2 gs_uv           = inUV;
    vec4 gs_vertex_color = vec4(1.0);
    // No per-frame clock is plumbed into the G-Buffer's descriptor layout yet,
    // and inventing one here would change the pipeline layout and break
    // compatibility. Time nodes therefore read 0 until that is plumbed --
    // stated plainly rather than left as a mystery for whoever wires one up.
    float gs_time        = 0.0;

    vec3  gs_out_base_color;
    float gs_out_metallic;
    float gs_out_roughness;
    vec3  gs_out_emissive;
    float gs_out_alpha;
)";

constexpr const char* kEpilogue = R"(
    // Alpha cutout, matching the stock shader so graph materials clip the same.
    float gs_cutoff = mat.emissive_factor.a;
    if (gs_cutoff > 0.0 && gs_out_alpha < gs_cutoff) discard;

    vec3 nmap = texture(normalMap, inUV).xyz * 2.0 - 1.0;
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    float gs_ao = texture(aoMap, inUV).r * mat.occlusion_strength;

    outPosition        = vec4(inWorldPos, 1.0);
    outNormalRoughness = vec4(N, clamp(gs_out_roughness, 0.04, 1.0));
    outAlbedoMetallic  = vec4(gs_out_base_color, clamp(gs_out_metallic, 0.0, 1.0));
    outMaterial        = vec4(gs_out_emissive, gs_ao);
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
