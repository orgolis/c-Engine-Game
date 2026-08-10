// ============================================================================
// emitter_check — particles must actually simulate, on real entities.
//
// vfx/particle_system was verified (12 assertions) and then referenced by
// exactly one editor file: the feature-toggle enum. Nothing simulated a single
// particle. This covers the connection, not the maths.
//
// The property most worth pinning is the REBASE one. Particles hold world-space
// positions, so a floating-origin rebase has to shift them like everything
// else. Miss it and nothing errors — the emitter simply walks away from its own
// smoke the first time the camera crosses the threshold, hundreds of metres
// from where anyone is looking when it happens.
// ============================================================================
#include "particle_emitter_cache.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

std::shared_ptr<scene::Entity> add_emitter(const std::shared_ptr<scene::Scene>& s,
                                           const char* name, glm::vec3 pos) {
    auto e = s->CreateEntity(name);
    e->GetTransform()->SetLocalPosition(pos);
    auto* pe = e->GetParticleEmitterComponent();
    pe->enabled  = true;
    pe->emitting = true;
    return e;
}

// Run enough frames for spawning to produce particles.
void run(editor::EditorParticleEmitters& em,
         const std::shared_ptr<scene::Scene>& s, int frames,
         const glm::vec3& shift = glm::vec3(0.0f)) {
    for (int i = 0; i < frames; ++i)
        em.update(s, 1.0f / 60.0f, glm::vec3(0, 2, 10), glm::vec3(0, 1, 0),
                  i == 0 ? shift : glm::vec3(0.0f));
}

}  // namespace

int main() {
    std::printf("=== emitter_check ===\n\n");

    // ---- 1. an emitter entity actually simulates ---------------------------
    {
        auto s = std::make_shared<scene::Scene>("t");
        add_emitter(s, "Fire", {0, 0, 0});
        editor::EditorParticleEmitters em;
        run(em, s, 30);

        check(em.emitter_count() == 1, "the emitter entity gets a simulation");
        check(em.live_particles() > 0, "particles are alive after 30 frames");
        check(em.vertex_count() == em.live_particles() * 4,
              "billboards are four verts per live particle");
    }

    // ---- 2. a non-emitter entity does nothing ------------------------------
    // The component defaults to disabled, so ordinary entities must not each
    // acquire a particle system.
    {
        auto s = std::make_shared<scene::Scene>("t");
        s->CreateEntity("JustACube");
        editor::EditorParticleEmitters em;
        run(em, s, 10);
        check(em.emitter_count() == 0, "an entity without the component simulates nothing");
        check(em.vertex_count() == 0, "and produces no billboards");
    }

    // ---- 3. THE REBASE CONTRACT --------------------------------------------
    // Particles are world-space. A floating-origin shift must move them too, or
    // the emitter walks away from its own smoke — silently.
    {
        auto s = std::make_shared<scene::Scene>("t");
        add_emitter(s, "Fire", {0, 0, 0});
        editor::EditorParticleEmitters em;
        run(em, s, 30);

        // Centroid of the billboard cloud before and after a rebase.
        auto centroid = [](const std::vector<vfx::BillboardVertex>& v) {
            glm::vec3 c(0.0f);
            for (const auto& b : v) c += b.pos;
            return v.empty() ? c : c / static_cast<float>(v.size());
        };
        const glm::vec3 before = centroid(em.billboards());
        check(!em.billboards().empty(), "there are particles to rebase");

        const glm::vec3 shift(-1024.0f, 0.0f, 512.0f);
        em.update(s, 1.0f / 60.0f, glm::vec3(0, 2, 10), glm::vec3(0, 1, 0), shift);
        const glm::vec3 after = centroid(em.billboards());

        // Newly spawned particles land at the (unshifted) emitter, so the
        // centroid does not move by exactly `shift`. What matters is that it
        // moved substantially TOWARDS it rather than staying put.
        check(std::abs(after.x - before.x) > 100.0f,
              "a floating-origin rebase moves live particles with the world");
        check(after.x < before.x, "and in the direction of the shift, not away from it");
    }

    // ---- 4. two emitters are independent -----------------------------------
    // Sharing one system, or one seed, would make every fire in the scene
    // identical — which looks obviously wrong and is easy to do accidentally.
    {
        auto s = std::make_shared<scene::Scene>("t");
        add_emitter(s, "A", {0, 0, 0});
        add_emitter(s, "B", {50, 0, 0});
        editor::EditorParticleEmitters em;
        run(em, s, 30);
        check(em.emitter_count() == 2, "each emitter entity gets its own simulation");

        // Particles should exist near BOTH emitters, not pooled at one.
        bool near_a = false, near_b = false;
        for (const auto& b : em.billboards()) {
            if (std::abs(b.pos.x -  0.0f) < 10.0f) near_a = true;
            if (std::abs(b.pos.x - 50.0f) < 10.0f) near_b = true;
        }
        check(near_a && near_b, "particles appear at both emitters, not pooled at one");
    }

    // ---- 5. emitters follow their entity -----------------------------------
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = add_emitter(s, "Moving", {0, 0, 0});
        editor::EditorParticleEmitters em;
        run(em, s, 5);
        e->GetTransform()->SetLocalPosition({200.0f, 0.0f, 0.0f});
        run(em, s, 30);

        bool any_near_new = false;
        for (const auto& b : em.billboards())
            if (std::abs(b.pos.x - 200.0f) < 20.0f) any_near_new = true;
        check(any_near_new, "moving the entity moves where new particles spawn");
    }

    // ---- 6. disabling stops the simulation ---------------------------------
    {
        auto s = std::make_shared<scene::Scene>("t");
        auto e = add_emitter(s, "Off", {0, 0, 0});
        editor::EditorParticleEmitters em;
        run(em, s, 20);
        check(em.emitter_count() == 1, "running while enabled");

        e->GetParticleEmitterComponent()->enabled = false;
        run(em, s, 2);
        check(em.emitter_count() == 0, "disabling releases the simulation");
        check(em.vertex_count() == 0, "and stops producing billboards");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL emitter_check\n");
    return g_fail ? 1 : 0;
}
