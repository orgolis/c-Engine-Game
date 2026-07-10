#pragma once

// Editor-side implementation of the ScriptApi table: marshals script calls
// onto the live scene, the play-mode physics world, GLFW input, and the
// component-driven render/audio properties. Kept separate from ScriptSystem so
// headless tools (tools/script_check) can supply their own minimal table
// without linking the whole editor.

#include "script_api.h"

#include <memory>

struct GLFWwindow;
namespace schizo::scene { class Scene; }

namespace schizo::editor {

class ScenePlaybackManager;

/// Everything the editor ScriptApi needs. main.cpp owns ONE persistent
/// instance (the table captures its address) and refreshes the pointers +
/// mouse delta every frame.
struct EditorScriptCtx {
    std::shared_ptr<schizo::scene::Scene> scene;
    ScenePlaybackManager* playback = nullptr;
    GLFWwindow*           window   = nullptr;
    float mouse_dx = 0.0f, mouse_dy = 0.0f;   // this frame's cursor delta (px)
};

/// Fill `api` with the editor implementations bound to `ctx`. Call once; the
/// ctx pointer is captured, so `ctx` must outlive `api`.
void bind_editor_script_api(ScriptApi& api, EditorScriptCtx* ctx);

}  // namespace schizo::editor
