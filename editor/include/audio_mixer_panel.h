#pragma once
// ============================================================================
// audio_mixer_panel — the Audio Mixer (Phase 4.9)
//
// Faders, mute/solo and post-fader meters over the Mixer's bus table. There is
// exactly ONE bus table in the process: this panel edits gws::audio::Mixer's own
// BusGraph rather than keeping an editor-side copy to sync. A second copy is how
// a panel and the sound it claims to control drift apart, and the drift only
// shows up as "the mixer does nothing", which is nearly unreportable.
//
// The bus LAYOUT is a project document, not a scene one: a game's SFX/Music/UI
// split belongs to the game, and binding it to a scene would mean every scene
// carried its own conflicting mixer. It saves next to the project manifest.
//
// Saving happens on GESTURE END, not per frame. A fader held for two seconds
// would otherwise rewrite the file about 120 times -- the same trap recorded for
// the v0.7.17 prefab inspector, and the same fix.
// ============================================================================

#include <string>

namespace gws::audio { class AudioEngine; }

namespace schizo::editor {

/// Draw the mixer window. `open` is the show/hide flag the Window menu toggles.
void ShowAudioMixer(bool* open, gws::audio::AudioEngine& audio);

/// Write the bus layout to the active project. Returns false and fills `err`
/// when there is no project open or the write fails.
bool save_audio_buses(gws::audio::AudioEngine& audio, std::string* err = nullptr);

/// Load the bus layout for the active project. Returns false when no layout file
/// exists -- which is NOT an error: the defaults seeded by BusGraph are a valid
/// starting mixer, and a project that never opened this panel must still play.
bool load_audio_buses(gws::audio::AudioEngine& audio, std::string* err = nullptr);

}  // namespace schizo::editor
