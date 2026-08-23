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

#include "vfx/vfx_graph.h"
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

    // ---- 4. The default stack is the current behaviour, written down --------
    {
        const VfxGraph g = VfxGraph::default_stack();
        check(g.stage(VfxStage::Spawn).size() == 1, "default stack has one Spawn module");
        check(g.stage(VfxStage::Init).size() == 4,  "default stack has four Init modules");
        check(g.stage(VfxStage::Update).size() == 4, "default stack has four Update modules");
        // Order is semantic: the current integrator applies gravity and THEN
        // damps the result. A stack that damps first is different motion.
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Gravity &&
              g.stage(VfxStage::Update)[1].kind == ModuleKind::Drag,
              "Gravity precedes Drag, matching the current integrator");
    }

    // ---- 5. validate() catches what fails silently -------------------------
    {
        check(VfxGraph::default_stack().validate().empty(),
              "the default stack validates clean");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        g.stage(VfxStage::Spawn).clear();
        check(!g.validate().empty(),
              "a stack with no spawn module is reported, not silently inert");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        auto& init = g.stage(VfxStage::Init);
        for (size_t i = 0; i < init.size(); ++i)
            if (init[i].kind == ModuleKind::InitColor) { init.erase(init.begin() + i); break; }
        check(!g.validate().empty(),
              "ColorOverLife with no InitColor before it is reported");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        g.max_particles = 0;
        check(!g.validate().empty(), "max_particles of 0 is reported");
    }
    {
        // A module placed in the wrong stage runs at the wrong frequency: an
        // Init module in Update re-initialises every particle every frame,
        // which reads as "the effect does not move".
        VfxGraph g = VfxGraph::default_stack();
        VfxModule stray; stray.kind = ModuleKind::InitLifetime;
        g.stage(VfxStage::Update).push_back(stray);
        check(!g.validate().empty(), "a module in the wrong stage is reported");
    }

    // ---- 6. Reordering ----------------------------------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        g.move_module(VfxStage::Update, 0, 1);
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Drag &&
              g.stage(VfxStage::Update)[1].kind == ModuleKind::Gravity,
              "move_module reorders within a stage");
    }

    std::printf("\nvfxgraph_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("vfxgraph_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
