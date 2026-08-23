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

#include "vfx/particle_system.h"
#include "vfx/vfx_io.h"
#include "vfx/vfx_graph.h"
#include "vfx/vfx_module.h"

#include <algorithm>
#include <cmath>
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

    // ---- 7. Module ORDER changes the result --------------------------------
    // Gravity-then-Drag damps the gravity contribution of this frame;
    // Drag-then-Gravity does not. The difference is small per frame and
    // compounds, which is exactly the kind of bug a screenshot cannot show.
    {
        auto run = [](bool gravity_first) {
            VfxGraph g = VfxGraph::default_stack();
            auto& up = g.stage(VfxStage::Update);
            if (!gravity_first) std::swap(up[0], up[1]);
            // Remove randomness so the comparison is exact.
            for (auto& m : g.stage(VfxStage::Init))
                if (m.kind == ModuleKind::InitVelocityCone) m.params["SPREAD"] = 0.0f;
            for (auto& m : g.stage(VfxStage::Update))
                if (m.kind == ModuleKind::Drag) m.params["DRAG"] = 2.0f;

            ParticleSystem ps;
            ps.set_graph(g);
            ps.set_seed(7);
            ps.emit_burst(1);
            for (int i = 0; i < 10; ++i) ps.update(0.016f);
            return ps.particles().empty() ? 0.0f : ps.particles()[0].pos.y;
        };
        const float a = run(true), b = run(false);
        check(std::fabs(a - b) > 1e-4f,
              "Gravity-then-Drag differs numerically from Drag-then-Gravity");
    }

    // ---- 8. The default stack matches the pre-4.3 integrator ---------------
    // The regression gate in one assertion: a stack-driven system and the old
    // arithmetic must agree, or every saved effect changed meaning.
    {
        EmitterConfig cfg;
        cfg.spawn_rate      = 0.0f;
        cfg.base_velocity   = glm::vec3(0.0f, 10.0f, 0.0f);
        cfg.velocity_spread = 0.0f;
        cfg.gravity         = glm::vec3(0.0f, -10.0f, 0.0f);
        cfg.drag            = 0.5f;

        ParticleSystem ps(cfg);               // the facade path
        ps.set_seed(3);
        ps.emit_burst(1);

        // Hand-rolled reference using the OLD arithmetic, exactly.
        glm::vec3 vel(0.0f, 10.0f, 0.0f), pos(0.0f);
        const float dt = 0.016f;
        for (int i = 0; i < 20; ++i) {
            ps.update(dt);
            const float damp = std::max(0.0f, 1.0f - 0.5f * dt);
            vel += glm::vec3(0.0f, -10.0f, 0.0f) * dt;
            vel *= damp;
            pos += vel * dt;
        }
        const bool ok = !ps.particles().empty() &&
                        std::fabs(ps.particles()[0].pos.y - pos.y) < 1e-4f;
        check(ok, "the default stack reproduces the pre-4.3 integrator exactly");
    }

    // ---- 9. Round-trip, including module ORDER -----------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        g.name = "campfire_embers";
        g.move_module(VfxStage::Update, 0, 1);          // Drag now precedes Gravity
        const VfxGraph back = vfx_from_text(vfx_to_text(g));
        check(back.name == "campfire_embers", "name round-trips");
        check(back.stage(VfxStage::Update).size() == 4, "module count round-trips");
        check(back.stage(VfxStage::Update)[0].kind == ModuleKind::Drag &&
              back.stage(VfxStage::Update)[1].kind == ModuleKind::Gravity,
              "module ORDER round-trips -- a sorted reload would change the effect");
    }

    // ---- 10. Serialisation is deterministic --------------------------------
    {
        const VfxGraph g = VfxGraph::default_stack();
        check(vfx_to_text(g) == vfx_to_text(g), "the same graph serialises byte-identically");
    }

    // ---- 11. Curve and Gradient parameters survive -------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        for (auto& m : g.stage(VfxStage::Update)) {
            if (m.kind != ModuleKind::ColorOverLife) continue;
            gws::anim::Gradient grad;
            grad.add({0.0f, glm::vec4(1, 0, 0, 1)});
            grad.add({0.5f, glm::vec4(0, 1, 0, 1)});
            grad.add({1.0f, glm::vec4(0, 0, 1, 0)});
            m.params["GRADIENT"] = grad;
        }
        const VfxGraph back = vfx_from_text(vfx_to_text(g));
        size_t stops = 0, keys = 0;
        for (const auto& m : back.stage(VfxStage::Update)) {
            if (m.kind == ModuleKind::ColorOverLife)
                stops = m.get_gradient("GRADIENT", gws::anim::Gradient()).size();
            if (m.kind == ModuleKind::SizeOverLife)
                keys = m.get_curve("CURVE", gws::anim::Curve()).keys().size();
        }
        check(stops == 3, "a three-stop gradient round-trips with all three stops");
        check(keys == 2, "a two-key curve round-trips with both keys");
    }

    // ---- 12. Indices are grouping, not position ----------------------------
    // A hand-edited file with descending or duplicated indices must load in
    // FILE order. Treating the index as a position would reorder the effect.
    {
        const std::string text =
            "VFX_VERSION=1\nNAME=x\nMAX_PARTICLES=64\nWORLD_SPACE=1\n"
            "STAGE=UPDATE\n"
            "MODULE.9.KIND=Drag\nMODULE.9.DRAG=0.5\n"
            "MODULE.2.KIND=Gravity\nMODULE.2.GRAVITY=0,-9.8,0\n";
        const VfxGraph g = vfx_from_text(text);
        check(g.stage(VfxStage::Update).size() == 2, "both modules load");
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Drag,
              "descending indices load in file order, not sorted order");
    }

    // ---- 13. Garbage yields defaults, absence is distinguishable -----------
    {
        const VfxGraph g = vfx_from_text("this is not a vfx file\n\x01\x02\n");
        check(g.stage(VfxStage::Update).empty() && g.max_particles == 2048,
              "an unparseable string yields an empty, default graph rather than garbage");
    }
    {
        VfxGraph g;
        check(!load_vfx("definitely/not/here.vfx", g),
              "load_vfx reports a missing file -- absent and unparseable are different");
    }
    {
        // An unknown module KIND is skipped, not defaulted to SpawnRate.
        const std::string text =
            "VFX_VERSION=1\nSTAGE=UPDATE\n"
            "MODULE.0.KIND=NotAModule\nMODULE.0.X=1\n"
            "MODULE.1.KIND=Gravity\nMODULE.1.GRAVITY=0,-1,0\n";
        const VfxGraph g = vfx_from_text(text);
        check(g.stage(VfxStage::Update).size() == 1 &&
              g.stage(VfxStage::Update)[0].kind == ModuleKind::Gravity,
              "an unknown module kind is skipped, not silently turned into another module");
    }

    std::printf("\nvfxgraph_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("vfxgraph_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
