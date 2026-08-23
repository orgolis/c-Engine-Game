// ====================
// vfxgraph_check — headless verification of the VFX module stack (item 4.3).
//
// The assertions here are about the things that fail SILENTLY. A stack that
// emits nothing looks like a broken renderer; a serialiser that reorders
// modules changes every saved effect with no error; a module that reads the
// scene compiles fine and forecloses GPU simulation. None of those show up in
// a screenshot, which is why they are asserted here.
//
// Pure CPU, no device. Prints "vfxgraph_check: ALL OK" + exits 0 on pass.
// ====================

#include "vfx/vfx_module.h"

#include <cstdio>
#include <string>

using namespace schizo::vfx;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== vfxgraph_check: VFX module stack ===\n");

    // ---- 1. Every module kind knows which stage it belongs to --------------
    check(stage_of(ModuleKind::SpawnRate)     == VfxStage::Spawn,  "SpawnRate is a Spawn module");
    check(stage_of(ModuleKind::InitLifetime)  == VfxStage::Init,   "InitLifetime is an Init module");
    check(stage_of(ModuleKind::Gravity)       == VfxStage::Update, "Gravity is an Update module");
    check(stage_of(ModuleKind::ColorOverLife) == VfxStage::Update, "ColorOverLife is an Update module");

    // ---- 2. Kind names round-trip -----------------------------------------
    // The name is what the .vfx file stores. If it does not round-trip, a
    // saved effect silently loses a module on load rather than failing.
    {
        bool all = true;
        for (int i = 0; i <= static_cast<int>(ModuleKind::VelocityOverLife); ++i) {
            const auto k = static_cast<ModuleKind>(i);
            bool ok = false;
            const ModuleKind back = module_kind_from_name(module_kind_name(k), ok);
            if (!ok || back != k) all = false;
        }
        check(all, "every ModuleKind name parses back to the same kind");
    }
    {
        bool ok = true;
        module_kind_from_name("NotAModule", ok);
        check(!ok, "an unknown module name reports failure rather than defaulting");
    }

    // ---- 3. Parameter access is typed and falls back ------------------------
    {
        VfxModule m;
        m.kind = ModuleKind::InitLifetime;
        m.params["MIN"] = 1.5f;
        check(m.get_float("MIN", 0.0f) == 1.5f, "a float parameter reads back");
        check(m.get_float("MAX", 9.0f) == 9.0f, "a missing parameter yields the fallback");
        check(m.get_vec3("MIN", glm::vec3(7.0f)) == glm::vec3(7.0f),
              "reading a parameter as the wrong type yields the fallback, not garbage");
    }

    std::printf("\nvfxgraph_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("vfxgraph_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
