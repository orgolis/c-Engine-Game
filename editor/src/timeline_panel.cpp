// ============================================================================
// timeline_panel — the sequencer's surface (4.4).
//
// The model is in sequence.h and tested there. This is playback control, track
// rows, the playhead, and applying evaluated values to the scene.
//
// Applying is the part with a trap in it. A timeline that writes entity
// transforms every frame will overwrite whatever the user is doing with the
// gizmo, so playback and editing fight each other and the loser is whoever
// touched the mouse last. Values are therefore only applied while the timeline
// is PLAYING or being SCRUBBED -- when it is idle the scene belongs to the
// editor.
// ============================================================================
#include "timeline_panel.h"

#include "curve_editor.h"
#include "sequence.h"

#include "entity.h"
#include "scene.h"
#include "transform.h"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace schizo::editor {

namespace {

void apply_value(const std::shared_ptr<scene::Scene>& scene, const TrackValue& v) {
    if (!scene) return;
    auto e = scene->GetEntityById(v.entity_id);
    if (!e) return;
    auto* t = e->GetTransform();
    if (!t) return;

    glm::vec3 p = t->GetLocalPosition();
    glm::vec3 s = t->GetLocalScale();
    glm::vec3 euler = glm::degrees(glm::eulerAngles(t->GetLocalRotation()));

    switch (v.target) {
        case TrackTarget::PositionX: p.x = v.value; t->SetLocalPosition(p); break;
        case TrackTarget::PositionY: p.y = v.value; t->SetLocalPosition(p); break;
        case TrackTarget::PositionZ: p.z = v.value; t->SetLocalPosition(p); break;
        case TrackTarget::ScaleUniform:
            // Uniform rather than per-axis: non-uniform scale animation is
            // rarely what anyone wants and quadruples the track count.
            t->SetLocalScale(glm::vec3(v.value));
            break;
        case TrackTarget::RotationYaw:
        case TrackTarget::RotationPitch:
        case TrackTarget::RotationRoll: {
            if (v.target == TrackTarget::RotationYaw)   euler.y = v.value;
            if (v.target == TrackTarget::RotationPitch) euler.x = v.value;
            if (v.target == TrackTarget::RotationRoll)  euler.z = v.value;
            t->SetLocalRotation(glm::quat(glm::radians(euler)));
            break;
        }
        default: break;
    }
    (void)s;
}

}  // namespace

void draw_timeline_panel(bool& open,
                         Sequence& seq,
                         TimelineState& st,
                         const std::shared_ptr<scene::Scene>& scene,
                         uint32_t selected_entity_id,
                         float delta_time) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(880.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Timeline", &open)) { ImGui::End(); return; }

    // ---- transport ---------------------------------------------------------
    if (ImGui::Button(st.playing ? "Pause" : "Play")) st.playing = !st.playing;
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { st.playing = false; st.time = 0.0f; st.scrubbing = true; }
    ImGui::SameLine();
    ImGui::Checkbox("loop", &st.loop);
    ImGui::SameLine();

    float dur = seq.duration();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("duration", &dur, 0.05f, 0.1f, 600.0f)) seq.set_duration(dur);

    ImGui::SetNextItemWidth(-1.0f);
    // Dragging the playhead is scrubbing, and scrubbing must apply values --
    // otherwise the scene does not move while you drag and the timeline looks
    // dead.
    if (ImGui::SliderFloat("##time", &st.time, 0.0f, seq.duration(), "%.2f s"))
        st.scrubbing = true;

    if (st.playing) st.time = seq.advance(st.time, delta_time, st.loop);

    // ---- apply, but only when the timeline is driving -----------------------
    if (st.playing || st.scrubbing) {
        for (const TrackValue& v : seq.evaluate(st.time)) apply_value(scene, v);
        st.scrubbing = false;
    }

    ImGui::Separator();

    // ---- add a track for the selection -------------------------------------
    {
        auto sel = scene ? scene->GetEntityById(selected_entity_id) : nullptr;
        if (sel) {
            ImGui::Text("Selected: %s", sel->GetName().c_str());
            ImGui::SameLine();
            static int target = 0;
            ImGui::SetNextItemWidth(140.0f);
            const char* names[] = {"Position X", "Position Y", "Position Z",
                                   "Rotation Yaw", "Rotation Pitch", "Rotation Roll", "Scale"};
            ImGui::Combo("##target", &target, names, IM_ARRAYSIZE(names));
            ImGui::SameLine();
            if (ImGui::Button("Add track"))
                seq.add_track(selected_entity_id, static_cast<TrackTarget>(target));
            ImGui::SameLine();
            // Keying the CURRENT value at the CURRENT time is the gesture people
            // expect from a timeline: pose the object, press the key.
            if (ImGui::Button("Key current value")) {
                Track& tr = seq.add_track(selected_entity_id, static_cast<TrackTarget>(target));
                auto* t = sel->GetTransform();
                const glm::vec3 p = t->GetLocalPosition();
                const glm::vec3 e = glm::degrees(glm::eulerAngles(t->GetLocalRotation()));
                float v = 0.0f;
                switch (static_cast<TrackTarget>(target)) {
                    case TrackTarget::PositionX:     v = p.x; break;
                    case TrackTarget::PositionY:     v = p.y; break;
                    case TrackTarget::PositionZ:     v = p.z; break;
                    case TrackTarget::RotationYaw:   v = e.y; break;
                    case TrackTarget::RotationPitch: v = e.x; break;
                    case TrackTarget::RotationRoll:  v = e.z; break;
                    default:                         v = t->GetLocalScale().x; break;
                }
                tr.curve.add({st.time, v, 0.0f, 0.0f});
            }
        } else {
            ImGui::TextDisabled("Select an entity to add a track for it.");
        }
    }

    ImGui::Separator();

    // ---- tracks ------------------------------------------------------------
    if (seq.tracks().empty()) {
        ImGui::TextDisabled("No tracks yet. Select an entity, choose a channel, "
                            "and press Add track or Key current value.");
    }

    int to_remove = -1;
    for (size_t i = 0; i < seq.tracks().size(); ++i) {
        Track& tr = seq.tracks()[i];
        ImGui::PushID(static_cast<int>(i));

        auto ent = scene ? scene->GetEntityById(tr.entity_id) : nullptr;
        const std::string label =
            (ent ? ent->GetName() : ("entity " + std::to_string(tr.entity_id))) +
            "  ·  " + track_target_name(tr.target);

        ImGui::Checkbox("##en", &tr.enabled);
        ImGui::SameLine();
        const bool opened = ImGui::CollapsingHeader(label.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        if (ImGui::SmallButton("del")) to_remove = static_cast<int>(i);

        if (opened) {
            // The SAME curve editor the material and VFX work will use, on the
            // same Curve type the runtime evaluates. A second curve widget here
            // would ease differently from what plays back.
            static int sel_key = -1;
            ImGui::TextDisabled("%zu key(s) — right-click the graph to add one",
                                tr.curve.size());
            draw_curve_editor("##track", tr.curve, 110.0f, &sel_key);

            // A marker showing where the playhead sits relative to the keys is
            // the difference between editing a curve and editing THIS curve at
            // THIS time.
            ImGui::TextDisabled("playhead %.2fs of %.2fs", st.time, seq.duration());
        }
        ImGui::PopID();
    }
    if (to_remove >= 0) seq.remove_track(static_cast<size_t>(to_remove));

    ImGui::End();
}

}  // namespace schizo::editor
