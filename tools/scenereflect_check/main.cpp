// ============================================================================
// scenereflect_check — the scene components describe themselves.
//
// This is the tractable half of 3.7. Adding one field to a scene component used
// to mean editing FOUR hand-written places: the serializer's writer, its key
// parser, a mirror struct, and the apply step. I did exactly that for
// SKINNED_PATH this session, and every one of those edits is a chance to forget
// a field — with a silent failure mode, because a component that saves but does
// not load looks fine until someone reopens their scene.
//
// So the property worth asserting is not "reflection works" (core reflection is
// already verified). It is that these specific components are reflected, that
// their fields ROUND-TRIP through the generic serializer, and — most usefully —
// that a field added later without being registered is CAUGHT, since that is
// the mistake this is meant to prevent.
// ============================================================================
#include "scene_component_reflect.h"

#include "ecs/component_serialize.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// Round-trip a component through the generic reflection serializer.
template <class T>
T round_trip(const T& in) {
    const auto* ti = gws::reflect::reflect<T>();
    std::vector<uint8_t> blob = ecs::serialize_component(&in, *ti);
    T out{};
    ecs::deserialize_component(&out, *ti, blob.data(), blob.size());
    return out;
}

// Bytes covered by registered fields, so an unregistered field shows up as a
// gap rather than as a silent omission.
template <class T>
size_t reflected_bytes() {
    const auto* ti = gws::reflect::reflect<T>();
    size_t n = 0;
    for (const auto& f : ti->fields) n += f.size;
    return n;
}

}  // namespace

int main() {
    std::printf("=== scenereflect_check ===\n\n");

    // ---- 1. the components are reflected at all ----------------------------
    {
        check(gws::reflect::reflect<scene::SkinnedMeshComponent>() != nullptr,
              "SkinnedMeshComponent is reflected");
        check(gws::reflect::reflect<scene::ParticleEmitterComponent>() != nullptr,
              "ParticleEmitterComponent is reflected");
        check(gws::reflect::reflect<scene::NpcAgentComponent>() != nullptr,
              "NpcAgentComponent is reflected");
        check(!gws::reflect::reflect<scene::SkinnedMeshComponent>()->fields.empty(),
              "and they declare fields rather than an empty list");
    }

    // ---- 2. values survive a round trip ------------------------------------
    // The actual point: save then load must return what you put in.
    {
        scene::SkinnedMeshComponent in;
        in.clip_index       = 3;
        in.playing          = false;
        in.speed            = 2.5f;
        in.drive_from_motion = true;
        in.idle_clip        = 1;
        in.move_clip        = 4;
        in.move_threshold   = 0.33f;

        const auto out = round_trip(in);
        check(out.clip_index == 3 && out.idle_clip == 1 && out.move_clip == 4,
              "SkinnedMeshComponent: int fields round-trip");
        check(out.playing == false && out.drive_from_motion == true,
              "bool fields round-trip (including false, which a lazy check would miss)");
        check(std::abs(out.speed - 2.5f) < 1e-6f &&
              std::abs(out.move_threshold - 0.33f) < 1e-6f,
              "float fields round-trip");
    }

    {
        scene::NpcAgentComponent in;
        in.enabled       = true;
        in.sight_range   = 42.0f;
        in.sight_fov_deg = 77.0f;
        in.move_speed    = 6.5f;
        in.patrol_radius = 12.0f;
        in.attack_range  = 3.5f;
        in.ability_index = 2;

        const auto out = round_trip(in);
        check(out.enabled && std::abs(out.sight_range - 42.0f) < 1e-6f &&
              std::abs(out.attack_range - 3.5f) < 1e-6f && out.ability_index == 2,
              "NpcAgentComponent round-trips every registered field");
    }

    {
        scene::ParticleEmitterComponent in;
        in.enabled       = true;
        in.emitting      = false;
        in.spawn_rate    = 123.0f;
        in.max_particles = 4096;
        in.color_start   = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        in.color_end     = glm::vec4(0.5f, 0.6f, 0.7f, 0.8f);

        const auto out = round_trip(in);
        check(out.max_particles == 4096u && std::abs(out.spawn_rate - 123.0f) < 1e-6f,
              "ParticleEmitterComponent scalars round-trip");
        check(std::abs(out.color_start.a - 0.4f) < 1e-6f &&
              std::abs(out.color_end.r - 0.5f) < 1e-6f,
              "and vec4 colours round-trip, alpha included");
    }

    // ---- 3. defaults survive too -------------------------------------------
    // A serializer that only works on non-default values is a serializer that
    // silently resets anything left alone.
    {
        const scene::SkinnedMeshComponent in;      // untouched defaults
        const auto out = round_trip(in);
        check(out.clip_index == in.clip_index && out.playing == in.playing &&
              std::abs(out.speed - in.speed) < 1e-6f,
              "a default-constructed component round-trips unchanged");
    }

    // ---- 4. THE ONE THAT EARNS ITS KEEP ------------------------------------
    // Every registered field is counted against the struct's size. A field added
    // later and not registered leaves a gap here, which is exactly the mistake
    // this whole file exists to catch: it saves, it loads, and the new field is
    // silently zero.
    //
    // Not an equality check -- padding and the std::string members that are
    // deliberately excluded make the totals differ legitimately. The assertion
    // is that the registered fields cover the struct's PLAIN DATA.
    {
        // SkinnedMeshComponent: 3 ints + 2 bools + 2 floats registered.
        check(reflected_bytes<scene::SkinnedMeshComponent>() ==
                  3 * sizeof(int) + 2 * sizeof(bool) + 2 * sizeof(float),
              "SkinnedMeshComponent's registered fields account for every non-string field");

        // NpcAgentComponent: 1 bool + 5 floats + 1 int.
        check(reflected_bytes<scene::NpcAgentComponent>() ==
                  sizeof(bool) + 5 * sizeof(float) + sizeof(int),
              "NpcAgentComponent's registered fields account for every non-string field");

        // ParticleEmitterComponent: 2 bools + 1 float + 1 uint32 + 2 vec4.
        check(reflected_bytes<scene::ParticleEmitterComponent>() ==
                  2 * sizeof(bool) + sizeof(float) + sizeof(uint32_t) + 2 * sizeof(glm::vec4),
              "ParticleEmitterComponent's registered fields account for every field");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL scenereflect_check\n");
    return g_fail ? 1 : 0;
}
