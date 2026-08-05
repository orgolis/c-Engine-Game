#pragma once

// Generic, reflection-driven inspector for an entity's authorable ECS components
// (F2). Lives in its own translation unit so main.cpp needs no EnTT / ECS include.

namespace schizo::scene { class Transform; }

namespace schizo::editor {

class EcsSceneBridge;

// Render an "ECS Components" section for the OOP entity that owns `tf`: lists its
// authorable gameplay components (Health, Ability State, …), edits each one's
// fields generically via core reflection, and offers Add / Remove. Returns true
// if anything changed this frame.
bool draw_ecs_component_inspector(EcsSceneBridge& bridge, schizo::scene::Transform* tf);

}  // namespace schizo::editor
