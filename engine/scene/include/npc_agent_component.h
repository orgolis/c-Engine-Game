#pragma once

// ============================================================================
// NpcAgentComponent — makes an entity an AI agent.
//
// Data only, like the other entity components: the behaviour tree, blackboard
// and path follower are runtime state and live editor-side keyed by entity.
// ============================================================================

#include <glm/glm.hpp>

#include <string>

namespace schizo::scene {

struct NpcAgentComponent {
    /// Off means "not an agent" — the check the tick uses.
    bool  enabled = false;

    /// Who to look for. A name rather than an id so it survives save/load and
    /// can be authored before the target exists.
    std::string target_name = "Player";

    // Perception, fed to schizo::ecs::can_see.
    float sight_range   = 20.0f;
    float sight_fov_deg = 100.0f;

    /// Movement speed in world units/second, applied along the navmesh path.
    float move_speed = 3.5f;

    /// Radius of the patrol loop walked when nothing is visible. Patrolling
    /// rather than standing still is what makes "the AI is running" visible
    /// without needing something to chase.
    float patrol_radius = 8.0f;

    bool active() const { return enabled; }
};

}  // namespace schizo::scene
