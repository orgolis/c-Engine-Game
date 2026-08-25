// ============================================================================
// assetops_check -- the file operations behind the asset browser's context menu.
//
// Every function under test can lose someone's work, and the interesting cases
// are all ones where the WRONG behaviour still looks like it worked:
//
//   * "CON.txt" is a reserved Windows device name. Creating it does not error,
//     it just does not produce a file.
//   * A name ending in a space is silently trimmed, so renaming "a" to "a "
//     reports success and changes nothing.
//   * Pasting a folder into its own subtree recurses until the disk fills.
//   * Cutting a file into the folder it already occupies must not duplicate it.
//   * A copy stays on the clipboard; a cut does not. Getting that backwards
//     turns one move into a stamp that scatters copies.
//
// Runs in a temp directory it creates and removes.
// ============================================================================

#include "asset_file_ops.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace ops = schizo::editor::assetops;
namespace fs  = std::filesystem;

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::cout << "  FAIL: " << what << "\n";
    }
}

static void write_file(const fs::path& p, const std::string& text = "x") {
    std::ofstream f(p, std::ios::binary);
    f << text;
}

// ---------------------------------------------------------------------------

static void test_name_validation() {
    std::cout << "-- name validation --\n";
    ops::Error why = ops::Error::None;

    check(ops::is_valid_filename("hero.mat"), "an ordinary name is valid");
    check(ops::is_valid_filename("a b c.txt"), "interior spaces are fine");
    check(ops::is_valid_filename(".gitignore"), "a leading dot is a stem, not an empty name");

    check(!ops::is_valid_filename("", &why) && why == ops::Error::EmptyName,
          "empty name rejected");
    check(!ops::is_valid_filename("a/b", &why) && why == ops::Error::InvalidChar,
          "a separator is not a filename");
    check(!ops::is_valid_filename("what?.txt", &why) && why == ops::Error::InvalidChar,
          "'?' rejected");
    check(!ops::is_valid_filename("a:b", &why) && why == ops::Error::InvalidChar,
          "':' rejected");

    // Windows strips these, so accepting them makes rename a silent no-op.
    check(!ops::is_valid_filename("trailing ", &why) && why == ops::Error::TrailingDotOrSpace,
          "trailing space rejected");
    check(!ops::is_valid_filename("trailing.", &why) && why == ops::Error::TrailingDotOrSpace,
          "trailing dot rejected");

    // The reserved-device rule applies to the STEM. This is the case a
    // name-only check passes and the filesystem then fails on.
    check(!ops::is_valid_filename("CON", &why) && why == ops::Error::ReservedName,
          "CON rejected");
    check(!ops::is_valid_filename("con.txt", &why) && why == ops::Error::ReservedName,
          "con.txt rejected -- an extension does not lift the reservation");
    check(!ops::is_valid_filename("LPT9.mat", &why) && why == ops::Error::ReservedName,
          "LPT9.mat rejected");
    check(ops::is_valid_filename("COM10.txt"),
          "COM10 is NOT reserved -- only COM1..COM9 are");
    check(ops::is_valid_filename("CONSOLE.txt"),
          "CONSOLE is not CON -- prefix matching would be wrong here");
}

static void test_split_and_unique(const fs::path& dir) {
    std::cout << "-- name splitting and uniquifying --\n";
    std::string stem, ext;

    ops::split_name("archive.tar.gz", stem, ext);
    check(stem == "archive" && ext == ".tar.gz",
          "split at the FIRST dot so a double extension survives");

    ops::split_name("noext", stem, ext);
    check(stem == "noext" && ext.empty(), "a name with no dot has no extension");

    ops::split_name(".gitignore", stem, ext);
    check(stem == ".gitignore" && ext.empty(), "a dotfile is all stem");

    const fs::path a = dir / "u.txt";
    check(ops::unique_sibling(a) == a, "a free name is returned unchanged");

    write_file(a);
    check(ops::unique_sibling(a).filename().string() == "u (2).txt",
          "first collision becomes ' (2)'");

    write_file(dir / "u (2).txt");
    check(ops::unique_sibling(a).filename().string() == "u (3).txt",
          "the counter skips names already taken");

    write_file(dir / "double.tar.gz");
    check(ops::unique_sibling(dir / "double.tar.gz").filename().string() == "double (2).tar.gz",
          "uniquifying keeps a double extension intact");
}

static void test_rename(const fs::path& dir) {
    std::cout << "-- rename --\n";
    const fs::path src = dir / "old.txt";
    write_file(src, "payload");

    fs::path out;
    check(ops::rename(src, "new.txt", &out) == ops::Error::None,
          "a valid rename succeeds");
    check(out.filename().string() == "new.txt" && fs::exists(out),
          "the file is at the new name");
    check(!fs::exists(src), "and not at the old one");

    // Content must survive -- a rename implemented as create+truncate would
    // pass every name assertion above and lose the file's contents.
    std::ifstream f(out, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check(body == "payload", "contents survive the rename");
    f.close();

    check(ops::rename(out, "new.txt") == ops::Error::None,
          "renaming to the same name is success, not a collision");
    check(fs::exists(dir / "new.txt"), "and leaves the file where it was");

    check(ops::rename(out, "CON.txt") == ops::Error::ReservedName,
          "rename refuses a reserved name before touching the disk");
    check(fs::exists(dir / "new.txt"), "a refused rename changes nothing");

    check(ops::rename(dir / "missing.txt", "x.txt") == ops::Error::NotFound,
          "renaming something absent reports NotFound");

    // Colliding renames uniquify rather than replace: losing the OTHER file is
    // the failure mode this rule exists to prevent.
    write_file(dir / "target.txt", "victim");
    fs::path collided;
    check(ops::rename(dir / "new.txt", "target.txt", &collided) == ops::Error::None,
          "a colliding rename still succeeds");
    check(collided.filename().string() == "target (2).txt",
          "by uniquifying rather than overwriting");
    std::ifstream v(dir / "target.txt", std::ios::binary);
    std::string vbody((std::istreambuf_iterator<char>(v)), std::istreambuf_iterator<char>());
    check(vbody == "victim", "the existing file is untouched");
}

static void test_duplicate(const fs::path& dir) {
    std::cout << "-- duplicate --\n";
    const fs::path src = dir / "dup.txt";
    write_file(src, "body");

    fs::path first, second, third;
    check(ops::duplicate(src, &first) == ops::Error::None, "duplicate succeeds");
    check(first.filename().string() == "dup - Copy.txt",
          "Explorer's naming: ' - Copy'");
    check(fs::exists(src) && fs::exists(first), "both files exist afterwards");

    check(ops::duplicate(src, &second) == ops::Error::None, "duplicating again succeeds");
    check(second.filename().string() == "dup - Copy (2).txt",
          "the second copy is numbered");

    // The suffix must not stack: duplicating a copy gives "(2)", never
    // "dup - Copy - Copy".
    check(ops::duplicate(first, &third) == ops::Error::None, "duplicating a copy succeeds");
    check(third.filename().string() == "dup - Copy (3).txt",
          "' - Copy' is not appended twice");

    const fs::path sub = dir / "tree";
    fs::create_directories(sub / "inner");
    write_file(sub / "inner" / "leaf.txt", "leaf");
    fs::path dtree;
    check(ops::duplicate(sub, &dtree) == ops::Error::None, "a directory duplicates");
    check(fs::exists(dtree / "inner" / "leaf.txt"),
          "and it is copied recursively, not just the top folder");
}

static void test_clipboard(const fs::path& dir) {
    std::cout << "-- cut / copy / paste --\n";
    const fs::path a_dir = dir / "A";
    const fs::path b_dir = dir / "B";
    fs::create_directories(a_dir);
    fs::create_directories(b_dir);
    write_file(a_dir / "f.txt", "content");

    ops::Clipboard clip;

    clip.copy_one(a_dir / "f.txt");
    std::vector<fs::path> made;
    check(ops::paste(clip, b_dir, &made) == ops::Error::None, "copy-paste succeeds");
    check(fs::exists(b_dir / "f.txt"), "the file lands in the destination");
    check(fs::exists(a_dir / "f.txt"), "and the source still exists after a COPY");
    check(!clip.empty(), "a copy STAYS on the clipboard, so it can be pasted again");

    // Pasting into the same folder is where copy naming has to kick in.
    clip.copy_one(a_dir / "f.txt");
    check(ops::paste(clip, a_dir, &made) == ops::Error::None, "copy into the same folder succeeds");
    check(made.size() == 1 && made[0].filename().string() == "f - Copy.txt",
          "and produces ' - Copy' rather than colliding");

    clip.cut_one(a_dir / "f.txt");
    check(ops::paste(clip, b_dir, &made) == ops::Error::None, "cut-paste succeeds");
    check(!fs::exists(a_dir / "f.txt"), "the source is gone after a CUT");
    check(fs::exists(b_dir / "f (2).txt"),
          "and the destination uniquifies rather than replacing what was there");
    check(clip.empty(), "a cut is CONSUMED by its paste -- otherwise one move becomes a stamp");

    // The destination file from the first copy must still hold its content:
    // an overwrite would have destroyed it silently.
    std::ifstream f(b_dir / "f.txt", std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check(body == "content", "the file already in the destination is untouched");
    f.close();

    write_file(a_dir / "g.txt");
    clip.cut_one(a_dir / "g.txt");
    check(ops::paste(clip, a_dir) == ops::Error::SameLocation,
          "cutting into the folder it already occupies is refused, not duplicated");
    check(fs::exists(a_dir / "g.txt"), "and the file is still there");

    check(ops::paste(clip, dir / "nowhere") == ops::Error::NotFound,
          "pasting into a missing folder reports NotFound");
}

static void test_paste_into_self(const fs::path& dir) {
    std::cout << "-- the recursive paste --\n";
    const fs::path outer = dir / "outer";
    const fs::path inner = outer / "inner";
    fs::create_directories(inner);
    write_file(outer / "top.txt");

    check(ops::is_inside(inner, outer), "inner is inside outer");
    check(!ops::is_inside(outer, inner), "outer is not inside inner");
    check(ops::is_inside(outer, outer), "a folder is inside itself");

    ops::Clipboard clip;
    clip.copy_one(outer);
    check(ops::paste(clip, inner) == ops::Error::IntoOwnSubtree,
          "pasting a folder into its own subtree is refused");
    check(ops::paste(clip, outer) == ops::Error::IntoOwnSubtree,
          "and into itself");

    // Nothing may have been written before the refusal -- the guard runs over
    // every clipboard item BEFORE the first copy, or a multi-item paste would
    // half-apply.
    check(!fs::exists(inner / "outer"), "no partial copy was left behind");
}

static void test_properties(const fs::path& dir) {
    std::cout << "-- properties --\n";
    const fs::path root = dir / "props";
    fs::create_directories(root / "sub");
    write_file(root / "a.txt", "12345");
    write_file(root / "sub" / "b.txt", "678");

    ops::Properties file_p = ops::inspect(root / "a.txt", "Text");
    check(file_p.valid, "an existing file inspects");
    check(file_p.name == "a.txt", "name reported");
    check(file_p.size_bytes == 5, "file size reported");
    check(!file_p.is_dir, "a file is not a directory");
    check(std::string(file_p.type) == "Text", "the caller's type label is carried through");
    check(!file_p.modified.empty(), "a modified timestamp is produced");

    ops::Properties dir_p = ops::inspect(root, "Folder");
    check(dir_p.valid && dir_p.is_dir, "a directory inspects");
    check(dir_p.file_count == 2, "files are counted recursively");
    check(dir_p.dir_count == 1, "subdirectories are counted");
    check(dir_p.size_bytes == 8, "sizes are summed across the whole subtree");

    check(!ops::inspect(root / "nope.txt").valid,
          "inspecting something absent reports invalid rather than zeroes");
}

// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== assetops_check ===\n";

    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "gws_assetops_check";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    if (ec) {
        std::cout << "could not create the temp directory: " << ec.message() << "\n";
        return 1;
    }

    test_name_validation();
    test_split_and_unique(base);
    test_rename(base);
    test_duplicate(base);
    test_clipboard(base);
    test_paste_into_self(base);
    test_properties(base);

    fs::remove_all(base, ec);

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed\n";
    if (g_failed) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
