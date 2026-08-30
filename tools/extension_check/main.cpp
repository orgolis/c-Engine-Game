// ====================
// extension_check — Phase 4.8 editor extensions (headless)
// ====================
//
// Drives ExtensionSystem with a FAKE ScriptHost rather than pocketpy. The thing
// under test is the loader and the command lifecycle, not any one language: a
// test that went through a real VM would fail for syntax reasons and pass for
// reasons that have nothing to do with the behaviour being asserted.
//
// The assertions that matter are the ones about RELOAD. Registration working is
// easy and visible; what is neither easy nor visible is that reloading twice
// leaves exactly one copy of each command. Every duplicate would still work, so
// the editor looks fine right up until the palette is unusable.

#include "editor_extensions.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

namespace fs = std::filesystem;
using namespace schizo::editor;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}

// ---------------------------------------------------------------------------
// A backend whose "script" is a tiny text file:
//   CMD <token> <title>     -- register this command on start
//   FAIL_START              -- on_start reports an error
//   FAIL_CREATE             -- the script will not load at all
//   BADTOKEN <token>        -- invoke() refuses this token
// ---------------------------------------------------------------------------
struct FakeInstance final : public ScriptInstance {
    std::vector<std::pair<std::string, std::string>> cmds;   // token, title
    std::string bad_token;
    bool        fail_start = false;
    const ScriptApi* api = nullptr;

    // Records what actually ran, so "the right function was called" is checked
    // rather than merely "something was called".
    static std::vector<std::string> invoked;

    bool start(uint32_t entity, std::string& err) override {
        if (entity != 0) { err = "an editor extension must be started with entity 0"; return false; }
        if (fail_start)  { err = "deliberate on_start failure"; return false; }
        for (const auto& c : cmds)
            if (api && api->register_command)
                api->register_command(api->ctx, c.second.c_str(), "Script", c.first.c_str());
        return true;
    }
    bool update(uint32_t, float, std::string&) override { return true; }

    bool invoke(const char* token, std::string& err) override {
        if (!token) { err = "null token"; return false; }
        if (!bad_token.empty() && bad_token == token) {
            err = std::string("no function named '") + token + "'";
            return false;
        }
        invoked.push_back(token);
        return true;
    }
};
std::vector<std::string> FakeInstance::invoked;

struct FakeHost final : public ScriptHost {
    const char* language() const override { return "Fake"; }
    std::unique_ptr<ScriptInstance> create(const std::string& path, const ScriptApi* api,
                                           std::string& err) override {
        std::ifstream in(path);
        if (!in) { err = "cannot open " + path; return nullptr; }
        auto inst = std::make_unique<FakeInstance>();
        inst->api = api;
        std::string tag;
        while (in >> tag) {
            if (tag == "FAIL_CREATE") { err = "deliberate create failure"; return nullptr; }
            if (tag == "FAIL_START")  { inst->fail_start = true; continue; }
            if (tag == "BADTOKEN")    { in >> inst->bad_token; continue; }
            if (tag == "CMD") {
                std::string token, title;
                in >> token;
                in >> std::ws;
                std::getline(in, title);
                inst->cmds.emplace_back(token, title);
            }
        }
        return inst;
    }
};

static void write_script(const fs::path& p, const std::string& body) {
    std::ofstream out(p, std::ios::trunc);
    out << body;
}

// Give the file a STRICTLY INCREASING mtime.
//
// The obvious version -- read the current stamp and add five seconds -- is wrong
// in a test: two edits inside the same filesystem tick both land on
// (that tick + 5s), so the second is byte-identical to the first and the watcher
// correctly reports no change. That is a real property of mtime watching, but it
// is not what these assertions are about, so the stamp is driven by a counter
// instead of by the clock.
static void touch_newer(const fs::path& p) {
    static int n = 0;
    fs::last_write_time(p, fs::file_time_type::clock::now() + std::chrono::seconds(++n * 10));
}

static size_t count_titled(const CommandRegistry& cmds, const std::string& title) {
    size_t n = 0;
    for (const auto& c : cmds.all()) if (c.title == title) ++n;
    return n;
}

int main() {
    std::printf("extension_check — Phase 4.8 editor extensions\n");

    const fs::path root = fs::temp_directory_path() / "gws_extension_check";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // The api the extensions see. Only the entries an extension uses are wired;
    // ctx carries the registry + system exactly as the editor's ctx does.
    static std::vector<std::string> logged;   // what a real script logged
    struct Ctx { CommandRegistry* cmds; ExtensionSystem* sys; } ctx{};
    ScriptApi api{};
    api.ctx = &ctx;
    api.register_command = [](void* c, const char* title, const char* cat, const char* token) {
        auto* x = static_cast<Ctx*>(c);
        x->sys->register_command_from_script(*x->cmds, title, cat, token);
    };
    api.run_command = [](void* c, const char* title) {
        auto* x = static_cast<Ctx*>(c);
        return x->cmds->run_by_title(title);
    };

    std::printf("\n[group] loading\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());

        cmds.add("Save Scene", "File", "Ctrl+S", [] {});   // a built-in to protect
        write_script(root / "alpha.fake", "CMD hello Say Hello\nCMD bye Say Bye\n");
        sys.load_all(root, cmds, api);

        check("one extension found", sys.extensions().size() == 1);
        check("it is healthy", sys.extensions()[0].error.empty());
        check("its language is reported", sys.extensions()[0].language == std::string("Fake"));
        check("both commands registered", sys.total_commands() == 2);
        check("commands reached the palette", count_titled(cmds, "Say Hello") == 1);
        check("the built-in is untouched", count_titled(cmds, "Save Scene") == 1);
        check("commands are owned by the extension", cmds.all()[1].owner == "alpha");
        check("the built-in has no owner", cmds.all()[0].owner.empty());

        // A command must call back into the script that registered it.
        FakeInstance::invoked.clear();
        check("running the command invokes its token",
              cmds.run_by_title("Say Hello") &&
              FakeInstance::invoked.size() == 1 && FakeInstance::invoked[0] == "hello");
        check("a second command invokes a DIFFERENT token",
              cmds.run_by_title("Say Bye") &&
              FakeInstance::invoked.size() == 2 && FakeInstance::invoked[1] == "bye");
        check("an unknown title reports false", !cmds.run_by_title("Nope"));
    }

    std::printf("\n[group] reload does not duplicate (the silent one)\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        write_script(root / "alpha.fake", "CMD hello Say Hello\nCMD bye Say Bye\n");
        sys.load_all(root, cmds, api);

        sys.reload_all(cmds, api);
        check("after one reload there is still one copy", count_titled(cmds, "Say Hello") == 1);
        sys.reload_all(cmds, api);
        sys.reload_all(cmds, api);
        check("after three reloads there is still one copy", count_titled(cmds, "Say Hello") == 1);
        check("the total command count is stable", sys.total_commands() == 2);

        // Reload must also pick up a CHANGED command set, not just re-add the old.
        write_script(root / "alpha.fake", "CMD solo Only One\n");
        sys.reload_all(cmds, api);
        check("a removed command is gone", count_titled(cmds, "Say Hello") == 0);
        check("the new command is present", count_titled(cmds, "Only One") == 1);
        check("count follows the file", sys.total_commands() == 1);
    }

    std::printf("\n[group] hot reload on mtime\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        write_script(root / "alpha.fake", "CMD hello Say Hello\n");
        sys.load_all(root, cmds, api);

        sys.poll(cmds, api, 10.0f);   // nothing changed
        check("an unchanged file is not reloaded", count_titled(cmds, "Say Hello") == 1);

        write_script(root / "alpha.fake", "CMD other Something Else\n");
        touch_newer(root / "alpha.fake");
        sys.poll(cmds, api, 10.0f);
        check("a changed file reloads", count_titled(cmds, "Something Else") == 1);
        check("and drops what it no longer registers", count_titled(cmds, "Say Hello") == 0);

        // Throttling: a poll inside the interval must not stat/reload.
        write_script(root / "alpha.fake", "CMD third Third\n");
        touch_newer(root / "alpha.fake");
        sys.poll(cmds, api, 0.01f);
        check("polling is throttled", count_titled(cmds, "Third") == 0);
        sys.poll(cmds, api, 10.0f);
        check("and catches up on the next tick", count_titled(cmds, "Third") == 1);
    }

    std::printf("\n[group] failures are surfaced, not swallowed\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        fs::remove(root / "alpha.fake", ec);
        write_script(root / "broken.fake", "FAIL_CREATE\n");
        write_script(root / "halfway.fake", "FAIL_START\nCMD x Never Registered\n");
        write_script(root / "working.fake", "CMD ok Works\n");
        sys.load_all(root, cmds, api);

        check("all three are listed", sys.extensions().size() == 3);
        check("two are marked failed", sys.failed_count() == 2);

        // A broken extension must remain VISIBLE. One that vanishes on failure
        // is indistinguishable from one that was never installed.
        bool broken_listed = false, has_reason = false;
        for (const auto& e : sys.extensions())
            if (e.name == "broken") { broken_listed = true; has_reason = !e.error.empty(); }
        check("a script that will not load stays listed", broken_listed);
        check("and carries the reason", has_reason);

        check("a script failing on_start registers nothing", count_titled(cmds, "Never Registered") == 0);
        check("a healthy sibling still loads", count_titled(cmds, "Works") == 1);
    }

    std::printf("\n[group] a command whose token is missing\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        fs::remove_all(root, ec); fs::create_directories(root, ec);
        write_script(root / "typo.fake", "BADTOKEN gone\nCMD gone Points Nowhere\n");
        sys.load_all(root, cmds, api);

        check("the command registers", count_titled(cmds, "Points Nowhere") == 1);
        check("extension starts healthy", sys.extensions()[0].error.empty());
        cmds.run_by_title("Points Nowhere");
        // Clicking a command that cannot run must say so somewhere a person
        // looks. Nothing happening, with nothing logged against the extension,
        // is the failure this asserts against.
        check("running it records the error on the extension", !sys.extensions()[0].error.empty());
    }

    std::printf("\n[group] a deleted file gives its commands back\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        fs::remove_all(root, ec); fs::create_directories(root, ec);
        write_script(root / "temp.fake", "CMD t Temporary\n");
        sys.load_all(root, cmds, api);
        check("registered while present", count_titled(cmds, "Temporary") == 1);

        fs::remove(root / "temp.fake", ec);
        sys.poll(cmds, api, 10.0f);
        check("its command is withdrawn", count_titled(cmds, "Temporary") == 0);
        check("but it stays listed with a reason", sys.extensions().size() == 1 &&
                                                   !sys.extensions()[0].error.empty());
    }

    std::printf("\n[group] guards\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        cmds.add("Save Scene", "File", "Ctrl+S", [] {});

        // remove_owned_by("") must never be a mass delete of the built-ins.
        check("removing the empty owner removes nothing", cmds.remove_owned_by("") == 0);
        check("built-ins survive", count_titled(cmds, "Save Scene") == 1);

        // register_command outside a load has no owner to attribute to.
        check("registering outside a load is refused",
              !sys.register_command_from_script(cmds, "Sneaky", "Script", "tok"));
        check("and nothing was added", count_titled(cmds, "Sneaky") == 0);

        // A missing folder is the normal case, not an error.
        ExtensionSystem empty;
        empty.register_host(".fake", std::make_unique<FakeHost>());
        empty.load_all(root / "does_not_exist", cmds, api);
        check("a project with no extensions folder loads cleanly", empty.extensions().empty());
    }

    std::printf("\n[group] the starter template\n");
    {
        const fs::path t = root / "gen" / "my_extension.py";
        std::string err;
        check("template is written", ExtensionSystem::write_template(t, &err));
        check("it will not overwrite", !ExtensionSystem::write_template(t, &err));

        std::ifstream in(t);
        const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // A template that does not demonstrate the callback token teaches the
        // one thing about this API that cannot be guessed.
        check("it defines on_start", body.find("def on_start") != std::string::npos);
        check("it calls register_command", body.find("register_command(") != std::string::npos);
        check("it defines the function the token names",
              body.find("def count_entities") != std::string::npos);
    }

    // -----------------------------------------------------------------------
    // The real backend. Everything above proves the loader; none of it proves
    // that a .py file a person writes reaches the editor, which is the only
    // claim that matters to them.
    // -----------------------------------------------------------------------
    std::printf("\n[group] the real Python backend\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".py", make_python_host());
        fs::remove_all(root, ec); fs::create_directories(root, ec);

        // Capture what the script logs, so "the Python function ran" is
        // observed rather than inferred from a return code.
        logged.clear();
        api.log = [](void*, const char* m) { logged.push_back(m ? m : ""); };

        write_script(root / "real.py",
                     "import engine\n"
                     "\n"
                     "def on_start(e):\n"
                     "    engine.register_command(\"Py Command\", \"Script\", \"do_it\")\n"
                     "    engine.register_command(\"Py Missing\", \"Script\", \"not_defined\")\n"
                     "\n"
                     "def do_it():\n"
                     "    engine.log(\"python-command-ran\")\n");
        sys.load_all(root, cmds, api);

        check("a real .py extension loads", sys.extensions().size() == 1 &&
                                            sys.extensions()[0].error.empty());
        check("Python registered its commands", sys.total_commands() == 2);

        cmds.run_by_title("Py Command");
        check("running it executed the Python function",
              logged.size() == 1 && logged[0] == "python-command-ran");

        // A token naming no function is the likeliest authoring mistake, and it
        // must land somewhere the author will see.
        cmds.run_by_title("Py Missing");
        check("a token with no function is reported, not silent",
              sys.extensions()[0].error.find("not_defined") != std::string::npos);
    }

    std::printf("\n[group] the SHIPPED template runs under real Python\n");
    {
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".py", make_python_host());
        fs::remove_all(root, ec); fs::create_directories(root, ec);

        std::string terr;
        ExtensionSystem::write_template(root / "my_extension.py", &terr);
        sys.load_all(root, cmds, api);

        // The template is what every user starts from. A template that does not
        // load means the first thing anyone tries with this feature fails, and
        // no amount of correct loader code makes up for that.
        check("the template we hand users actually loads",
              sys.extensions().size() == 1 && sys.extensions()[0].error.empty());
        check("and registers the commands it advertises", sys.total_commands() == 2);
        check("its commands are in the palette",
              count_titled(cmds, "Count Entities") == 1 &&
              count_titled(cmds, "Select First Entity") == 1);
    }

    std::printf("\n[group] four copies of the same extension (a real user setup)\n");
    {
        // Reported from the field: "New Extension..." clicked four times leaves
        // my_extension.py plus _1/_2/_3, all identical, all registering the same
        // two command TITLES. Four backends, four owners, one title each.
        CommandRegistry cmds;
        ExtensionSystem sys;
        ctx.cmds = &cmds; ctx.sys = &sys;
        sys.register_host(".fake", std::make_unique<FakeHost>());
        fs::remove_all(root, ec); fs::create_directories(root, ec);
        for (int i = 0; i < 4; ++i)
            write_script(root / ("dup_" + std::to_string(i) + ".fake"),
                         "CMD hello Count Entities\n" "CMD first Select First\n");
        sys.load_all(root, cmds, api);

        check("all four load", sys.extensions().size() == 4);
        check("all four are healthy", sys.failed_count() == 0);
        check("each registered its own two", sys.total_commands() == 8);
        check("the title appears four times", count_titled(cmds, "Count Entities") == 4);

        // Reloading repeatedly must not accumulate. This is the case the owner
        // tag exists for, now with four owners sharing a title.
        for (int i = 0; i < 5; ++i) sys.reload_all(cmds, api);
        check("five reloads leave exactly four", count_titled(cmds, "Count Entities") == 4);
        check("and the total is stable", sys.total_commands() == 8);

        // Invoking by title must reach a LIVE instance, not a destroyed one.
        FakeInstance::invoked.clear();
        check("running the shared title works after reloads",
              cmds.run_by_title("Count Entities") && FakeInstance::invoked.size() == 1);

        // Deleting one file must not disturb the other three.
        fs::remove(root / "dup_2.fake", ec);
        sys.poll(cmds, api, 10.0f);
        check("removing one leaves three", count_titled(cmds, "Count Entities") == 3);
        check("the others still run", cmds.run_by_title("Count Entities"));
        check("the removed one is listed with a reason", sys.failed_count() == 1);
    }


    fs::remove_all(root, ec);
    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
