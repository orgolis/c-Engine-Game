// projectfile_check: reading a project file without losing anything, and who
// answers Esc (editor/include/project_file.h), headless.
//
// The bug this guards: a writer that kept only the keys it knew erased the
// others -- the Hub dropped TestGame's `escape = game` when it changed the
// project's engine version, and from then on one Esc brought up the engine's
// menu (the editor's "click to resume", an exported game's Resume / Quit) and the
// game's own pause menu together.
#include "project_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace schizo::editor;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
}  // namespace

int main() {
    {
        std::istringstream in(
            "# GameWorldshaper project file\r\n"
            "name = TestGame\r\n"
            "engine_version = 0.8.65\n"
            "# the author's note\n"
            "escape = game\n"
            "some_tool_key = 3\n"
            "features = Physics,Animation\n"
            "a line without a value\n");
        const ManifestLines m = read_manifest_lines(in, {"name", "engine_version", "default_scene", "escape", "features"});
        check(m.keys.size() == 4 && m.keys[2].first == "escape" && m.keys[2].second == "game",
              "the keys the engine reads (escape among them), in order, trimmed");
        check(m.other.size() == 3 && m.other[0] == "# the author's note" && m.other[1] == "some_tool_key = 3" &&
                  m.other[2] == "a line without a value",
              "every other line kept as it was (a comment, another tool's key, a stray line)");
        bool header = false;
        for (const auto& l : m.other) header = header || l.find("GameWorldshaper project file") != std::string::npos;
        check(!header, "the header is not kept (the writer puts it back: no doubled header)");
    }
    {
        std::istringstream yes("node\t15\tevent.key_pressed\t0.0\t1130.0\tEscape\tnone\t0\t\n");
        std::istringstream other_key("node\t3\tevent.key_pressed\t0.0\t0.0\tI\tnone\t0\t\n");
        std::istringstream not_event("node\t4\tui.show\t0.0\t0.0\tEscape\tnone\t0\t\n");
        check(lgraph_answers_escape(yes), "a logic graph with an Escape key event answers Esc");
        check(!lgraph_answers_escape(other_key) && !lgraph_answers_escape(not_event),
              "another key's event, or 'Escape' outside a key event, does not");
    }
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / "gws_projectfile_check";
        fs::remove_all(root, ec);
        write(root / "testgame" / "Checkpoint.lgraph", "VSCRIPT 2\nnode\t1\tevent.begin_play\t0\t0\t\tnone\t0\t\n");
        write(root / "art_source" / "old" / "GameRules.lgraph", "node\t15\tevent.key_pressed\t0\t0\tEscape\tnone\t0\t\n");
        check(logic_answering_escape(root.string()).empty(),
              "a project whose logic does not answer Esc (a copy in art_source does not count): none");
        write(root / "testgame" / "GameRules.lgraph", "VSCRIPT 2\nnode\t15\tevent.key_pressed\t0.0\t1130.0\tEscape\tnone\t0\t\n");
        check(logic_answering_escape(root.string()) == "testgame/GameRules.lgraph",
              "one whose GameRules answers Esc: that graph, project-relative");
        fs::remove_all(root, ec);
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
