// ============================================================================
// reflectio_check — reflected components survive save and load.
//
// Two things are under test, and the first is a bug I shipped earlier this
// session: particle emitters and NPC agents were never written to the scene
// file AT ALL. Configure an emitter, save, reopen, and every setting was
// silently back at its default. I had reflected both components specifically so
// this could not happen, and then never connected the reflection to the
// serializer -- the same "verified library nothing runs" pattern I spent this
// phase finding in other people's work.
//
// The second is the property that keeps it fixed: the text is generated FROM
// the reflection, so a field added later is written and read with no serializer
// edit at all. The assertion that earns its keep is therefore not "these values
// round-trip" but "a field nobody wrote code for round-trips".
//
// Values are deliberately non-default throughout. A serializer that writes
// nothing and a serializer that writes correctly are indistinguishable when the
// test data is all defaults.
// ============================================================================
#include "reflected_text_io.h"
#include "scene_component_reflect.h"

#include <cstdio>
#include <sstream>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// Save to text, then load every line back — exactly what the scene serializer
// does, minus the file.
template <class T>
T round_trip(const T& in, const std::string& prefix, std::string* out_text = nullptr) {
    std::ostringstream os;
    scene::write_reflected(os, prefix, &in, *gws::reflect::reflect<T>());
    if (out_text) *out_text = os.str();

    T out{};
    std::istringstream is(os.str());
    std::string line;
    while (std::getline(is, line))
        scene::apply_reflected_line(prefix, line, &out, *gws::reflect::reflect<T>());
    return out;
}

}  // namespace

int main() {
    std::printf("=== reflectio_check ===\n\n");

    // ---- 1. an emitter survives the trip ------------------------------------
    {
        scene::ParticleEmitterComponent in;
        in.enabled       = true;
        in.emitting      = false;          // false matters: a writer that skips
                                           // falsy values would pass on `true`
        in.spawn_rate    = 37.5f;
        in.max_particles = 4096;
        in.color_start   = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        in.color_end     = glm::vec4(0.5f, 0.6f, 0.7f, 0.8f);

        std::string text;
        const auto out = round_trip(in, "EMITTER", &text);

        check(out.enabled && !out.emitting,
              "emitter: bools round-trip, including the false one");
        check(out.max_particles == 4096u && std::abs(out.spawn_rate - 37.5f) < 1e-4f,
              "emitter: uint and float round-trip");
        check(std::abs(out.color_start.a - 0.4f) < 1e-4f &&
              std::abs(out.color_end.r - 0.5f) < 1e-4f,
              "emitter: vec4 colours round-trip, alpha included");
        check(text.find("EMITTER.spawn_rate=") != std::string::npos,
              "and the key is the REFLECTED field name, not one invented here");
    }

    // ---- 2. an NPC agent survives the trip ----------------------------------
    {
        scene::NpcAgentComponent in;
        in.enabled       = true;
        in.sight_range   = 42.0f;
        in.sight_fov_deg = 77.0f;
        in.move_speed    = 6.5f;
        in.patrol_radius = 12.0f;
        in.attack_range  = 3.5f;
        in.ability_index = 2;

        const auto out = round_trip(in, "NPCAGENT");
        check(out.enabled &&
              std::abs(out.sight_range - 42.0f) < 1e-4f &&
              std::abs(out.attack_range - 3.5f) < 1e-4f &&
              out.ability_index == 2,
              "npc agent: every reflected field round-trips");
    }

    // ---- 3. THE ONE THAT EARNS ITS KEEP -------------------------------------
    // Nothing in this file, and nothing in the serializer, names these fields.
    // They round-trip because they are reflected. That is the whole point: the
    // next field somebody adds costs zero serializer edits, instead of four
    // hand edits whose failure mode is a scene that silently loses data.
    {
        scene::SkinnedMeshComponent in;
        in.clip_index        = 3;
        in.playing           = false;
        in.speed             = 2.5f;
        in.drive_from_motion = true;
        in.idle_clip         = 1;
        in.move_clip         = 4;
        in.move_threshold    = 0.33f;

        const auto out = round_trip(in, "SKINNED");

        const auto* ti = gws::reflect::reflect<scene::SkinnedMeshComponent>();
        check(ti->fields.size() >= 7,
              "the component reports at least its seven reflected fields");
        check(out.clip_index == 3 && out.idle_clip == 1 && out.move_clip == 4 &&
              !out.playing && out.drive_from_motion &&
              std::abs(out.speed - 2.5f) < 1e-4f &&
              std::abs(out.move_threshold - 0.33f) < 1e-4f,
              "every one of them round-trips with NO per-field serializer code");
    }

    // ---- 4. it does not eat other components' lines -------------------------
    // The serializer feeds every line to every parser in turn. A prefix match
    // that was too eager would swallow lines belonging to something else, and
    // the loss would show up somewhere unrelated.
    {
        scene::ParticleEmitterComponent e;
        const auto* ti = gws::reflect::reflect<scene::ParticleEmitterComponent>();
        check(!scene::apply_reflected_line("EMITTER", "NPCAGENT.sight_range=5", &e, *ti),
              "a different component's line is declined");
        check(!scene::apply_reflected_line("EMITTER", "EMITTER_LEGACY=1", &e, *ti),
              "and so is a key that merely starts with the prefix");
        check(!scene::apply_reflected_line("EMITTER", "EMITTER.no_such_field=1", &e, *ti),
              "and an unknown field name, rather than being applied somewhere");
        check(!scene::apply_reflected_line("EMITTER", "MESH_PATH=cube.obj", &e, *ti),
              "an unrelated legacy line falls through to its own parser");
    }

    // ---- 5. defaults survive too --------------------------------------------
    // A round trip that only works on values somebody changed is a round trip
    // that silently resets everything left alone.
    {
        const scene::NpcAgentComponent in;
        const auto out = round_trip(in, "NPCAGENT");
        check(out.sight_range == in.sight_range && out.enabled == in.enabled &&
              out.ability_index == in.ability_index,
              "a default-constructed component round-trips unchanged");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL reflectio_check\n");
    return g_fail ? 1 : 0;
}
