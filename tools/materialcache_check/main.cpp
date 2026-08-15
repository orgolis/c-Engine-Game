// ============================================================================
// materialcache_check — what happens when a material does NOT load.
//
// material_check covers the .mat FORMAT. This covers the cache the editor puts
// in front of it, where the interesting behaviours are all failure and reload
// behaviours — the ones a person notices as "the editor is being weird" rather
// than as an error:
//
//   - A path that fails to load must keep failing (not retry every frame and
//     spam the log at 60 Hz), must report nullptr rather than a default material
//     (a substituted white surface for a typo is the failure this whole feature
//     was built to make impossible), and must STILL be watched, so creating the
//     file later fixes the scene without a restart.
//
//   - A save that rewrites identical bytes must report "nothing changed".
//     Otherwise every no-op write rebuilds a GPU material and idles the device,
//     and clicking Save twice costs a visible hitch.
//
//   - set() must update memory WITHOUT touching the file. That split is what
//     lets a slider drag update the viewport every frame while writing the file
//     once, on release.
//
//   - A material that was loading fine and then disappears must be reported.
//     Silently keeping the last good copy would mean a scene referencing a
//     deleted material looks correct until the next restart, and then does not.
//
// Time is injected into poll_reload_at rather than slept, so the settle window
// is exercised exactly instead of flakily.
// ============================================================================
#include "material_asset_cache.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using gws::assets::MaterialDesc;
using schizo::editor::MaterialAssetCache;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

fs::path g_dir;

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

/// Force a strictly increasing mtime. Filesystem timestamp granularity is coarse
/// enough on some Windows configurations that two writes inside one millisecond
/// compare equal. The watcher also compares SIZE — but two edits to the same
/// material very often produce the same byte count ("METALLIC=1" → "METALLIC=0"),
/// so a helper that leaned on size would pass while testing nothing. The counter
/// guarantees each write looks newer than the last regardless of clock
/// granularity or content length.
void touch_distinct(const fs::path& p, const std::string& body) {
    static int bump = 0;
    write_file(p, body);
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(++bump * 2));
}

/// Drive the watcher past its settle window. Anything comfortably larger than
/// the settle interval works; the exact value is not the property under test.
size_t settle_and_poll(MaterialAssetCache& c, double& clock) {
    size_t changed = 0;
    for (int i = 0; i < 8; ++i) {
        clock += 1.0;
        changed += c.poll_reload_at(clock);
    }
    return changed;
}

}  // namespace

int main() {
    std::printf("materialcache_check — .mat asset cache\n\n");

    std::error_code ec;
    g_dir = fs::temp_directory_path() / "gws_materialcache_check";
    fs::remove_all(g_dir, ec);
    fs::create_directories(g_dir, ec);
    double clock = 1000.0;

    // ---- Loading -----------------------------------------------------------
    std::printf("Loading:\n");
    {
        MaterialAssetCache cache;
        const auto path = g_dir / "rock.mat";
        write_file(path, "NAME=Rock\nROUGHNESS=0.8\nALBEDO_MAP=t/rock.png\n");

        const MaterialDesc* m = cache.get(path.string());
        check(m != nullptr, "an existing material loads");
        check(m && m->name == "Rock", "its fields are read");
        check(m && m->albedo_map == "t/rock.png", "its texture path is read");

        const MaterialDesc* again = cache.get(path.string());
        check(again == m, "a second get returns the SAME pointer (cached, not re-read)");

        check(cache.get("") == nullptr, "an empty path yields nullptr without touching disk");
    }

    // ---- Missing files -----------------------------------------------------
    std::printf("\nA material that will not load:\n");
    {
        MaterialAssetCache cache;
        const auto missing = (g_dir / "ghost.mat").string();

        check(cache.get(missing) == nullptr,
              "a missing material yields nullptr, NOT a default white material");
        check(cache.failed(missing), "the failure is remembered so it is not retried every frame");
        check(cache.get(missing) == nullptr, "still nullptr on the second ask");

        // The point of remembering the failure is that it must not be permanent:
        // referencing a material before creating it is normal mid-authoring.
        touch_distinct(missing, "NAME=Ghost\nMETALLIC=1\n");
        const size_t changed = settle_and_poll(cache, clock);
        check(changed >= 1, "creating the file afterwards is noticed");
        const MaterialDesc* m = cache.get(missing);
        check(m != nullptr, "a material that appears later starts working — no restart");
        check(m && m->name == "Ghost", "and it has the new file's contents");
        check(!cache.failed(missing), "the failure flag clears once it loads");
    }

    // ---- Reload reports CONTENT changes, not writes -------------------------
    std::printf("\nReload distinguishes a real edit from a no-op write:\n");
    {
        MaterialAssetCache cache;
        const auto path = g_dir / "steel.mat";
        const std::string body = "NAME=Steel\nMETALLIC=1\nROUGHNESS=0.2\n";
        write_file(path, body);
        const MaterialDesc* m = cache.get(path.string());
        check(m && m->metallic > 0.9f, "loaded");

        touch_distinct(path, body);   // same bytes, new mtime
        check(settle_and_poll(cache, clock) == 0,
              "rewriting identical bytes reports 0 changed (no needless GPU rebuild)");

        touch_distinct(path, "NAME=Steel\nMETALLIC=0\nROUGHNESS=0.9\n");
        check(settle_and_poll(cache, clock) == 1, "a real edit reports 1 changed");
        const MaterialDesc* after = cache.get(path.string());
        check(after && after->metallic < 0.1f && after->roughness > 0.8f,
              "and the new values are what get() returns");
    }

    // ---- A material that disappears ----------------------------------------
    // The shared AssetWatcher deliberately does NOT report deletion: a file
    // vanishing is usually the middle of a save (write-to-temp-then-rename is
    // the normal way editors and exporters write), and there is nothing useful
    // to reload from anyway. Materials follow that rule rather than inventing
    // their own, so a material behaves like a mesh, a texture and a shader
    // instead of being the one asset type that flickers when Blender saves.
    //
    // The consequence is asserted rather than left implicit: a deleted material
    // keeps rendering from its last good copy until something invalidates it.
    std::printf("\nA material deleted out from under a scene:\n");
    {
        MaterialAssetCache cache;
        const auto path = g_dir / "vanishing.mat";
        write_file(path, "NAME=Vanishing\nROUGHNESS=0.5\n");
        check(cache.get(path.string()) != nullptr, "loads while it exists");

        fs::remove(path, ec);
        settle_and_poll(cache, clock);
        check(cache.get(path.string()) != nullptr,
              "keeps the last good copy — deletion is treated as mid-save, not as an edit");
        check(!cache.failed(path.string()), "and is not marked failed by a vanish alone");

        // invalidate() is the escape hatch, and it must surface the real state.
        cache.invalidate(path.string());
        check(cache.get(path.string()) == nullptr,
              "invalidate() re-reads and THEN reports it gone");
        check(cache.failed(path.string()), "and marks it failed");
    }

    // ---- set() vs save() ---------------------------------------------------
    // The split that makes live slider dragging affordable.
    std::printf("\nIn-memory edit versus writing the file:\n");
    {
        MaterialAssetCache cache;
        const auto path = g_dir / "live.mat";
        write_file(path, "NAME=Live\nROUGHNESS=1.0\n");
        cache.get(path.string());

        MaterialDesc edited;
        edited.name      = "Live";
        edited.roughness = 0.25f;
        cache.set(path.string(), edited);

        const MaterialDesc* m = cache.get(path.string());
        check(m && std::fabs(m->roughness - 0.25f) < 1e-4f,
              "set() is visible immediately (the viewport follows the slider)");

        MaterialDesc on_disk;
        gws::assets::load_material(path.string(), on_disk);
        check(std::fabs(on_disk.roughness - 1.0f) < 1e-4f,
              "set() did NOT write the file — that is what makes drag-to-edit cheap");

        check(cache.save(path.string(), edited), "save() succeeds");
        gws::assets::load_material(path.string(), on_disk);
        check(std::fabs(on_disk.roughness - 0.25f) < 1e-4f, "save() wrote the file");
        const MaterialDesc* after = cache.get(path.string());
        check(after && std::fabs(after->roughness - 0.25f) < 1e-4f,
              "and the cache agrees with the file afterwards");
    }

    // ---- save() into a folder that does not exist yet -----------------------
    std::printf("\nCreating a material somewhere new:\n");
    {
        MaterialAssetCache cache;
        const auto path = (g_dir / "made" / "up" / "path" / "new.mat").string();
        MaterialDesc m;
        m.name = "New";
        check(cache.save(path, m), "save creates the folders on the way");
        check(cache.get(path) != nullptr, "and the material is immediately available");
        check(!cache.failed(path), "a freshly created material is not marked failed");
    }

    // ---- invalidate --------------------------------------------------------
    std::printf("\nExplicit invalidation:\n");
    {
        MaterialAssetCache cache;
        const auto path = g_dir / "inv.mat";
        write_file(path, "NAME=A\nROUGHNESS=0.1\n");
        cache.get(path.string());
        // Rewrite WITHOUT going through the watcher, to prove invalidate() is
        // what forces the re-read rather than the poll happening to catch it.
        write_file(path, "NAME=B\nROUGHNESS=0.9\n");
        cache.invalidate(path.string());
        const MaterialDesc* m = cache.get(path.string());
        check(m && m->name == "B", "invalidate() forces a re-read from disk");
    }

    fs::remove_all(g_dir, ec);
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
