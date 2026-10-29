// ============================================================================
// shadowskip_check — the lighting pass must not trace shadow rays for a
// surface the light cannot reach, and that skip must stay exact.
//
// evaluateLight() traced its shadow rays -- four to eight per pixel at the
// High preset -- before looking at whether the surface faced the light at
// all. On a back face NdotL is zero, and the light's whole contribution is a
// product with NdotL, so every one of those rays was wasted: at 20k entities
// in MyGame, Lighting fell in all 16 frame-paired samples once they were
// skipped (see PERFORMANCE_AUDIT.md F14 (q)).
//
// The skip returns vec3(0.0) instead of computing that product, which is
// exact only while two things stay true, and both are easy to break without
// noticing:
//   - the result is a product with NdotL and radiance (a term added outside
//     it -- back-lighting, translucency -- would make the skip drop light);
//   - every other factor is finite (the specular denominator's +1e-4), so the
//     product really is zero rather than 0 * inf = NaN.
// So this pins the guard, its position ahead of every shadow call, and both
// premises. Changing either premise is fine -- but it must fail here first,
// so whoever changes it re-decides whether the skip still holds.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / "tools" / "lighting_pass.frag", ec)) return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

// The body of the function whose signature starts with `signature`, braces
// matched; empty if it is not there.
std::string function_body(const std::string& src, const std::string& signature) {
    const size_t at = src.find(signature);
    if (at == std::string::npos) return {};
    const size_t open = src.find('{', at);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (size_t i = open; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}' && --depth == 0) return src.substr(open, i - open + 1);
    }
    return {};
}

}  // namespace

int main(int, char** argv) {
    std::printf("=== shadowskip_check ===\n\n");
    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) {
        std::printf("SKIP shadowskip_check: needs a source checkout (tools/lighting_pass.frag)\n");
        return 0;
    }
    std::ifstream f(root / "tools" / "lighting_pass.frag");
    std::stringstream ss; ss << f.rdbuf();
    const std::string body = function_body(ss.str(), "vec3 evaluateLight(Light L");
    check(!body.empty(), "lighting_pass.frag still has evaluateLight(Light L, ...)");
    if (body.empty()) {
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return 1;
    }

    const std::string guard = "if (NdotL <= 0.0 || all(equal(radiance, vec3(0.0)))) return vec3(0.0);";
    const size_t guard_at = body.find(guard);
    check(guard_at != std::string::npos, "the skip is there: " + guard);

    // Every shadow evaluation comes after it.
    size_t first_shadow = std::string::npos;
    for (const char* call : {"pointShadow(", "spotShadow(", "directionalShadow(", "rayQueryShadow("}) {
        const size_t p = body.find(call);
        if (p != std::string::npos && p < first_shadow) first_shadow = p;
    }
    check(first_shadow != std::string::npos, "evaluateLight still evaluates a shadow");
    check(guard_at != std::string::npos && first_shadow != std::string::npos && guard_at < first_shadow,
          "the skip comes BEFORE every shadow evaluation (else the rays are traced anyway)");

    // The premises that make returning zero exact.
    const size_t ndotl_at = body.find("float NdotL = max(dot(N, lightDir), 0.0);");
    check(ndotl_at != std::string::npos && ndotl_at < guard_at,
          "NdotL is max(dot(N, lightDir), 0) and set before the skip (zero exactly on a back face)");
    const size_t radiance_last = body.rfind("radiance = vec3(0.0);");
    check(radiance_last == std::string::npos || radiance_last < guard_at,
          "radiance is final before the skip (no later spot/cookie term revives it)");
    check(body.find("return (kD * albedo / PI + specular) * radiance * NdotL * shadow;") != std::string::npos,
          "the light's result is ONE product with radiance and NdotL (no term outside it)");
    int returns = 0;
    for (size_t p = body.find("return "); p != std::string::npos; p = body.find("return ", p + 1)) ++returns;
    check(returns == 2, "evaluateLight returns only the skip and that product (" + std::to_string(returns) + " returns)");
    check(body.find("float denominator = 4.0 * NdotV * NdotL + 1e-4;") != std::string::npos,
          "the specular denominator stays finite at NdotL = 0 (+1e-4), so the product is 0, not NaN");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL shadowskip_check — the lighting pass may trace shadow rays it then multiplies by zero,\n"
                    "     or the skip may no longer be exact. Re-decide before changing this check.\n");
    return g_fail ? 1 : 0;
}
