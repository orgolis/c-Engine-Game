#include "audio_mixer_panel.h"

#include "project_paths.h"

#include "audio/audio_engine.h"
#include "audio/bus.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using gws::audio::Bus;
using gws::audio::BusGraph;
using gws::audio::BusId;
using gws::audio::kInvalidBus;
using gws::audio::kMasterBus;
using gws::audio::kMaxBuses;

namespace schizo::editor {
namespace {

constexpr const char* kLayoutFile = "audio_buses.txt";

fs::path layout_path() { return project_root() / kLayoutFile; }

// Meter ballistics. A raw per-block peak flickers too fast to read, so the
// displayed value jumps up instantly and falls slowly -- the behaviour every
// hardware meter has, and the reason a level that is only occasionally too hot
// is still visible.
float g_meter[kMaxBuses] = {};

void decay_meters(gws::audio::AudioEngine& audio) {
    const BusGraph& bg = audio.mixer().buses();
    for (size_t i = 0; i < bg.count(); ++i) {
        const float peak = audio.mixer().bus_peak(static_cast<BusId>(i));
        float&      m    = g_meter[i];
        m = peak > m ? peak : m * 0.90f;
        if (m < 1e-4f) m = 0.0f;
    }
}

// Push one bus's mixable fields across the command queue.
void commit(gws::audio::AudioEngine& audio, BusId id, const Bus& b) {
    audio.mixer().set_bus(id, b);
}

void draw_meter(float level) {
    // Drawn rather than a ProgressBar so clipping (>1.0) can be red: the one
    // thing a meter exists to tell you is that you are over, and a bar that
    // simply saturates at full says "loud" and "broken" identically.
    const ImVec2 size(14.0f, 120.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 32, 255));
    const float  clamped = std::min(level, 1.0f);
    const float  h       = size.y * clamped;
    const ImU32  col     = level >= 1.0f   ? IM_COL32(220,  60,  60, 255)
                         : level >= 0.75f  ? IM_COL32(220, 190,  60, 255)
                                           : IM_COL32( 70, 190, 110, 255);
    if (h > 0.0f) dl->AddRectFilled(ImVec2(p0.x, p1.y - h), p1, col);
    dl->AddRect(p0, p1, IM_COL32(80, 80, 88, 255));
    ImGui::Dummy(size);
}

}  // namespace

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool save_audio_buses(gws::audio::AudioEngine& audio, std::string* err) {
    const fs::path path = layout_path();
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write " + path.string();
        return false;
    }

    const BusGraph& bg = audio.mixer().buses();
    out << "# GameWorldshaper audio bus layout (Phase 4.9)\n";
    out << "# BUS <parent> <gain> <mute> <solo> <name>   -- name is last so it may contain spaces\n";
    for (size_t i = 0; i < bg.count(); ++i) {
        const Bus* b = bg.bus(static_cast<BusId>(i));
        if (!b) continue;
        out << "BUS " << static_cast<int>(b->parent) << ' ' << b->gain << ' '
            << (b->mute ? 1 : 0) << ' ' << (b->solo ? 1 : 0) << ' ' << b->name << "\n";
    }
    return true;
}

bool load_audio_buses(gws::audio::AudioEngine& audio, std::string* err) {
    const fs::path path = layout_path();
    std::ifstream in(path);
    if (!in) {
        if (err) *err = "no layout file";
        return false;   // not an error: the seeded defaults are a valid mixer
    }

    // Parse fully BEFORE touching the live table. A half-applied layout would
    // leave the running mixer in a state that is neither the file's nor the
    // defaults', and the audio thread is reading it the whole time.
    struct Row { int parent; float gain; int mute, solo; std::string name; };
    std::vector<Row> rows;
    std::string      line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string        tag;
        Row                r{};
        if (!(ss >> tag) || tag != "BUS") continue;
        if (!(ss >> r.parent >> r.gain >> r.mute >> r.solo)) continue;
        ss >> std::ws;
        std::getline(ss, r.name);
        if (r.name.empty()) continue;
        rows.push_back(std::move(r));
        if (rows.size() >= kMaxBuses) break;
    }
    if (rows.empty()) {
        if (err) *err = "layout file has no buses";
        return false;
    }

    BusGraph& bg = audio.mixer().buses_setup();
    bg.reset_to_defaults();
    // Rebuild from row 0. Master is index 0 by construction, so the file's first
    // row configures it rather than adding a second root.
    while (bg.count() > 1) bg.remove_last();
    if (Bus* m = bg.bus(kMasterBus)) {
        m->name = rows[0].name;
        m->gain = rows[0].gain;
        m->mute = rows[0].mute != 0;
        m->solo = rows[0].solo != 0;
    }
    for (size_t i = 1; i < rows.size(); ++i) {
        // A parent that is not yet defined would dangle, so it clamps to Master.
        // Files are written parent-before-child; a hand-edited one that is not
        // still loads, with a visible parent rather than a silent cycle.
        const BusId parent = static_cast<BusId>(rows[i].parent) < bg.count()
                                 ? static_cast<BusId>(rows[i].parent) : kMasterBus;
        const BusId id = bg.add(rows[i].name, parent);
        if (id == kInvalidBus) break;
        Bus* b = bg.bus(id);
        b->gain = rows[i].gain;
        b->mute = rows[i].mute != 0;
        b->solo = rows[i].solo != 0;
    }
    spdlog::info("[audio] bus layout loaded: {} bus(es) from {}", bg.count(), path.string());
    return true;
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void ShowAudioMixer(bool* open, gws::audio::AudioEngine& audio) {
    if (!*open) return;

    ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio Mixer", open)) { ImGui::End(); return; }

    decay_meters(audio);

    BusGraph&  bg          = audio.mixer().buses_setup();
    bool       layout_dirty = false;   // structure changed -> save now
    bool       value_dirty  = false;   // a gesture ended    -> save now

    if (bg.any_solo()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                           "SOLO active - buses that are not soloed are silent");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear solo")) {
            for (size_t i = 0; i < bg.count(); ++i) {
                Bus* b = bg.bus(static_cast<BusId>(i));
                if (b && b->solo) { b->solo = false; commit(audio, static_cast<BusId>(i), *b); }
            }
            value_dirty = true;
        }
        ImGui::Separator();
    }

    const size_t n = bg.count();
    for (size_t i = 0; i < n; ++i) {
        Bus* b = bg.bus(static_cast<BusId>(i));
        if (!b) continue;
        const BusId id = static_cast<BusId>(i);

        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();

        ImGui::TextUnformatted(b->name.c_str());
        // Show the routing, because a submix that looks quiet is usually a
        // parent problem and the panel should not make you guess the tree.
        if (id != kMasterBus) {
            const Bus* parent = bg.bus(b->parent);
            ImGui::TextDisabled("-> %s", parent ? parent->name.c_str() : "?");
        } else {
            ImGui::TextDisabled("output");
        }

        draw_meter(g_meter[i]);
        ImGui::SameLine();
        if (ImGui::VSliderFloat("##gain", ImVec2(24, 120), &b->gain, 0.0f, 2.0f, "")) {
            b->gain = std::max(0.0f, b->gain);
            commit(audio, id, *b);          // audible immediately...
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) value_dirty = true;   // ...saved once, at the end
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %.2f (%.1f dB)", b->name.c_str(), b->gain,
                              b->gain > 0.0001f ? 20.0f * std::log10(b->gain) : -99.0f);

        const bool muted = b->mute;
        if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.20f, 1.0f));
        if (ImGui::SmallButton("M")) { b->mute = !b->mute; commit(audio, id, *b); value_dirty = true; }
        if (muted) ImGui::PopStyleColor();
        ImGui::SameLine();
        const bool soloed = b->solo;
        if (soloed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.65f, 0.20f, 1.0f));
        if (ImGui::SmallButton("S")) { b->solo = !b->solo; commit(audio, id, *b); value_dirty = true; }
        if (soloed) ImGui::PopStyleColor();

        // Master has no parent to choose and must not be re-rooted.
        if (id != kMasterBus) {
            ImGui::SetNextItemWidth(70);
            const Bus* cur = bg.bus(b->parent);
            if (ImGui::BeginCombo("##parent", cur ? cur->name.c_str() : "?", ImGuiComboFlags_HeightSmall)) {
                for (size_t j = 0; j < n; ++j) {
                    if (j == i) continue;              // a bus cannot parent itself
                    const Bus* cand = bg.bus(static_cast<BusId>(j));
                    if (!cand) continue;
                    if (ImGui::Selectable(cand->name.c_str(), b->parent == j)) {
                        b->parent = static_cast<BusId>(j);
                        layout_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::EndGroup();
        ImGui::PopID();
        if (i + 1 < n) ImGui::SameLine(0.0f, 18.0f);
    }

    ImGui::Separator();
    static char new_name[48] = "";
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##newbus", "new bus name", new_name, sizeof(new_name));
    ImGui::SameLine();
    const bool can_add = new_name[0] != '\0' && bg.count() < kMaxBuses;
    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button("Add Bus") && can_add) {
        if (bg.find(new_name) != kInvalidBus) {
            spdlog::warn("[audio] a bus named '{}' already exists", new_name);
        } else if (bg.add(new_name, kMasterBus) != kInvalidBus) {
            layout_dirty = true;
            new_name[0]  = '\0';
        }
    }
    ImGui::EndDisabled();
    if (bg.count() >= kMaxBuses) {
        ImGui::SameLine();
        ImGui::TextDisabled("(bus limit reached)");
    }

    if (layout_dirty || value_dirty) {
        std::string err;
        if (!save_audio_buses(audio, &err))
            spdlog::warn("[audio] could not save the bus layout: {}", err);
    }

    ImGui::End();
}

}  // namespace schizo::editor
