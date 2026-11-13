// ============================================================================
// uidoc_check -- UI documents (ui/ui_document.h): a game's screens as .ui
// files. Parsing, placement on screens of other sizes, live values, buttons,
// setting text, and lines it cannot read.
// ============================================================================
#include "ui/ui_document.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace schizo::ui;

static int g_fail = 0, g_pass = 0;
static void check(const char* what, bool ok) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}
static bool close_to(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    const char* menu =
        "# Pause menu\n"
        "ui modal pause layer=10\n"
        "panel  id=dim    anchor=fill color=0,0,0,0.55\n"
        "label  id=title  anchor=center x=0 y=-150 w=600 h=70 text=\"Paused for now\" size=54 align=center\n"
        "button id=resume anchor=center x=0 y=-40 w=360 h=64 text=\"Resume\" size=26\n"
        "button id=quit   anchor=bottom_right x=-20 y=-20 w=200 h=50 text=\"Quit\"\n"
        "bar    id=hp     anchor=top_left x=24 y=24 w=320 h=22 color=0.85,0.2,0.2,1 bind=player.Health\n"
        "label  id=ammo   anchor=bottom x=0 y=-30 w=300 h=30 text=\"{player.ammo} / {player.reserve} {flag.x}\"\n";
    auto d = parse_ui_document(menu, "pause.ui");
    check("options: modal, pause, layer", d->modal && d->pause && d->layer == 10);
    check("widgets by id, buttons listed", d->ids.size() == 6 && d->buttons.size() == 2 && d->buttons[0].first == "resume" &&
                                           d->errors.empty());
    check("quoted text keeps its spaces", dynamic_cast<Label*>(d->ids["title"])->text == "Paused for now");

    // Placement on the reference screen and on one half its size.
    d->canvas.layout(1920.0f, 1080.0f);
    const UiRect r = d->ids["resume"]->rect;
    check("center anchor: the widget's centre sits at the anchor, moved by x,y",
          close_to(r.cx(), 960.0f) && close_to(r.cy(), 540.0f - 40.0f) && close_to(r.w, 360.0f));
    const UiRect q = d->ids["quit"]->rect;
    check("bottom_right anchor: its corner sits at the screen's corner, inset by x,y",
          close_to(q.x + q.w, 1900.0f) && close_to(q.y + q.h, 1060.0f));
    const UiRect f = d->ids["dim"]->rect;
    check("fill covers the screen", close_to(f.w, 1920.0f) && close_to(f.h, 1080.0f));
    d->canvas.layout(960.0f, 540.0f);
    const UiRect r2 = d->ids["resume"]->rect;
    check("a smaller screen scales it all", close_to(r2.cx(), 480.0f) && close_to(r2.cy(), 270.0f - 20.0f) && close_to(r2.w, 180.0f));

    // Live values.
    refresh_ui_bindings(*d, [](const std::string& s) {
        UiValue v;
        if (s == "player.Health") { v.found = true; v.value = 30; v.max = 120; }
        if (s == "player.ammo")   { v.found = true; v.value = 17; }
        if (s == "player.reserve"){ v.found = true; v.value = 90; }
        return v;
    });
    check("{placeholders} and bound bars read the game; unknown ones show '?'",
          dynamic_cast<Label*>(d->ids["ammo"])->text == "17 / 90 ?" &&
          close_to(dynamic_cast<ProgressBar*>(d->ids["hp"])->fraction, 0.25f));

    // A click on a button.
    int clicks = 0;
    d->buttons[0].second->on_click = [&clicks] { ++clicks; };
    d->canvas.layout(1920.0f, 1080.0f);
    d->canvas.pointer_move(960.0f, 500.0f);
    d->canvas.pointer_button(true);
    d->canvas.pointer_button(false);
    d->canvas.pointer_move(10.0f, 10.0f);
    d->canvas.pointer_button(true);
    d->canvas.pointer_button(false);
    check("a press and release on a button clicks it; elsewhere nothing", clicks == 1);

    // Text set from a script.
    const bool set = set_ui_text(*d, "ammo", "empty");
    refresh_ui_bindings(*d, [](const std::string&) { UiValue v; v.found = true; v.value = 1; return v; });
    check("set text: a label keeps it (no longer follows its placeholders)",
          set && dynamic_cast<Label*>(d->ids["ammo"])->text == "empty" && !set_ui_text(*d, "nope", "x"));

    // A button's text can follow a live value too (a controls menu: {key.jump}).
    {
        auto c = parse_ui_document("button id=jump text=\"{key.jump}\"\n");
        refresh_ui_bindings(*c, [](const std::string& s) { UiValue v; v.found = s == "key.jump"; v.text = "Space"; return v; });
        check("a button's text follows its {placeholder}", dynamic_cast<Button*>(c->ids["jump"])->text == "Space");
    }

    // A slots grid (an inventory): contents from the game, a click picks a
    // cell, the selection's placeholders, the click reported with its cell.
    {
        auto inv = parse_ui_document(
            "slots id=bag anchor=top_left x=0 y=0 w=600 h=200 cols=6 rows=2 bind=player.inventory\n"
            "label id=picked anchor=top_left x=0 y=300 w=400 h=40 text=\"{bag.name} ({bag.count})\"\n"
            "label id=about anchor=top_left x=0 y=350 w=400 h=40 text=\"{bag.info}\"\n"
            "button id=use anchor=top_left x=0 y=400 w=200 h=50 text=\"Use\" enabled=bag.count\n");
        auto slots = [](const std::string& s) {
            std::vector<UiSlot> v;
            if (s == "player.inventory") { v.push_back({"Potion", 3, ""}); v.push_back({"Rounds", 120, "", "For the rifle"}); }
            return v;
        };
        auto none = [](const std::string&) { return UiValue{}; };
        inv->canvas.layout(1920.0f, 1080.0f);
        refresh_ui_bindings(*inv, none, slots);
        auto* grid = dynamic_cast<SlotGrid*>(inv->ids["bag"]);
        auto* use = dynamic_cast<Button*>(inv->ids["use"]);
        const bool before = grid->slots.size() == 2 && grid->selected == -1 && !use->interactive;
        int clicked = -1;
        grid->on_click = [&clicked](int i) { clicked = i; };
        inv->canvas.pointer_move(150.0f, 50.0f);      // the second cell of the first row (cells 100 wide)
        inv->canvas.pointer_button(true);
        inv->canvas.pointer_button(false);
        refresh_ui_bindings(*inv, none, slots);
        check("slots: filled from the game; a click picks the cell and reports it",
              before && clicked == 1 && grid->selected == 1);
        check("slots: the selection's {name}, {count} and {info}, and a button enabled by it",
              dynamic_cast<Label*>(inv->ids["picked"])->text == "Rounds (120)" &&
              dynamic_cast<Label*>(inv->ids["about"])->text == "For the rifle" && use->interactive);
    }

    // Weapon slots: empty cells, the gun in hand framed, a button enabled
    // while the picked cell has a gun.
    {
        auto doc = parse_ui_document(
            "slots id=guns anchor=top_left x=0 y=0 w=300 h=100 cols=3 rows=1 bind=player.weapons frame=0,1,0,1\n"
            "button id=stow anchor=top_left x=0 y=200 w=200 h=50 text=\"Put in bag\" enabled=guns.filled\n");
        auto guns = [](const std::string& s) {
            std::vector<UiSlot> v(3);
            if (s == "player.weapons") { v[0] = {"Rifle", 30, "in hand"}; v[0].marked = true; v[2] = {"Pistol", 12, ""}; }
            return v;
        };
        auto none = [](const std::string&) { return UiValue{}; };
        doc->canvas.layout(1920.0f, 1080.0f);
        refresh_ui_bindings(*doc, none, guns);
        auto* grid = dynamic_cast<SlotGrid*>(doc->ids["guns"]);
        auto* stow = dynamic_cast<Button*>(doc->ids["stow"]);
        UiDrawList dl;
        grid->paint_self(dl);
        int frames = 0, texts = 0;
        for (const auto& c : dl) {
            if (c.type == UiCmdType::Rect && c.color.g == 1.0f && c.color.r == 0.0f) ++frames;
            if (c.type == UiCmdType::Text) ++texts;
        }
        grid->selected = 1;   // the empty slot
        refresh_ui_bindings(*doc, none, guns);
        const bool empty_off = !stow->interactive;
        grid->selected = 2;
        refresh_ui_bindings(*doc, none, guns);
        check("slots: empty cells stay empty, the marked one is framed (frame=), {grid.filled} enables a button",
              frames == 1 && texts == 4 && empty_off && stow->interactive);
    }

    // Lines it cannot read.
    auto bad = parse_ui_document("ui modal sideways\nslider id=s\nlabel id=l anchor=middle text=hi\nbutton text=\"No id\"\n");
    check("bad lines are reported with their line numbers, the rest still builds",
          bad->errors.size() == 4 && bad->errors[0].rfind("line 1:", 0) == 0 && bad->errors[1].rfind("line 2:", 0) == 0 &&
          bad->ids.count("l") == 1);

    std::printf("\nuidoc_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("ALL OK\n");
    return g_fail ? 1 : 0;
}
