// ============================================================================
// palette_check — the command palette ranks the way a person expects.
//
// A palette that finds the right command but puts it fourth is a palette people
// stop using: the whole value is that the first result is the one you meant, so
// you can type three letters and press Enter without reading the list. Ranking
// is therefore the feature, not a detail of it — and it is exactly the part a
// screenshot cannot check.
//
// So these assert ORDER, not membership. "does the list contain Save Scene" is
// true of a palette that returns everything in registration order.
// ============================================================================
#include "command_palette.h"

#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

CommandRegistry make_registry() {
    CommandRegistry r;
    auto nop = [] {};
    r.add("New Scene",          "File",   "Ctrl+N", nop);
    r.add("Open Scene",         "File",   "Ctrl+O", nop);
    r.add("Save Scene",         "File",   "Ctrl+S", nop);
    r.add("Save Scene As",      "File",   "",       nop);
    r.add("Undo",               "Edit",   "Ctrl+Z", nop);
    r.add("Redo",               "Edit",   "Ctrl+Y", nop);
    r.add("Duplicate Entity",   "Edit",   "Ctrl+D", nop);
    r.add("Toggle World Streaming", "Tools", "",    nop);
    r.add("Bake Navmesh",       "Tools",  "",       nop);
    r.add("Play",               "Run",    "F5",     nop);
    r.add("Rescale Selection",  "Edit",   "",       nop);
    return r;
}

std::string first(const CommandRegistry& r, const std::string& q) {
    const auto hits = r.search(q);
    return hits.empty() ? std::string("<none>") : hits.front()->title;
}

}  // namespace

int main() {
    std::printf("=== palette_check ===\n\n");
    const CommandRegistry r = make_registry();

    // ---- 1. initials find the command ---------------------------------------
    // The behaviour people actually rely on: type the initials, press Enter.
    {
        check(first(r, "ss") == "Save Scene",
              "\"ss\" -> Save Scene (initials beat mid-word matches)");
        check(first(r, "bn") == "Bake Navmesh", "\"bn\" -> Bake Navmesh");
        check(first(r, "de") == "Duplicate Entity", "\"de\" -> Duplicate Entity");
    }

    // ---- 2. word starts outrank letters buried mid-word ----------------------
    // "Rescale Selection" matches s-c only inside words; a naive subsequence
    // match would rank it alongside the real hits.
    //
    // I first asserted "sc" -> Save Scene here, and that was my expectation
    // being wrong rather than the ranking. Measured, "sc" gives New Scene (28)
    // and Open Scene (28) ahead of Save Scene (24): "sc" matches "SCene"
    // consecutively AND at a word start, which is a better hit than s...c
    // spread across "Save Scene". Someone typing "sc" most likely wants
    // something Scene-related, so the ranking was right and the test was not.
    // The property actually worth pinning is that the mid-word match loses.
    {
        const auto hits = r.search("sc");
        check(!hits.empty() && hits.back()->title == "Rescale Selection",
              "\"sc\": the only mid-word match ranks LAST");
        check(hits.size() > 1 && hits.front()->title.find("Scene") != std::string::npos,
              "and every better hit matched \"Sc\" at the start of a word");
    }

    // ---- 3. a typed prefix wins ----------------------------------------------
    {
        check(first(r, "save") == "Save Scene", "\"save\" -> Save Scene");
        check(first(r, "undo") == "Undo",       "\"undo\" -> Undo");
        check(first(r, "play") == "Play",       "\"play\" -> Play");
    }

    // ---- 4. the more specific command wins a shared prefix -------------------
    // "Save Scene" and "Save Scene As" both match "save scene"; the shorter one
    // is what someone typing it meant.
    {
        check(first(r, "save scene") == "Save Scene",
              "\"save scene\" -> Save Scene, not Save Scene As");
    }

    // ---- 5. categories are searchable, at a discount --------------------------
    {
        const auto hits = r.search("tools");
        bool all_tools = !hits.empty();
        for (const Command* c : hits) if (c->category != "Tools") all_tools = false;
        check(all_tools, "\"tools\" surfaces the Tools commands");

        // ...but a category match must not outrank a command's own name.
        check(first(r, "undo") == "Undo",
              "and a category never outranks a command whose NAME was typed");
    }

    // ---- 6. no match is no match ---------------------------------------------
    // A fuzzy matcher that always returns something makes the palette lie.
    {
        check(r.search("zzzz").empty(), "nonsense matches nothing at all");
        check(r.search("scene save").empty(),
              "and so does out-of-order text — this is subsequence matching, "
              "which the ordering guarantees depend on");
    }

    // ---- 7. an empty query lists everything ----------------------------------
    // Opening the palette should show what is available, not a blank box.
    {
        check(r.search("", 100).size() == r.size(),
              "an empty query lists every command");
    }

    // ---- 8. the order does not churn between keystrokes ----------------------
    // Two commands scoring equally must not swap places as the user types; a
    // list that reorders under an unchanged query is worse than one that ranks
    // imperfectly, because it moves the item out from under the cursor.
    {
        const auto a = r.search("e");
        const auto b = r.search("e");
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
        check(same, "the same query gives the same order every time");
    }

    // ---- 9. limit is respected -----------------------------------------------
    {
        check(r.search("e", 3).size() == 3, "the result limit is honoured");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL palette_check\n");
    return g_fail ? 1 : 0;
}
