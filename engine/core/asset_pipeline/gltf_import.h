#pragma once

// ====================
// glTF Importer  (Pillar C1 / Master Plan Stage 2.1)
// ====================
//
// Parses a glTF 2.0 file (.gltf + external .bin, or self-contained .glb) into
// the neutral ImportedScene the cooker consumes — geometry in the canonical
// ImportedVertex layout, PBR materials (base color / metallic / roughness +
// base-color texture reference), and the baked node hierarchy.
//
// The declaration lives here; the implementation (gltf_import.cpp) is the only
// TU that includes the in-tree tinygltf, so its header-only IMPLEMENTATION is
// emitted exactly once. Link the `tinygltf` interface target into whatever
// builds gltf_import.cpp (the assetcook tool).
//
//   ImportedScene scene;
//   if (import_gltf("model.gltf", scene)) { auto pak = cook_scene(scene); ... }

#include "imported_scene.h"

#include <string>

namespace schizo::assets {

// Import a .gltf/.glb into `out`. Returns false on parse failure or when no
// geometry could be read (e.g. a .gltf whose external .bin / data-URI buffer is
// missing — tinygltf only resolves external .bin and embedded .glb buffers).
bool import_gltf(const std::string& path, ImportedScene& out);

}  // namespace schizo::assets
