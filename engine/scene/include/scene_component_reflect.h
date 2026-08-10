#pragma once

// ============================================================================
// Reflection for the plain-struct scene components — the tractable half of 3.7.
//
// 3.7 asks for the inspector and scene save to run on core reflection instead
// of hand-written code. The blocker is real but PARTIAL: the older scene
// components (Light, Camera, Collider, MeshRenderer, ...) keep private members
// behind accessors, and gws::reflect is offset-based, so reflecting those means
// either exposing 13 types or building property reflection.
//
// The components added for Phase 3 have no such problem. They are plain structs
// with public fields, by design, because a scene has to save and load without a
// GPU. So they can be reflected today, with no refactoring and nothing thrown
// away when the ECS authority flip eventually lands.
//
// WHAT THIS BUYS. Adding SKINNED_PATH to the scene format this session meant
// editing FOUR places by hand: the writer, the key parser, a mirror struct, and
// the apply step. Every one of those is a chance to forget a field, and the
// failure is silent -- a component that saves but does not load. Reflected,
// they are one list.
//
// Transform is deliberately NOT here: it is authored through a Transform object
// with a parent/child hierarchy, not a flat struct, and the serializer already
// handles it specially.
// ============================================================================

#include "mesh_component.h"
#include "npc_agent_component.h"
#include "particle_emitter_component.h"
#include "skinned_mesh_component.h"

#include "reflection/reflection.h"

GWS_REFLECT_BEGIN(schizo::scene::SkinnedMeshComponent)
    GWS_REFLECT_FIELD(clip_index)
    GWS_REFLECT_FIELD(playing)
    GWS_REFLECT_FIELD(speed)
    GWS_REFLECT_FIELD(drive_from_motion)
    GWS_REFLECT_FIELD(idle_clip)
    GWS_REFLECT_FIELD(move_clip)
    GWS_REFLECT_FIELD(move_threshold)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::scene::ParticleEmitterComponent)
    GWS_REFLECT_FIELD(enabled)
    GWS_REFLECT_FIELD(emitting)
    GWS_REFLECT_FIELD(spawn_rate)
    GWS_REFLECT_FIELD(max_particles)
    GWS_REFLECT_FIELD(color_start)
    GWS_REFLECT_FIELD(color_end)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::scene::NpcAgentComponent)
    GWS_REFLECT_FIELD(enabled)
    GWS_REFLECT_FIELD(sight_range)
    GWS_REFLECT_FIELD(sight_fov_deg)
    GWS_REFLECT_FIELD(move_speed)
    GWS_REFLECT_FIELD(patrol_radius)
    GWS_REFLECT_FIELD(attack_range)
    GWS_REFLECT_FIELD(ability_index)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::scene::MeshComponent)
    GWS_REFLECT_FIELD(use_asset_mesh)
GWS_REFLECT_END()

// ---------------------------------------------------------------------------
// Size guards — the tripwire that makes the reflection above stay complete.
//
// A field added to one of these structs and NOT registered above still saves
// and loads without error; it is simply always its default value. That is a
// silent data-loss bug, and it is the exact mistake this file exists to
// prevent.
//
// A runtime test cannot catch it: summing the registered field sizes and
// comparing against a hardcoded expectation is tautological, because adding an
// unregistered field changes neither side. (I wrote that test first, added an
// unregistered field, and watched it pass.) sizeof() is what actually moves.
//
// So: adding ANY field breaks the build here, with a message saying what to do.
// The number is toolchain-specific and that is fine — this is a "did you add a
// field" tripwire, not an ABI guarantee. If it fires after a compiler change,
// register any new fields and update the number.
// ---------------------------------------------------------------------------
static_assert(sizeof(schizo::scene::SkinnedMeshComponent) == 64,
    "SkinnedMeshComponent changed size. If you added a field, register it above "
    "or it will silently fail to save, then update this number.");

static_assert(sizeof(schizo::scene::ParticleEmitterComponent) == 44,
    "ParticleEmitterComponent changed size. If you added a field, register it "
    "above or it will silently fail to save, then update this number.");

static_assert(sizeof(schizo::scene::NpcAgentComponent) == 64,
    "NpcAgentComponent changed size. If you added a field, register it above "
    "or it will silently fail to save, then update this number.");
