#pragma once

// ====================
// FBX Importer  (Pillar C1 / Master Plan Stage 2.1)
// ====================
//
// Parses an FBX file (binary or ASCII, FBX 6100–7x00) into the neutral
// ImportedScene the cooker consumes — geometry in the canonical ImportedVertex
// layout, PBR materials (base color / metallic / roughness + base-color texture
// reference), and the baked node hierarchy.
//
// Backed by the in-tree single-file `ufbx` loader (third_party/ufbx). The plan
// names Assimp for FBX breadth; ufbx was chosen instead — a zero-dependency,
// FBX-purpose-built single TU that vendors + builds cleanly on the MinGW
// toolchain (Assimp's full build is heavy/fragile here). It covers the C1 FBX
// requirement; broader format coverage (USD, etc.) can add Assimp later.
//
// The declaration lives here; the implementation (fbx_import.cpp) is the only
// TU that includes ufbx. Link the `ufbx` target into whatever builds it.

#include "imported_scene.h"

#include <string>

namespace schizo::assets {

// Import a .fbx into `out`. Returns false on parse failure or when no geometry
// could be read.
bool import_fbx(const std::string& path, ImportedScene& out);

}  // namespace schizo::assets
