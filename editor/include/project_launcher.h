#pragma once
// ============================================================================
// Project launcher UI — the first screen the editor shows: pick a recent
// project, create a new one (choosing which features it uses), or open one.
// Decoupled from the editor's internal state: it operates on the project model
// and returns the chosen project; main.cpp does the actual loading.
// ============================================================================
#include "project.h"

namespace schizo::project {

// Draws the fullscreen launcher over the whole window. Returns true ONCE, on the
// frame a project is chosen to open (its manifest is loaded into `out`). Sets
// `quit` if the user asked to exit. `registry` (recent list) is updated + saved
// on create/open.
bool draw_launcher(ProjectsRegistry& registry, ProjectManifest& out, bool& quit);

// Draws a "Features" window that edits `features` in place (used by the launcher's
// New tab and by the in-editor Project Settings). Returns true if any toggle
// changed this frame. `p_open`, if non-null, controls window visibility.
bool draw_feature_settings(FeatureSet& features, bool* p_open);

} // namespace schizo::project
