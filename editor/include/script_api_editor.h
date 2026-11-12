#pragma once

// Editor-side implementation of the ScriptApi table: marshals script calls
// onto the live scene, the play-mode physics world, GLFW input, and the
// component-driven render/audio properties. Kept separate from ScriptSystem so
// headless tools (tools/script_check) can supply their own minimal table
// without linking the whole editor.

#include "script_api.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct GLFWwindow;
namespace schizo::scene { class Scene; }

namespace schizo::editor { class CommandRegistry; class ExtensionSystem; }

namespace schizo::editor {

class ScenePlaybackManager;
class EcsSceneBridge;

/// Everything the editor ScriptApi needs. main.cpp owns ONE persistent
/// instance (the table captures its address) and refreshes the pointers +
/// mouse delta every frame.
struct EditorScriptCtx {
    std::shared_ptr<schizo::scene::Scene> scene;
    ScenePlaybackManager* playback = nullptr;
    GLFWwindow*           window   = nullptr;
    EcsSceneBridge*       bridge   = nullptr;   // ECS gameplay components (G0–G4)
    float dt = 0.0f;                            // this frame's delta time (s), for continuous gameplay calls
    float mouse_dx = 0.0f, mouse_dy = 0.0f;   // this frame's cursor delta (px)

    // ---- gameplay event feed (ScriptApi::event_count / event_get) ----
    // The bus hands events to `feed_incoming` as it flushes; the first query of
    // a new frame moves them to `feed_current`, which is what scripts read. So
    // every script sees the same list for the whole frame, one frame after the
    // event happened.
    struct FeedEvent {
        std::string name, param;
        uint32_t    instigator = 0, target = 0;
        float       value = 0.0f;
    };
    uint64_t               frame = 0;              // main.cpp bumps this once per frame
    uint64_t               feed_frame = ~0ull;
    EcsSceneBridge*        feed_bridge = nullptr;  // the bridge the feed is subscribed to
    std::vector<FeedEvent> feed_incoming, feed_current;

    // ---- editor extensions (Phase 4.8) ----
    // Filled only for the EDITOR extension api; a gameplay script's table leaves
    // these null and its editor entries unbound.
    //
    // Selection and status are std::function rather than pointers into
    // EditorState because EditorState lives in main.cpp: a pointer to it here
    // would invert the dependency and drag the whole editor into anything that
    // includes this header -- including the headless tools this file exists to
    // keep buildable.
    CommandRegistry*                        commands   = nullptr;
    ExtensionSystem*                        extensions = nullptr;
    std::function<uint32_t()>               get_selection;
    std::function<void(uint32_t)>           set_selection;
    std::function<void(const std::string&)> set_status;
};

/// Fill `api` with the editor implementations bound to `ctx`. Call once; the
/// ctx pointer is captured, so `ctx` must outlive `api`.
void bind_editor_script_api(ScriptApi& api, EditorScriptCtx* ctx);

}  // namespace schizo::editor
