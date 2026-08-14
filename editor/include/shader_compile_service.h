#pragma once
// ============================================================================
// shader_compile_service — GLSL to SPIR-V, for generated material shaders (4.1).
//
// The engine cannot compile GLSL in-process: GWS_HAS_GLSLANG is defined nowhere,
// because the glslang wrapper links only under MSVC and this is a MinGW build.
// Runtime shader compilation has never worked here, so every shader the engine
// runs is precompiled at build time.
//
// A material graph breaks that assumption -- its shader does not exist until a
// user makes it. So the editor invokes glslangValidator as a subprocess and
// reads the SPIR-V back. Compile on SAVE, not per keystroke: a process launch
// is milliseconds, but doing it on every dragged slider would spawn hundreds.
//
// The compiler is shipped BESIDE THE EDITOR rather than assumed present. A user
// of the editor is not a Vulkan developer and will not have the SDK installed;
// requiring it would mean the material graph works for us and silently fails
// for them, which is the worst of the possible outcomes.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor {

struct ShaderCompileResult {
    bool                  ok = false;
    std::vector<uint32_t> spirv;
    std::string           error;       // compiler diagnostics, line-mapped
    bool                  from_cache = false;
    bool                  compiler_missing = false;
};

class ShaderCompileService {
public:
    /// Locate glslangValidator. Looks beside the running executable first (the
    /// shipped copy), then $VULKAN_SDK/Bin, then the PATH. Returns false when
    /// none is found, which callers must report as "shader compilation
    /// unavailable" rather than as a compile error -- they mean different
    /// things and lead to different fixes.
    bool init(const std::string& exe_dir);

    bool available() const { return !compiler_.empty(); }
    const std::string& compiler_path() const { return compiler_; }

    /// Compile a fragment shader body produced by the material graph.
    ///
    /// `body` is wrapped in the surface-shader preamble before compiling, and
    /// any diagnostics have the preamble's line count subtracted so reported
    /// line numbers refer to the GENERATED BODY. A user staring at "error on
    /// line 47" when their shader is nine lines long learns nothing.
    ShaderCompileResult compile_material(const std::string& body);

    /// Number of compiles actually run (cache hits excluded). Exposed so
    /// "compilation is skipped when nothing changed" is checkable rather than
    /// merely intended.
    uint32_t compile_count() const { return compiles_; }

    /// The full wrapped source last submitted, for showing the user what was
    /// really compiled when an error refers to something they did not write.
    const std::string& last_source() const { return last_source_; }

private:
    std::string compiler_;
    std::string temp_dir_;
    uint32_t    compiles_ = 0;

    std::string last_hash_;
    std::vector<uint32_t> last_spirv_;
    std::string last_source_;
};

/// The preamble + epilogue the graph body is wrapped in. Exposed so tests can
/// compile the same thing the editor does instead of a lookalike that drifts.
std::string material_shader_preamble();
std::string material_shader_epilogue();
uint32_t    material_preamble_line_count();

}  // namespace schizo::editor
