// ============================================================================
// assetpath_check — assets whose names are not ASCII must still load.
//
// Found by measuring, not by reading: cooking assets/models reported
// "FAIL read assets/models/Главный стол+ПК.obj" — a 64 MB model that simply
// could not be opened. The cause was `std::ifstream f(path)` with a narrow
// std::string, which Windows interprets in the ANSI codepage, so any path
// outside it fails to open. The engine already knew this (the texture manager
// carries a comment about exactly this failure) but the asset pipeline had
// three copies of the unfixed version.
//
// This is worth a permanent check rather than a one-time fix because of HOW it
// fails: not a crash, not a wrong result — a plain "could not read" on a file
// that is plainly there. A developer outside the English-speaking world hits it
// on their first import and has no way to tell it is a bug in the engine rather
// than something wrong with their file. Silent, locale-dependent breakage is
// precisely what a regression test is for, and no ASCII-named test asset can
// ever catch it.
// ============================================================================
#include "asset_pipeline/obj_import.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// A minimal but complete OBJ: one triangle with a material group, which is what
// import_obj requires before it will report success.
const char* kTriangleObj =
    "mtllib t.mtl\n"
    "v 0.0 0.0 0.0\n"
    "v 1.0 0.0 0.0\n"
    "v 0.0 1.0 0.0\n"
    "vn 0.0 0.0 1.0\n"
    "usemtl mat\n"
    "f 1//1 2//1 3//1\n";

bool write_utf8(const fs::path& p, const char* text) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

// Round-trip a UTF-8 std::string through fs::path the way the importers do.
std::string u8str(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}  // namespace

int main() {
    std::printf("=== assetpath_check ===\n\n");

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "gws_assetpath_check";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // Names drawn from scripts a real user might have on disk. Each one would
    // have failed before the fix, on a machine whose ANSI codepage cannot
    // represent it.
    struct Case { const char* label; const char8_t* name; };
    const Case cases[] = {
        {"ASCII (the control)",  u8"plain_model.obj"},
        {"Cyrillic",             u8"Главный стол.obj"},
        {"CJK",                  u8"モデル_01.obj"},
        {"accented Latin",       u8"modèle_à_côté.obj"},
        {"emoji",                u8"model_🚀.obj"},
    };

    for (const auto& c : cases) {
        const fs::path p = dir / fs::path(std::u8string(c.name));
        if (!write_utf8(p, kTriangleObj)) {
            check(false, std::string(c.label) + ": could not create the test file");
            continue;
        }

        // Go through a UTF-8 std::string, which is how every caller reaches the
        // importer. Passing fs::path directly would test a path no caller uses.
        schizo::assets::ImportedScene scene;
        const bool ok = schizo::assets::import_obj(u8str(p), scene);
        check(ok, std::string(c.label) + ": import_obj opens the file");
        if (ok)
            check(!scene.meshes.empty(),
                  std::string(c.label) + ": geometry actually parsed");
    }

    // A missing file must still fail — the fix must not turn every path into a
    // success, which is the way a fix like this goes wrong.
    {
        schizo::assets::ImportedScene scene;
        const bool ok = schizo::assets::import_obj(
            u8str(dir / fs::path(std::u8string(u8"нет_такого.obj"))), scene);
        check(!ok, "a genuinely missing non-ASCII path still fails");
    }

    fs::remove_all(dir, ec);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL assetpath_check\n");
    return g_fail ? 1 : 0;
}
