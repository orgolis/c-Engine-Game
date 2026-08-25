#pragma once

// ============================================================================
// Asset file operations — rename / duplicate / cut / copy / paste, Explorer's
// rules, with no ImGui in sight so they can be tested headlessly.
//
// Every function here can destroy a user's work, which is why the logic lives
// apart from the panel that calls it. The panel decides WHEN; this file decides
// WHETHER, and says why not.
//
// Two rules the whole file obeys:
//
//   1. NOTHING is ever overwritten. Where Explorer would raise a replace/skip
//      dialog, this uniquifies the destination name instead. A surprising name
//      is recoverable; a replaced file is not.
//
//   2. A name is validated against WINDOWS rules before it reaches the
//      filesystem, because the failures there are silent rather than loud --
//      trailing spaces are stripped, and the reserved device names fail in ways
//      that read as "the editor is broken".
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace schizo::editor::assetops {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------
enum class Error {
    None = 0,
    EmptyName,
    InvalidChar,
    ReservedName,
    TrailingDotOrSpace,
    NotFound,
    IntoOwnSubtree,
    SameLocation,
    Filesystem,
};

/// Human-facing reason, written to be shown verbatim in the UI.
inline const char* message(Error e) {
    switch (e) {
        case Error::None:               return "";
        case Error::EmptyName:          return "A name cannot be empty.";
        case Error::InvalidChar:        return "A name cannot contain  <  >  :  \"  /  \\  |  ?  *";
        case Error::ReservedName:       return "That name is reserved by Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9) "
                                               "and stays reserved even with an extension.";
        case Error::TrailingDotOrSpace: return "A name cannot end in a space or a dot -- Windows silently strips both, "
                                               "so the rename would appear to do nothing.";
        case Error::NotFound:           return "The file no longer exists.";
        case Error::IntoOwnSubtree:     return "A folder cannot be pasted inside itself.";
        case Error::SameLocation:       return "Source and destination are the same folder.";
        case Error::Filesystem:         return "The operating system refused the operation.";
    }
    return "Unknown error.";
}

// ---------------------------------------------------------------------------
// Name validation
// ---------------------------------------------------------------------------

/// The MS-DOS device names. Reserved as a whole *stem*, so "CON.txt" is every
/// bit as illegal as "CON" -- the check has to run on the stem, not the name.
inline bool is_reserved_stem(std::string stem) {
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* kDevices[] = {"CON", "PRN", "AUX", "NUL"};
    for (const char* d : kDevices)
        if (stem == d) return true;
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') {
        const std::string head = stem.substr(0, 3);
        if (head == "COM" || head == "LPT") return true;
    }
    return false;
}

/// Validate a single path COMPONENT (never a path -- a separator is rejected).
inline bool is_valid_filename(const std::string& name, Error* why = nullptr) {
    auto fail = [&](Error e) { if (why) *why = e; return false; };

    if (name.empty()) return fail(Error::EmptyName);
    if (name == "." || name == "..") return fail(Error::InvalidChar);

    for (unsigned char c : name) {
        if (c < 0x20) return fail(Error::InvalidChar);
        if (std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos)
            return fail(Error::InvalidChar);
    }
    if (name.back() == ' ' || name.back() == '.') return fail(Error::TrailingDotOrSpace);

    // The stem is everything before the FIRST dot: "CON.txt.bak" is reserved.
    const size_t dot = name.find('.');
    if (is_reserved_stem(dot == std::string::npos ? name : name.substr(0, dot)))
        return fail(Error::ReservedName);

    if (why) *why = Error::None;
    return true;
}

// ---------------------------------------------------------------------------
// Unique names
// ---------------------------------------------------------------------------

/// Split at the FIRST dot rather than the last, so ".tar.gz" survives a
/// duplicate as "archive (2).tar.gz" and not "archive.tar (2).gz". A leading
/// dot is part of the stem: ".gitignore" has no extension.
inline void split_name(const std::string& name, std::string& stem, std::string& ext) {
    const size_t dot = name.find('.', name.empty() ? 0 : 1);
    if (dot == std::string::npos) { stem = name; ext.clear(); }
    else                          { stem = name.substr(0, dot); ext = name.substr(dot); }
}

/// First free path of the form "<stem>[ (n)]<ext>" beside `desired`.
/// Returns `desired` untouched when nothing is in the way.
inline fs::path unique_sibling(const fs::path& desired) {
    std::error_code ec;
    if (!fs::exists(desired, ec)) return desired;

    std::string stem, ext;
    split_name(desired.filename().string(), stem, ext);
    const fs::path dir = desired.parent_path();

    // Bounded rather than while(true): a directory that reports every candidate
    // as existing (a permission fault, a hostile filesystem) would otherwise
    // spin forever inside a UI frame.
    for (int n = 2; n < 10000; ++n) {
        fs::path candidate = dir / (stem + " (" + std::to_string(n) + ")" + ext);
        if (!fs::exists(candidate, ec)) return candidate;
    }
    return {};
}

/// Explorer's copy naming: "a.txt" -> "a - Copy.txt" -> "a - Copy (2).txt".
/// The suffix is only added once; a second duplicate numbers it instead of
/// producing "a - Copy - Copy.txt".
inline fs::path copy_sibling(const fs::path& src) {
    std::string stem, ext;
    split_name(src.filename().string(), stem, ext);

    static const std::string kSuffix = " - Copy";
    const bool already_a_copy =
        stem.size() > kSuffix.size() &&
        stem.compare(stem.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;

    const fs::path first = src.parent_path() /
                           (already_a_copy ? stem + ext : stem + kSuffix + ext);
    return unique_sibling(first);
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

/// Rename in place. `new_name` is a bare filename, not a path.
inline Error rename(const fs::path& src, const std::string& new_name, fs::path* out = nullptr) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return Error::NotFound;

    Error why = Error::None;
    if (!is_valid_filename(new_name, &why)) return why;

    fs::path dst = src.parent_path() / new_name;

    // A rename to the same name is success, not a collision -- otherwise
    // opening the rename box and pressing Enter reports an error.
    if (dst == src) { if (out) *out = src; return Error::None; }

    // Case-only renames ("readme" -> "README") collide with themselves on
    // Windows' case-insensitive filesystem, so they must bypass uniquifying or
    // they would land on "README (2)".
    const bool case_only = dst.parent_path() == src.parent_path() &&
                           dst.filename().string().size() == src.filename().string().size() &&
                           std::equal(dst.filename().string().begin(), dst.filename().string().end(),
                                      src.filename().string().begin(),
                                      [](char a, char b) {
                                          return std::tolower(static_cast<unsigned char>(a)) ==
                                                 std::tolower(static_cast<unsigned char>(b));
                                      });
    if (!case_only) dst = unique_sibling(dst);
    if (dst.empty()) return Error::Filesystem;

    fs::rename(src, dst, ec);
    if (ec) return Error::Filesystem;
    if (out) *out = dst;
    return Error::None;
}

/// Copy beside the original under an Explorer-style " - Copy" name.
/// Directories are copied whole.
inline Error duplicate(const fs::path& src, fs::path* out = nullptr) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return Error::NotFound;

    const fs::path dst = copy_sibling(src);
    if (dst.empty()) return Error::Filesystem;

    if (fs::is_directory(src, ec))
        fs::copy(src, dst, fs::copy_options::recursive, ec);
    else
        fs::copy_file(src, dst, ec);

    if (ec) return Error::Filesystem;
    if (out) *out = dst;
    return Error::None;
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

/// Holds absolute paths. A CUT is not applied until the paste, which is what
/// lets a user cut, change their mind, and lose nothing.
struct Clipboard {
    std::vector<fs::path> items;
    bool cut = false;

    bool empty() const { return items.empty(); }
    void clear() { items.clear(); cut = false; }

    void copy_one(const fs::path& p) { items.assign(1, p); cut = false; }
    void cut_one(const fs::path& p)  { items.assign(1, p); cut = true; }
};

/// True when `dir` is `maybe_child` or one of its ancestors -- the test that
/// stops a folder being pasted into itself.
inline bool is_inside(const fs::path& maybe_child, const fs::path& dir) {
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(maybe_child, ec);
    const fs::path b = fs::weakly_canonical(dir, ec);
    auto ai = a.begin(), ae = a.end();
    for (auto bi = b.begin(), be = b.end(); bi != be; ++bi, ++ai) {
        if (ai == ae) return false;
        if (*ai != *bi) return false;
    }
    return true;
}

/// Paste the clipboard into `dst_dir`. A cut clears the clipboard on success
/// (Explorer does the same -- a cut is one move, not a stamp); a copy keeps it,
/// so the same file can be pasted into several folders.
inline Error paste(Clipboard& clip, const fs::path& dst_dir,
                   std::vector<fs::path>* out = nullptr) {
    std::error_code ec;
    if (clip.empty()) return Error::None;
    if (!fs::is_directory(dst_dir, ec)) return Error::NotFound;

    for (const fs::path& src : clip.items) {
        if (!fs::exists(src, ec)) return Error::NotFound;
        if (fs::is_directory(src, ec) && is_inside(dst_dir, src)) return Error::IntoOwnSubtree;
        // Moving something into the folder it already lives in is a no-op, not
        // a copy. Only a COPY into the same folder produces " - Copy".
        if (clip.cut && src.parent_path() == dst_dir) return Error::SameLocation;
    }

    std::vector<fs::path> made;
    for (const fs::path& src : clip.items) {
        const bool same_dir = (src.parent_path() == dst_dir);
        fs::path dst = same_dir ? copy_sibling(src)
                                : unique_sibling(dst_dir / src.filename());
        if (dst.empty()) return Error::Filesystem;

        if (clip.cut) {
            fs::rename(src, dst, ec);
            if (ec) {
                // Across volumes rename fails; fall back to copy-then-delete,
                // in that order, so a failure leaves the source intact.
                ec.clear();
                if (fs::is_directory(src, ec)) fs::copy(src, dst, fs::copy_options::recursive, ec);
                else                           fs::copy_file(src, dst, ec);
                if (ec) return Error::Filesystem;
                fs::remove_all(src, ec);
            }
        } else if (fs::is_directory(src, ec)) {
            fs::copy(src, dst, fs::copy_options::recursive, ec);
        } else {
            fs::copy_file(src, dst, ec);
        }
        if (ec) return Error::Filesystem;
        made.push_back(dst);
    }

    if (clip.cut) clip.clear();
    if (out) *out = made;
    return Error::None;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

/// Byte count in the units a person reads. Binary multiples (1 KB = 1024),
/// matching what Explorer shows, so the two never disagree about a file.
inline std::string human_size(uint64_t b) {
    char buf[32];
    if (b >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ull * 1024)   std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
    else if (b >= 1024ull)          std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else                            std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    return buf;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

struct Properties {
    std::string name;
    std::string location;      // parent directory, absolute
    std::string type;          // caller-supplied classification
    bool        is_dir = false;
    uint64_t    size_bytes = 0;
    uint64_t    file_count = 0;   // directories only: recursive count
    uint64_t    dir_count  = 0;   // directories only: recursive count
    bool        read_only = false;
    std::string modified;      // "YYYY-MM-DD HH:MM"
    bool        valid = false;
};

/// Format a filesystem timestamp without <chrono>'s clock-casting minefield:
/// file_clock's epoch is unspecified, and the portable conversion needs C++20
/// support this toolchain has only in part.
inline std::string format_file_time(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return "";
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

/// Gather what a properties dialog shows. For a directory this walks the whole
/// subtree, which is why the panel caches the result rather than calling it per
/// frame.
inline Properties inspect(const fs::path& p, const char* type_label = "File") {
    Properties pr;
    std::error_code ec;
    if (!fs::exists(p, ec)) return pr;

    pr.valid    = true;
    pr.name     = p.filename().string();
    pr.location = p.parent_path().string();
    pr.type     = type_label ? type_label : "File";
    pr.is_dir   = fs::is_directory(p, ec);
    pr.modified = format_file_time(p);

    const auto perms = fs::status(p, ec).permissions();
    pr.read_only = (perms & fs::perms::owner_write) == fs::perms::none;

    if (!pr.is_dir) {
        pr.size_bytes = fs::file_size(p, ec);
        if (ec) pr.size_bytes = 0;
    } else {
        for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_directory(ec)) { ++pr.dir_count; continue; }
            ++pr.file_count;
            const auto sz = it->file_size(ec);
            if (!ec) pr.size_bytes += sz;
            ec.clear();
        }
    }
    return pr;
}

} // namespace schizo::editor::assetops
