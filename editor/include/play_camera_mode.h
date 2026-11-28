#pragma once
// ============================================================================
// play_camera_mode.h -- which camera the player gets this frame (0.9.1).
// ============================================================================
//
// The player's own Camera Rig says how it is seen (none: first person). What
// it holds and what it is may ask for another, most specific first:
//
//   a tag on the player     camera.first_person, camera.third_person or
//                           camera.orbit (an ability's form, a cutscene)
//   the weapon in its hand  its `camera`: first, third or orbit; a weapon
//                           with a moveset and no `camera` orbits (a blade is
//                           fought in third person)
//   else                    its rig as it is
//
// "third" is over the shoulder (the player faces where the camera looks);
// "orbit" turns the camera round the player (the player turns to where it
// goes). Asked for without a rig of its own, the engine's default rig is used.

#include "ecs/world.h"
#include "ecs/gameplay_camera.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_weapons.h"

#include <string>

namespace schizo::editor {

/// The rig for this frame (null: first person). `scratch` holds a changed copy.
inline const schizo::ecs::CameraRig* play_camera_rig(schizo::ecs::World& w, schizo::ecs::Entity player, const std::string& weapon_id,
                                                     schizo::ecs::CameraRig& scratch) {
    namespace ecs = schizo::ecs;
    if (!w.valid(player)) return nullptr;
    const ecs::CameraRig* own = w.try_get<ecs::CameraRig>(player);
    std::string want;
    if (const ecs::WeaponDef* wd = weapon_id.empty() ? nullptr : ecs::find_weapon(weapon_id))
        want = !wd->camera.empty() ? wd->camera : (!wd->moveset.empty() ? std::string("orbit") : std::string());
    if (const auto* tags = w.try_get<ecs::GameplayTags>(player)) {
        if (tags->has("camera.first_person")) want = "first";
        else if (tags->has("camera.orbit")) want = "orbit";
        else if (tags->has("camera.third_person")) want = "third";
    }
    if (want.empty()) return own;
    if (want == "first") return nullptr;
    // Its own rig's distances when it has one (switched on), else the default rig's.
    scratch = own ? *own : ecs::CameraRig{};
    scratch.third_person = true;
    scratch.orbit = want == "orbit" || (want != "third" && scratch.orbit);
    if (want == "third") scratch.orbit = false;
    if (!own && scratch.orbit) {
        // No rig of its own: the player in the middle, a little farther back (a blade's camera).
        scratch.shoulder = 0.0f;
        scratch.distance = 3.8f;
        scratch.height = 0.6f;
    }
    return &scratch;
}

}  // namespace schizo::editor
