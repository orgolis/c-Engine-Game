// ====================
// assetcook — offline content cooker CLI  (Master Plan Stage 2.2)
// ====================
//
// Scans a source directory, imports each supported asset into the neutral
// ImportedScene, cooks it into a content-addressed .pak bundle, and tracks
// source hashes in a CookDb so re-runs only re-cook what changed (the C2
// "incremental cooker" acceptance). Ties together the whole asset pipeline:
//   importer (obj_import) -> cook (scene_cook/mesh_cook) -> bundle (pak)
//                         -> incremental tracking (cook_db).
//
//   assetcook [source_dir] [out_dir] [db_path]
//   defaults: source=assets/models  out=cooked  db=cooked/cook.db

#include "asset_pipeline/obj_import.h"
#include "asset_pipeline/gltf_import.h"
#include "asset_pipeline/fbx_import.h"
#include "asset_pipeline/usd_import.h"
#include "asset_pipeline/scene_cook.h"
#include "asset_pipeline/texture_cook.h"
#include "asset_pipeline/cook_db.h"
#include "asset_pipeline/pak.h"
#include "asset_pipeline/asset_id.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace schizo::assets;

// Tag_CookedTexture (102) now lives in scene_cook.h (SceneTypeTag) so the
// runtime loader and cooker agree on it.

// Bump when the cook logic changes so everything re-cooks.
// v2: cooked-mesh format gained offline meshlets (CookedMeshHeader magic+version).
// v3: per-asset LZ4 compression in the bundle.
// v4: OBJ .mtl materials + albedo textures embedded in the scene bundle by GUID.
// v5: glTF importer + dependency-graph incremental cook (re-cook on dep change).
static constexpr uint32_t kCookerVersion = 5;

// Per-asset compression codec for cooked bundles (LZ4 = fast runtime decode).
static constexpr gws::compress::Codec kCookCodec = gws::compress::Codec::LZ4;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}
static bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

// Combined content hash of a dependency set (path + file bytes folded in
// order). A missing dep folds a fixed marker so it differs from present, and a
// changed path/content changes the result -> the dependent re-cooks.
static uint64_t hash_deps(const std::vector<std::string>& deps) {
    if (deps.empty()) return 0;
    uint64_t h = 1469598103934665603ull;
    for (const std::string& d : deps) {
        for (unsigned char c : d) { h ^= c; h *= 1099511628211ull; }
        std::vector<uint8_t> bytes = read_file(d);
        const uint64_t part = bytes.empty() ? 0x00D15EA5E5ull
                                            : content_hash(bytes.data(), bytes.size());
        h ^= part; h *= 1099511628211ull;
    }
    return h;
}

// Decode an image file (PNG/JPG/TGA/BMP) and cook it into a BC7 mipped texture
// blob (CookedTextureView format). Returns empty on read/decode failure.
static std::vector<uint8_t> cook_texture_file(const std::string& path) {
    std::vector<uint8_t> src = read_file(path);
    if (src.empty()) return {};
    int w = 0, h = 0, ch = 0;
    unsigned char* pix = stbi_load_from_memory(src.data(), static_cast<int>(src.size()),
                                               &w, &h, &ch, 4);
    if (!pix) return {};
    std::vector<uint8_t> tex = cook_texture(pix, static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h), TexFormat::BC7);
    stbi_image_free(pix);
    return tex;
}

// Stage 2.3 stream-ready format: cook a tiled/page-aligned virtual texture from
// an image file AND self-check it — every tile must be page-aligned and the
// tiles must reconstruct to the exact flat BC cook (lossless re-layout). Returns
// the VT blob (empty on decode failure); fills the out-params.
struct VtCheck { bool ok = false; bool aligned = true; bool reconstructs = true;
                 uint32_t tiles = 0; uint32_t mips = 0; uint32_t tile_texels = 0; };
static std::vector<uint8_t> cook_virtual_texture_file(const std::string& path, VtCheck& chk) {
    std::vector<uint8_t> src = read_file(path);
    if (src.empty()) return {};
    int w = 0, h = 0, ch = 0;
    unsigned char* pix = stbi_load_from_memory(src.data(), static_cast<int>(src.size()), &w, &h, &ch, 4);
    if (!pix) return {};
    const auto uw = static_cast<uint32_t>(w), uh = static_cast<uint32_t>(h);
    std::vector<uint8_t> flat = cook_texture(pix, uw, uh, TexFormat::BC7);
    std::vector<uint8_t> vt   = cook_virtual_texture(pix, uw, uh, TexFormat::BC7);
    stbi_image_free(pix);

    CookedTextureView fv; CookedVTView vv;
    if (!fv.open(flat.data(), flat.size()) || !vv.open(vt.data(), vt.size())) return vt;
    chk.mips = vv.header->mip_count;
    chk.tiles = vv.header->tile_count;
    chk.tile_texels = vv.header->tile_texels;
    const uint32_t bb  = bc_block_bytes(static_cast<TexFormat>(vv.header->format));
    const uint32_t bpt = vv.header->tile_texels / 4;

    const uint8_t* fbase = fv.blocks;
    uint64_t fmip_off = 0;            // flat mip blocks are concatenated mip0..N
    for (uint32_t mi = 0; mi < chk.mips; ++mi) {
        const CookedVTMip& md = vv.mips[mi];
        const uint32_t bw = md.blocks_x, bh = md.blocks_y;
        for (uint32_t ty = 0; ty < md.tiles_y; ++ty)
            for (uint32_t tx = 0; tx < md.tiles_x; ++tx) {
                uint32_t tsz = 0; const uint8_t* tp = vv.tile(mi, tx, ty, tsz);
                if (!tp) { chk.reconstructs = false; continue; }
                if (vv.tiles[md.first_tile + ty * md.tiles_x + tx].offset % vv.header->page_align != 0)
                    chk.aligned = false;
                const uint32_t sbx = tx * bpt, sby = ty * bpt;
                const uint32_t tbw = std::min(bpt, bw - sbx), tbh = std::min(bpt, bh - sby);
                for (uint32_t ly = 0; ly < tbh; ++ly)
                    for (uint32_t lx = 0; lx < tbw; ++lx) {
                        const uint8_t* fa = fbase + fmip_off +
                            ((static_cast<size_t>(sby + ly) * bw) + (sbx + lx)) * bb;
                        if (std::memcmp(fa, tp + (ly * tbw + lx) * bb, bb) != 0)
                            chk.reconstructs = false;
                    }
            }
        fmip_off += static_cast<uint64_t>(bw) * bh * bb;
    }
    chk.ok = chk.aligned && chk.reconstructs;
    return vt;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: logs survive a crash
    const std::string src_dir = argc > 1 ? argv[1] : "assets/models";
    const std::string out_dir = argc > 2 ? argv[2] : "cooked";
    const std::string db_path = argc > 3 ? argv[3] : (out_dir + "/cook.db");

    std::printf("assetcook: src='%s' out='%s' db='%s' (cooker v%u)\n",
                src_dir.c_str(), out_dir.c_str(), db_path.c_str(), kCookerVersion);

    std::error_code ec;
    if (!fs::exists(src_dir, ec)) { std::printf("ERROR: source dir not found\n"); return 1; }
    fs::create_directories(out_dir, ec);

    // Load the incremental DB (absent = empty).
    CookDb db;
    {
        std::vector<uint8_t> blob = read_file(db_path);
        db.load(blob.data(), blob.size());
    }

    auto is_image_ext = [](const std::string& e) {
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp";
    };

    size_t cooked = 0, skipped = 0, failed = 0;
    for (const auto& entry : fs::directory_iterator(src_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(::tolower(c));
        const bool is_obj  = (ext == ".obj");
        const bool is_gltf = (ext == ".gltf" || ext == ".glb");
        const bool is_fbx  = (ext == ".fbx");
        const bool is_usd  = (ext == ".usd" || ext == ".usda" || ext == ".usdc" || ext == ".usdz");
        const bool is_img  = is_image_ext(ext);
        if (!is_obj && !is_gltf && !is_fbx && !is_usd && !is_img) continue;

        const std::string path = entry.path().string();
        std::vector<uint8_t> src = read_file(path);
        if (src.empty()) { std::printf("  FAIL read   %s\n", path.c_str()); ++failed; continue; }
        const uint64_t hash = content_hash(src.data(), src.size());

        // Re-hash this source's PREVIOUSLY-recorded dependencies (cheap file
        // hashes) so a changed dependency forces a re-cook of the dependent —
        // this is what makes "change a texture, re-cook it AND its dependents"
        // work without a separate dependency-graph walk.
        uint64_t prev_dep_hash = 0;
        if (const CookRecord* r = db.find(path)) prev_dep_hash = hash_deps(r->deps);

        if (!db.needs_cook(path, hash, kCookerVersion, prev_dep_hash)) {
            std::printf("  skip        %s\n", path.c_str());
            ++skipped;
            continue;
        }

        const std::string out_path = out_dir + "/" + path_stem(path) +
                                     (is_img ? ".tex.pak" : ".pak");

        if (is_obj || is_gltf || is_fbx || is_usd) {
            ImportedScene scene;
            bool imported = false;
            try {
                imported = is_obj  ? import_obj(path, scene)
                         : is_gltf ? import_gltf(path, scene)
                         : is_fbx  ? import_fbx(path, scene)
                                   : import_usd(path, scene);
            } catch (const std::exception& e) {
                std::printf("  EXCEPTION   %s: %s\n", path.c_str(), e.what()); ++failed; continue;
            }
            if (!imported) { std::printf("  FAIL import %s\n", path.c_str()); ++failed; continue; }

            // Cook the material-referenced albedo textures into the SAME bundle,
            // keyed by their GUID, so the material->texture link survives cook.
            std::vector<BundleAsset>        extras;
            std::unordered_set<std::string> seen_tex;
            size_t linked_tex = 0;
            for (const CookedMaterial& m : scene.materials) {
                const std::string tex = m.albedo_tex;   // char[128], NUL-terminated
                if (tex.empty() || !seen_tex.insert(tex).second) continue;
                std::vector<uint8_t> tb = cook_texture_file(tex);
                if (tb.empty()) {
                    std::printf("    (albedo texture missing/undecodable: %s)\n", tex.c_str());
                    continue;
                }
                const AssetId tid = asset_id_from_path(tex);
                std::printf("    + albedo tex %s -> guid %016llx (%zu B BC7)\n",
                            tex.c_str(), static_cast<unsigned long long>(tid), tb.size());
                extras.push_back(BundleAsset{ tid, Tag_CookedTexture, std::move(tb) });
                ++linked_tex;
            }

            std::vector<uint8_t> bundle = cook_scene(scene, kCookCodec, extras);
            if (!write_file(out_path, bundle)) { std::printf("  FAIL write  %s\n", out_path.c_str()); ++failed; continue; }
            db.record(path, hash, asset_id_from_path(path), Tag_SceneManifest,
                      kCookerVersion, scene.deps, hash_deps(scene.deps));
            size_t vtx = 0;
            for (const auto& mm : scene.meshes) vtx += mm.vertices.size() / mm.vertex_stride;
            std::printf("  COOK mesh   %s -> %s (%zu mesh, %zu mat, %zu tex, %zu deps, %zu verts, %zu bytes)\n",
                        path.c_str(), out_path.c_str(), scene.meshes.size(),
                        scene.materials.size(), linked_tex, scene.deps.size(), vtx, bundle.size());
        } else {  // standalone image -> BC7 mipped texture bundle
            std::vector<uint8_t> tex = cook_texture_file(path);
            if (tex.empty()) { std::printf("  FAIL decode %s\n", path.c_str()); ++failed; continue; }
            PakWriter tw;
            tw.add(asset_id_from_path(path), Tag_CookedTexture, tex, kCookCodec);
            std::vector<uint8_t> bundle = tw.finalize();
            if (!write_file(out_path, bundle)) { std::printf("  FAIL write  %s\n", out_path.c_str()); ++failed; continue; }
            db.record(path, hash, asset_id_from_path(path), Tag_CookedTexture, kCookerVersion);
            std::printf("  COOK tex    %s -> %s (%zu bytes BC7)\n",
                        path.c_str(), out_path.c_str(), bundle.size());

            // Stage 2.3: also emit a STREAM-READY virtual texture — a tiled,
            // page-aligned `.vt` the runtime can page one tile at a time (the
            // Stage-8 streamer mmaps it; here we fix + verify the format).
            VtCheck chk;
            std::vector<uint8_t> vt = cook_virtual_texture_file(path, chk);
            if (!vt.empty()) {
                const std::string vt_path = out_dir + "/" + path_stem(path) + ".vt";
                write_file(vt_path, vt);
                std::printf("  COOK vt     %s -> %s (%u mips, %u %u-px tiles, page-aligned %s, "
                            "reconstructs %s, %zu bytes)\n",
                            path.c_str(), vt_path.c_str(), chk.mips, chk.tiles, chk.tile_texels,
                            chk.aligned ? "OK" : "FAIL", chk.reconstructs ? "OK" : "FAIL", vt.size());
            }
        }
        ++cooked;
    }

    // Persist the updated DB.
    if (!write_file(db_path, db.save()))
        std::printf("WARNING: failed to write cook db '%s'\n", db_path.c_str());

    std::printf("assetcook done: %zu cooked, %zu skipped, %zu failed, %zu db records\n",
                cooked, skipped, failed, db.size());
    return failed == 0 ? 0 : 1;
}
