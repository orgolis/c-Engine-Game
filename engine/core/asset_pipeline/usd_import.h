#pragma once

// ====================
// USD Importer  (Pillar C1 / Master Plan Stage 2.1)
// ====================
//
// Parses a USD file (.usd / .usda ASCII / .usdc Crate / .usdz zip) into the
// neutral ImportedScene the cooker consumes — geometry in the canonical
// ImportedVertex layout, UsdPreviewSurface materials (diffuse / metallic /
// roughness + base-color texture reference), and the baked node hierarchy.
//
// Backed by the in-tree `tinyusdz` loader + its Tydra render-data converter
// (third_party/tinyusdz). The declaration lives here; the implementation
// (usd_import.cpp) is the only TU that includes tinyusdz. Link `tinyusdz_static`
// into whatever builds it.

#include "imported_scene.h"

#include <string>

namespace schizo::assets {

// Import a .usd/.usda/.usdc/.usdz into `out`. Returns false on parse failure or
// when no geometry could be read.
bool import_usd(const std::string& path, ImportedScene& out);

}  // namespace schizo::assets
