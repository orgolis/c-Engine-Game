// ============================================================
// script_host_logic.cpp -- logic graphs as a script backend
// ============================================================
//
// A fourth ScriptHost next to Python, C++ and C#. A ".lgraph" file on an
// entity's Script component is a visual script (vscript/io.h text): it is
// compiled when the entity is first seen in Play mode, "Begin Play" fires
// once, "Tick" every frame, and the file hot-reloads like any other script.
//
// Every entry of the ScriptApi table is registered as a node here, so what a
// Python script can call, a graph can call. An engine capability added to
// ScriptApi needs one more line in register_engine_nodes().

#include "script_system.h"
#include "logic_debug.h"
#include "logic_nodes.h"

#include "vscript/io.h"
#include "vscript/program.h"
#include "vscript/vm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

namespace {

using namespace schizo::vs;

using Shared = LogicApiSlot;

glm::vec3 v3(const float f[3]) { return glm::vec3(f[0], f[1], f[2]); }

}  // namespace

void register_engine_logic_nodes(schizo::vs::Registry& r, LogicApiSlot* s) {
    using namespace schizo::vs;
    // The lambdas hold `s`, not the table: the table pointer is refreshed every
    // frame, and a missing entry (a test harness fills only some) is a no-op.
#define API (s->api)
    const char* E = "Entity";
    r.pure("entity.find", E, "Find Entity", [s](std::string name) {
        return EntityId{API && API->find_entity ? API->find_entity(API->ctx, name.c_str()) : 0u};
    }, {"Name", "Entity"}).tip("The entity with this name, or none.");
    r.pure("entity.is_valid", E, "Is Valid", [](EntityId e) { return e.id != 0; }, {"Entity", "Valid"});
    r.pure("entity.get_position", E, "Get Position", [s](EntityId e) {
        float p[3] = {0, 0, 0};
        if (API && API->get_position) API->get_position(API->ctx, e.id, p);
        return v3(p);
    }, {"Entity", "Position"});
    r.fn("entity.set_position", E, "Set Position", [s](EntityId e, glm::vec3 p) {
        if (API && API->set_position) API->set_position(API->ctx, e.id, &p.x);
    }, {"Entity", "Position"});
    r.pure("entity.get_rotation", E, "Get Rotation", [s](EntityId e) {
        float p[3] = {0, 0, 0};
        if (API && API->get_rotation_euler) API->get_rotation_euler(API->ctx, e.id, p);
        return v3(p);
    }, {"Entity", "Degrees"});
    r.fn("entity.set_rotation", E, "Set Rotation", [s](EntityId e, glm::vec3 deg) {
        if (API && API->set_rotation_euler) API->set_rotation_euler(API->ctx, e.id, &deg.x);
    }, {"Entity", "Degrees"});
    r.pure("entity.get_scale", E, "Get Scale", [s](EntityId e) {
        float p[3] = {1, 1, 1};
        if (API && API->get_scale) API->get_scale(API->ctx, e.id, p);
        return v3(p);
    }, {"Entity", "Scale"});
    r.fn("entity.set_scale", E, "Set Scale", [s](EntityId e, glm::vec3 sc) {
        if (API && API->set_scale) API->set_scale(API->ctx, e.id, &sc.x);
    }, {"Entity", "Scale"});
    r.fn("entity.translate", E, "Move By", [s](EntityId e, glm::vec3 d) {
        if (API && API->translate) API->translate(API->ctx, e.id, &d.x);
    }, {"Entity", "Delta"});
    r.pure("entity.forward", E, "Get Forward", [s](EntityId e) {
        float p[3] = {0, 0, -1};
        if (API && API->get_forward) API->get_forward(API->ctx, e.id, p);
        return v3(p);
    }, {"Entity", "Forward"});
    r.pure("entity.distance", E, "Distance Between", [s](EntityId a, EntityId b) {
        return double(API && API->distance ? API->distance(API->ctx, a.id, b.id) : 0.0f);
    }, {"A", "B", "Distance"});
    r.fn("entity.spawn_primitive", E, "Spawn Primitive",
         [s](int64_t shape, glm::vec3 pos, double size, ColorValue color, bool dynamic) {
             return EntityId{API && API->spawn_primitive
                                 ? API->spawn_primitive(API->ctx, int(shape), &pos.x, float(size), &color.rgba.x, dynamic ? 1 : 0)
                                 : 0u};
         }, {"Shape", "Position", "Size", "Color", "Dynamic", "Entity"})
        .tip("Shape: 0 cube, 1 sphere. Dynamic gives it a rigid body.");
    r.fn("entity.destroy", E, "Destroy Entity", [s](EntityId e) {
        if (API && API->destroy_entity) API->destroy_entity(API->ctx, e.id);
    }, {"Entity"});
    r.fn("entity.set_color", "Rendering", "Set Color", [s](EntityId e, ColorValue c) {
        if (API && API->set_color) API->set_color(API->ctx, e.id, &c.rgba.x);
    }, {"Entity", "Color"});
    r.fn("entity.set_emissive", "Rendering", "Set Emissive", [s](EntityId e, glm::vec3 rgb, double intensity) {
        if (API && API->set_emissive) API->set_emissive(API->ctx, e.id, &rgb.x, float(intensity));
    }, {"Entity", "Color", "Intensity"});

    const char* I = "Input";
    r.pure("input.key_down", I, "Is Key Down", [s](int64_t key) {
        return API && API->key_down && API->key_down(API->ctx, int(key));
    }, {"Key", "Down"}).tip("Key is a GLFW key code: 32 space, 65 A, 87 W ...");
    r.pure("input.mouse_down", I, "Is Mouse Button Down", [s](int64_t button) {
        return API && API->mouse_down && API->mouse_down(API->ctx, int(button));
    }, {"Button", "Down"});
    r.pure("input.mouse_delta", I, "Mouse Delta", [s]() {
        float d[2] = {0, 0};
        if (API && API->mouse_delta) API->mouse_delta(API->ctx, d);
        return glm::vec2(d[0], d[1]);
    }, {"Delta"});

    const char* P = "Physics";
    r.fn("physics.set_velocity", P, "Set Velocity", [s](EntityId e, glm::vec3 v) {
        if (API && API->set_velocity) API->set_velocity(API->ctx, e.id, &v.x);
    }, {"Entity", "Velocity"});
    r.fn("physics.add_impulse", P, "Add Impulse", [s](EntityId e, glm::vec3 v) {
        if (API && API->add_impulse) API->add_impulse(API->ctx, e.id, &v.x);
    }, {"Entity", "Impulse"});
    r.fn("physics.raycast", P, "Raycast", [s](glm::vec3 origin, glm::vec3 dir, double max_dist) {
        float    pos[3] = {0, 0, 0};
        uint32_t hit = 0;
        const bool ok = API && API->raycast && API->raycast(API->ctx, &origin.x, &dir.x, float(max_dist), pos, &hit);
        return std::make_tuple(ok, v3(pos), EntityId{hit});
    }, {"Origin", "Direction", "Max Distance", "Hit", "Position", "Entity"});

    const char* A = "Audio";
    r.fn("audio.play", A, "Play Audio", [s](EntityId e) { if (API && API->audio_play) API->audio_play(API->ctx, e.id); }, {"Entity"});
    r.fn("audio.stop", A, "Stop Audio", [s](EntityId e) { if (API && API->audio_stop) API->audio_stop(API->ctx, e.id); }, {"Entity"});

    const char* G = "Gameplay";
    r.pure("gameplay.get_attribute", G, "Get Attribute", [s](EntityId e, std::string n) {
        return double(API && API->get_attribute ? API->get_attribute(API->ctx, e.id, n.c_str()) : 0.0f);
    }, {"Entity", "Attribute", "Value"});
    r.fn("gameplay.set_attribute", G, "Set Attribute", [s](EntityId e, std::string n, double v) {
        if (API && API->set_attribute) API->set_attribute(API->ctx, e.id, n.c_str(), float(v));
    }, {"Entity", "Attribute", "Value"});
    r.fn("gameplay.adjust_attribute", G, "Adjust Attribute", [s](EntityId e, std::string n, double d) {
        if (API && API->adjust_attribute) API->adjust_attribute(API->ctx, e.id, n.c_str(), float(d));
    }, {"Entity", "Attribute", "Delta"});
    r.pure("gameplay.has_tag", G, "Has Tag", [s](EntityId e, std::string t) {
        return API && API->has_tag && API->has_tag(API->ctx, e.id, t.c_str());
    }, {"Entity", "Tag", "Result"});
    r.fn("gameplay.add_tag", G, "Add Tag", [s](EntityId e, std::string t) {
        if (API && API->add_tag) API->add_tag(API->ctx, e.id, t.c_str());
    }, {"Entity", "Tag"});
    r.fn("gameplay.remove_tag", G, "Remove Tag", [s](EntityId e, std::string t) {
        if (API && API->remove_tag) API->remove_tag(API->ctx, e.id, t.c_str());
    }, {"Entity", "Tag"});
    r.fn("gameplay.apply_damage", G, "Apply Damage", [s](EntityId e, double amount, std::string type) {
        return double(API && API->apply_damage ? API->apply_damage(API->ctx, e.id, float(amount), type.c_str()) : 0.0f);
    }, {"Target", "Amount", "Type", "Dealt"});
    r.fn("gameplay.apply_heal", G, "Apply Heal", [s](EntityId e, std::string attr, double amount) {
        if (API && API->apply_heal) API->apply_heal(API->ctx, e.id, attr.c_str(), float(amount));
    }, {"Target", "Attribute", "Amount"});
    r.fn("gameplay.activate_ability", G, "Activate Ability", [s](EntityId e, int64_t index, EntityId target) {
        return API && API->activate_ability && API->activate_ability(API->ctx, e.id, int(index), target.id);
    }, {"Entity", "Index", "Target", "Activated"});
    r.fn("gameplay.grant_xp", G, "Grant XP", [s](EntityId e, double amount) {
        if (API && API->grant_xp) API->grant_xp(API->ctx, e.id, float(amount));
    }, {"Entity", "Amount"});
    r.pure("gameplay.get_level", G, "Get Level", [s](EntityId e) {
        return int64_t(API && API->get_level ? API->get_level(API->ctx, e.id) : 0);
    }, {"Entity", "Level"});
    r.fn("gameplay.unlock_skill", G, "Unlock Skill", [s](EntityId e, std::string node) {
        return API && API->unlock_skill && API->unlock_skill(API->ctx, e.id, node.c_str());
    }, {"Entity", "Skill", "Unlocked"});
    r.fn("gameplay.send_state_event", G, "Send State Event", [s](EntityId e, std::string ev) {
        return API && API->send_state_event && API->send_state_event(API->ctx, e.id, ev.c_str());
    }, {"Entity", "Event", "Changed"});
    r.pure("gameplay.in_state", G, "Is In State", [s](EntityId e, std::string st) {
        return API && API->in_state && API->in_state(API->ctx, e.id, st.c_str());
    }, {"Entity", "State", "Result"});
    r.fn("gameplay.interact", G, "Interact", [s](EntityId e) {
        return API && API->interact && API->interact(API->ctx, e.id);
    }, {"Entity", "Interacted"});
    r.fn("gameplay.drive", G, "Drive Vehicle", [s](EntityId e, double throttle, double steer, bool brake, bool boost) {
        if (API && API->drive) API->drive(API->ctx, e.id, float(throttle), float(steer), brake ? 1 : 0, boost ? 1 : 0);
    }, {"Vehicle", "Throttle", "Steer", "Brake", "Boost"});
    r.fn("gameplay.toggle_flashlight", G, "Toggle Flashlight", [s](EntityId e) {
        return API && API->toggle_flashlight && API->toggle_flashlight(API->ctx, e.id);
    }, {"Entity", "On"});

    const char* IT = "Gameplay|Items";
    r.fn("items.add", IT, "Add Item", [s](EntityId e, std::string def, int64_t qty) {
        return int64_t(API && API->add_item ? API->add_item(API->ctx, e.id, def.c_str(), int(qty)) : qty);
    }, {"Entity", "Item", "Quantity", "Leftover"});
    r.pure("items.count", IT, "Item Count", [s](EntityId e, std::string def) {
        return int64_t(API && API->item_count ? API->item_count(API->ctx, e.id, def.c_str()) : 0);
    }, {"Entity", "Item", "Count"});
    r.fn("items.equip", IT, "Equip Item", [s](EntityId e, std::string def) {
        return API && API->equip_item && API->equip_item(API->ctx, e.id, def.c_str());
    }, {"Entity", "Item", "Equipped"});
    r.fn("items.use", IT, "Use Item", [s](EntityId e, std::string def) {
        return API && API->use_item && API->use_item(API->ctx, e.id, def.c_str());
    }, {"Entity", "Item", "Used"});
    r.fn("weapons.fire", "Gameplay|Weapons", "Fire Weapon", [s](EntityId e) {
        return API && API->fire_weapon && API->fire_weapon(API->ctx, e.id);
    }, {"Entity", "Fired"});
    r.fn("weapons.reload", "Gameplay|Weapons", "Reload Weapon", [s](EntityId e) {
        return API && API->reload_weapon && API->reload_weapon(API->ctx, e.id);
    }, {"Entity", "Reloading"});

    const char* W = "World";
    r.pure("world.get_flag", W, "Get Flag", [s](std::string key) {
        return int64_t(API && API->get_flag ? API->get_flag(API->ctx, key.c_str()) : 0);
    }, {"Key", "Value"});
    r.fn("world.set_flag", W, "Set Flag", [s](std::string key, int64_t v) {
        if (API && API->set_flag) API->set_flag(API->ctx, key.c_str(), int(v));
    }, {"Key", "Value"});
    r.fn("world.emit_event", W, "Emit Event", [s](std::string name) {
        if (API && API->emit_event) API->emit_event(API->ctx, name.c_str());
    }, {"Name"}).tip("Publishes a named event on the gameplay bus.");
    r.pure("time.delta", "Utilities", "Delta Seconds", [s]() { return double(API ? API->dt : 0.0f); }, {"Seconds"});
    r.pure("time.now", "Utilities", "Game Time", [s]() { return API ? API->time : 0.0; }, {"Seconds"});

    // Events the engine raises. Node::name says which key / which bus event,
    // so one script can have several of each.
    {
        NodeDef d;
        d.id = "event.key_pressed"; d.category = "Events|Input"; d.title = "Key Pressed";
        d.builtin = Builtin::Event; d.named = true; d.name_label = "Key";
        d.tooltip = "Runs once when the key goes down. Key: a letter or digit, Space, Enter, Escape, Shift, Ctrl, Tab, Up, Down, Left, Right, or a GLFW key code.";
        d.out("Then", kExec);
        r.add(std::move(d));
    }
    {
        NodeDef d;
        d.id = "event.key_released"; d.category = "Events|Input"; d.title = "Key Released";
        d.builtin = Builtin::Event; d.named = true; d.name_label = "Key";
        d.out("Then", kExec);
        r.add(std::move(d));
    }
    {
        NodeDef d;
        d.id = "event.gameplay"; d.category = "Events"; d.title = "On Gameplay Event";
        d.builtin = Builtin::Event; d.named = true; d.name_label = "Event";
        d.tooltip = "Runs when an event with this name is published on the gameplay bus: a trigger volume's enter or exit event, damage, a pickup, or one sent with Emit Event. It arrives the frame after it happened.";
        d.out("Then", kExec).out("Instigator", kEntity).out("Target", kEntity).out("Value", kFloat).out("Param", kString);
        r.add(std::move(d));
    }

    {
        NodeDef d;
        d.id = "event.about_me"; d.category = "Events"; d.title = "On Event About Me";
        d.builtin = Builtin::Event; d.named = true; d.name_label = "Event";
        d.tooltip = "Like On Gameplay Event, but only when this object is the event's target or its instigator: damage dealt to it, an item it picked up, an actor entering it when it is a trigger volume.";
        d.out("Then", kExec).out("Instigator", kEntity).out("Target", kEntity).out("Value", kFloat).out("Param", kString);
        r.add(std::move(d));
    }
    {
        NodeDef d;
        d.id = "event.trigger_enter"; d.category = "Events"; d.title = "On Trigger Enter";
        d.builtin = Builtin::Event;
        d.tooltip = "For a script on a trigger volume: runs when an actor enters it. (Listens for the volume's default event, trigger.enter; a volume with its own event names needs On Event About Me.)";
        d.out("Then", kExec).out("Actor", kEntity);
        r.add(std::move(d));
    }
    {
        NodeDef d;
        d.id = "event.trigger_exit"; d.category = "Events"; d.title = "On Trigger Exit";
        d.builtin = Builtin::Event;
        d.tooltip = "For a script on a trigger volume: runs when an actor leaves it.";
        d.out("Then", kExec).out("Actor", kEntity);
        r.add(std::move(d));
    }

    const char* SP = "Script Parameters";
    r.pure("param.float", SP, "Get Float Parameter", [s](EntityId e, std::string n, double def) {
        return double(API && API->get_param_float ? API->get_param_float(API->ctx, e.id, n.c_str(), float(def)) : float(def));
    }, {"Entity", "Name", "Default", "Value"});
    r.pure("param.int", SP, "Get Int Parameter", [s](EntityId e, std::string n, int64_t def) {
        return int64_t(API && API->get_param_int ? API->get_param_int(API->ctx, e.id, n.c_str(), int(def)) : int(def));
    }, {"Entity", "Name", "Default", "Value"});
    r.pure("param.bool", SP, "Get Bool Parameter", [s](EntityId e, std::string n, bool def) {
        return API && API->get_param_bool ? API->get_param_bool(API->ctx, e.id, n.c_str(), def ? 1 : 0) : def;
    }, {"Entity", "Name", "Default", "Value"});
#undef API
}

namespace {

using namespace schizo::vs;

struct LogicRuntime;
LogicRuntime* g_debug_runtime = nullptr;   // what logic_debugger() hands out

/// A script's work for one frame, in steps, so that a frame a breakpoint
/// interrupted can go on from where it stopped.
struct FrameWork {
    virtual ~FrameWork() = default;
    /// Do (the rest of) this frame's work. False when a breakpoint stopped it.
    virtual bool run_frame() = 0;
    /// Start a frame that never began because another script was stopped.
    virtual void begin_frame(float dt) = 0;
};

/// A script file by one spelling, however the caller wrote the path.
std::string path_key(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path c = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : c.generic_string();
}

double wall_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// One registry and one VM for every logic script: programs are small, the
/// VM's stack is not. Also the debugger the graph editor talks to.
struct LogicRuntime : Host, LogicDebugger {
    Registry reg;
    Shared   shared;
    VM       vm{this};
    double   last_tick_time = -1.0;
    std::string last_error;

    // A compiled script, shared by every object that runs that file.
    struct Compiled {
        std::filesystem::file_time_type stamp;
        uintmax_t                       size = 0;
        std::shared_ptr<const Program>  program;
    };
    std::unordered_map<std::string, Compiled> programs;   // by path_key

    // Which script is on which entity: for the nodes that reach another
    // entity's script (Send Event to Entity, Get / Set Variable of Entity),
    // and for the debugger.
    struct Live {
        Instance*                      inst = nullptr;
        std::string                    path;      // path_key of the file
        std::shared_ptr<const Program> program;
    };
    std::unordered_map<uint32_t, Live> live;

    // Breakpoints by script file, kept whether or not anything is running.
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> breakpoints;
    // The frame a breakpoint interrupted: the script whose work was cut short,
    // and the scripts whose turn had not come. Continue finishes it, so a
    // breakpoint costs nobody their Tick.
    FrameWork* interrupted = nullptr;
    struct Waiting {
        FrameWork* work;
        float      dt;
    };
    std::vector<Waiting> waiting;

    void wait_turn(FrameWork* w, float dt) {
        for (const Waiting& x : waiting)
            if (x.work == w) return;
        if (w != interrupted) waiting.push_back(Waiting{w, dt});
    }
    void gone(FrameWork* w) {
        if (interrupted == w) interrupted = nullptr;
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(), [&](const Waiting& x) { return x.work == w; }), waiting.end());
    }
    /// After a run continued and finished: the rest of the interrupted frame.
    void finish_frame() {
        while (vm.state() != RunState::Paused) {
            FrameWork* w = interrupted;
            interrupted = nullptr;
            if (!w) {
                if (waiting.empty()) break;
                w = waiting.front().work;
                w->begin_frame(waiting.front().dt);
                waiting.erase(waiting.begin());
            }
            if (!w->run_frame()) {
                interrupted = w;
                break;
            }
        }
    }
    double trace_until = 0.0;   // tracing stays on until this wall-clock time

    Instance* find_instance(uint32_t entity) override {
        const auto it = live.find(entity);
        return it == live.end() ? nullptr : it->second.inst;
    }
    const Live* find_live(uint32_t entity) const {
        const auto it = live.find(entity);
        return it == live.end() || !it->second.inst ? nullptr : &it->second;
    }

    LogicRuntime() {
        register_standard_library(reg);
        register_engine_logic_nodes(reg, &shared);
    }
    ~LogicRuntime() override {
        if (g_debug_runtime == this) g_debug_runtime = nullptr;
    }

    /// Put the per-file breakpoints on the programs that are running now.
    void apply_breakpoints() {
        vm.clear_breakpoints();
        for (const auto& [entity, l] : live) {
            const auto it = breakpoints.find(l.path);
            if (it == breakpoints.end()) continue;
            for (const auto& [graph, node] : it->second) vm.set_breakpoint(l.program.get(), graph, node, true);
        }
    }
    /// Once a frame: the trace clock, and switching tracing off when nobody
    /// has asked for it lately.
    void frame() {
        if (!vm.trace) return;
        const double now = wall_seconds();
        vm.trace_time = now;
        if (now > trace_until) {
            vm.trace = false;
            vm.clear_trace();
        }
    }

    // ---- LogicDebugger ---------------------------------------------------
    std::vector<LogicDebugTarget> targets() const override {
        std::vector<LogicDebugTarget> out;
        for (const auto& [entity, l] : live)
            if (l.inst) out.push_back(LogicDebugTarget{entity, l.path});
        std::sort(out.begin(), out.end(), [](const LogicDebugTarget& a, const LogicDebugTarget& b) { return a.entity < b.entity; });
        return out;
    }
    void set_breakpoints(const std::string& path, const std::vector<std::pair<int, int>>& graph_node) override {
        const std::string key = path_key(path);
        if (graph_node.empty()) breakpoints.erase(key);
        else breakpoints[key] = graph_node;
        apply_breakpoints();
    }
    void want_tracing() override {
        const double now = wall_seconds();
        trace_until = now + 1.0;
        if (!vm.trace) {
            vm.trace = true;
            vm.trace_time = now;
        }
    }
    bool paused(LogicDebugTarget* where, int* graph, int* node) const override {
        if (vm.state() != RunState::Paused) return false;
        const Instance* pi = vm.paused_instance();
        const Where     w = vm.paused_at();
        if (where && pi) {
            where->entity = pi->self;
            const Live* l = find_live(pi->self);
            where->path = l ? l->path : std::string();
        }
        if (graph) *graph = w.graph;
        if (node) *node = w.node;
        return true;
    }
    void resume(LogicStep how) override {
        if (vm.state() != RunState::Paused) return;
        vm.trace_time = wall_seconds();
        vm.resume(how == LogicStep::Into ? VM::Step::Into : how == LogicStep::Over ? VM::Step::Over
                  : how == LogicStep::Out ? VM::Step::Out : VM::Step::Continue);
        finish_frame();
    }
    double seconds_since_run(uint32_t entity, int graph, int node) const override {
        const Live* l = find_live(entity);
        if (!l) return -1.0;
        const double t = vm.last_run(*l->inst, graph, node);
        return t < 0.0 ? -1.0 : std::max(0.0, wall_seconds() - t);
    }
    bool output_text(uint32_t entity, int graph, int node, int slot, std::string& text) const override {
        const Live* l = find_live(entity);
        if (!l || slot < 0) return false;
        const NodeOutputs* o = l->program->find_outputs(graph, node);
        const Value*       v = o ? vm.frame_register(*l->inst, o->fn, o->base + slot) : nullptr;
        if (!v) return false;
        text = display_string(*v);
        return true;
    }
    bool variable_text(uint32_t entity, const std::string& name, std::string& text) const override {
        const Live* l = find_live(entity);
        const Value* v = l ? l->inst->variable(name) : nullptr;
        if (!v) return false;
        text = display_string(*v);
        return true;
    }

    void print(const Instance&, std::string_view text) override {
        if (shared.api && shared.api->log) shared.api->log(shared.api->ctx, std::string(text).c_str());
    }
    void script_error(const Instance& inst, int graph, int node, std::string_view text) override {
        last_error = std::string(text);
        if (!shared.api || !shared.api->log) return;
        std::string where = inst.program ? inst.program->name : std::string("logic graph");
        if (inst.program && graph >= 0 && graph < static_cast<int>(inst.program->graph_names.size()))
            where += " / " + inst.program->graph_names[static_cast<size_t>(graph)];
        const std::string msg = "[logic] " + where + " node " + std::to_string(node) + ": " + std::string(text);
        shared.api->log(shared.api->ctx, msg.c_str());
    }
};

/// "A", "7", "Space", "Up" or a number -> GLFW key code; -1 when unknown.
int key_code_of(const std::string& s) {
    if (s.size() == 1) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    static const struct { const char* name; int code; } kNamed[] = {
        {"space", 32}, {"escape", 256}, {"enter", 257}, {"tab", 258}, {"backspace", 259},
        {"right", 262}, {"left", 263}, {"down", 264}, {"up", 265},
        {"shift", 340}, {"ctrl", 341}, {"alt", 342},
    };
    std::string low = s;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& k : kNamed)
        if (low == k.name) return k.code;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return end != s.c_str() && *end == 0 && v > 0 ? static_cast<int>(v) : -1;
}

class LogicInstance final : public ScriptInstance, public FrameWork {
public:
    LogicInstance(std::shared_ptr<LogicRuntime> rt, std::shared_ptr<const Program> program, std::string path,
                  const ScriptApi* api)
        : rt_(std::move(rt)), program_(std::move(program)), path_(std::move(path)), api_(api) {}
    ~LogicInstance() override {
        if (inst_) {
            rt_->shared.api = api_;
            // A run that is stopped at a breakpoint may be standing in this
            // script: it cannot be finished once the script is gone.
            if (rt_->vm.state() == RunState::Paused) rt_->vm.abort();
            rt_->gone(this);
            if (begun_) rt_->vm.fire(*inst_, "event.end_play");
            rt_->vm.forget(*inst_);
            // Only if the entry is still this script: on a hot reload the new
            // instance may already have taken the entity's slot.
            const auto it = rt_->live.find(inst_->self);
            if (it != rt_->live.end() && it->second.inst == inst_.get()) rt_->live.erase(it);
        }
    }

    bool start(uint32_t entity, std::string&) override {
        rt_->shared.api = api_;
        inst_ = rt_->vm.instantiate(*program_, entity);
        rt_->live[entity] = LogicRuntime::Live{inst_.get(), path_, program_};
        rt_->apply_breakpoints();
        apply_inspector_values(entity);
        // Which keys this script listens for, so update() polls only those.
        keys_.clear();
        for (const EventInfo& e : program_->events) {
            for (const bool pressed : {true, false}) {
                const std::string prefix = pressed ? "event.key_pressed:" : "event.key_released:";
                if (e.name.rfind(prefix, 0) != 0) continue;
                const int code = key_code_of(e.name.substr(prefix.size()));
                if (code < 0) continue;
                Key* k = nullptr;
                for (Key& x : keys_) if (x.code == code) k = &x;
                if (!k) { keys_.push_back(Key{}); k = &keys_.back(); k->code = code; }
                (pressed ? k->on_press : k->on_release) = e.name;
            }
            if (e.name.rfind("event.gameplay:", 0) == 0 || e.name.rfind("event.about_me:", 0) == 0 ||
                e.name == "event.trigger_enter" || e.name == "event.trigger_exit")
                wants_bus_ = true;
        }
        return true;
    }
    bool update(uint32_t, float dt, std::string&) override {
        rt_->shared.api = api_;
        VM& vm = rt_->vm;
        // Another script is stopped at a breakpoint: this one takes its turn
        // when that run continues.
        if (vm.state() == RunState::Paused) {
            rt_->wait_turn(this, dt);
            return true;
        }
        // Waits belong to the shared VM: advance them once per frame, not once
        // per script.
        if (api_ && api_->time != rt_->last_tick_time) {
            rt_->last_tick_time = api_->time;
            rt_->frame();
            vm.tick(static_cast<double>(dt));
            if (vm.state() == RunState::Paused) {   // a wait ran into a breakpoint
                rt_->wait_turn(this, dt);
                return true;
            }
        }
        begin_frame(dt);
        if (!run_frame()) rt_->interrupted = this;
        return true;
    }

    void begin_frame(float dt) override {
        phase_ = Phase::BeginPlay;
        key_i_ = 0;
        bus_i_ = 0;
        bus_n_ = -1;
        bus_step_ = 0;
        dt_ = dt;
    }
    /// Begin Play (first frame), key events, gameplay events, Tick: one event
    /// at a time, and a stop between any two of them is picked up again here.
    bool run_frame() override {
        rt_->shared.api = api_;
        VM& vm = rt_->vm;
        for (;;) {
            if (vm.state() == RunState::Paused) return false;
            switch (phase_) {
                case Phase::BeginPlay:
                    // On the first update, not in start(): the script system
                    // starts every script before it updates any, so an event
                    // sent from Begin Play finds the other script there.
                    phase_ = Phase::Keys;
                    if (!begun_) {
                        begun_ = true;
                        vm.fire(*inst_, "event.begin_play");
                    }
                    break;
                case Phase::Keys: {
                    if (key_i_ >= keys_.size()) {
                        phase_ = Phase::Bus;
                        break;
                    }
                    Key&       k = keys_[key_i_++];
                    const bool down = api_ && api_->key_down && api_->key_down(api_->ctx, k.code);
                    const bool was = k.down;
                    k.down = down;
                    if (down && !was && !k.on_press.empty()) vm.fire(*inst_, k.on_press);
                    else if (!down && was && !k.on_release.empty()) vm.fire(*inst_, k.on_release);
                    break;
                }
                case Phase::Bus: {
                    if (!wants_bus_ || !api_ || !api_->event_count || !api_->event_get) {
                        phase_ = Phase::Tick;
                        break;
                    }
                    if (bus_n_ < 0) bus_n_ = api_->event_count(api_->ctx);
                    if (bus_i_ >= bus_n_) {
                        phase_ = Phase::Tick;
                        break;
                    }
                    // One bus event can run up to three of this script's events
                    // (anyone's, about me, trigger): one per pass, so a stop in
                    // any of them is picked up at the next.
                    char     name[128] = {}, param[256] = {};
                    uint32_t instigator = 0, target = 0;
                    float    value = 0.0f;
                    const int step = bus_step_++;
                    if (step >= 2) {
                        bus_step_ = 0;
                        ++bus_i_;
                    }
                    const int at = step >= 2 ? bus_i_ - 1 : bus_i_;
                    if (!api_->event_get(api_->ctx, at, name, sizeof name, &instigator, &target, &value, param, sizeof param)) {
                        if (step < 2) {
                            bus_step_ = 0;
                            ++bus_i_;
                        }
                        break;
                    }
                    const Value args[4] = {Value::entity(instigator), Value::entity(target),
                                           Value::number(static_cast<double>(value)), Value::string(param)};
                    const uint32_t self = inst_->self;
                    if (step == 0) {
                        vm.fire(*inst_, std::string("event.gameplay:") + name, args, 4);
                    } else if (step == 1) {
                        if (self != 0 && (instigator == self || target == self)) vm.fire(*inst_, std::string("event.about_me:") + name, args, 4);
                    } else if (self != 0 && target == self) {
                        const std::string_view n(name);
                        if (n == "trigger.enter") vm.fire(*inst_, "event.trigger_enter", &args[0], 1);
                        else if (n == "trigger.exit") vm.fire(*inst_, "event.trigger_exit", &args[0], 1);
                    }
                    break;
                }
                case Phase::Tick: {
                    phase_ = Phase::Done;
                    const Value d = Value::number(static_cast<double>(dt_));
                    vm.fire(*inst_, "event.tick", &d, 1);
                    break;
                }
                case Phase::Done: return true;
            }
        }
    }
    bool invoke(const char* token, std::string& err) override {
        rt_->shared.api = api_;
        if (inst_ && token && rt_->vm.fire(*inst_, token)) return true;
        err = std::string("the logic graph has no event named '") + (token ? token : "") + "'";
        return false;
    }

private:
    /// Variables marked "show in Inspector" take the value authored on this
    /// entity's Script component, when there is one. Stored as text: the
    /// graph format's literal, or for an entity reference the object's name.
    void apply_inspector_values(uint32_t entity) {
        if (!api_ || !api_->get_param_string) return;
        static const char kUnset[] = "\x01";
        for (size_t i = 0; i < program_->vars.size(); ++i) {
            const VarInfo& v = program_->vars[i];
            if (!v.exposed || v.type.array) continue;
            char text[512] = {};
            api_->get_param_string(api_->ctx, entity, v.name.c_str(), text, sizeof text, kUnset);
            if (text[0] == kUnset[0]) continue;   // nothing authored: keep the default
            Value val;
            if (v.type.kind == Kind::Entity) {
                val = Value::entity(text[0] && api_->find_entity ? api_->find_entity(api_->ctx, text) : 0u);
            } else if (!parse_literal(v.type, text, val)) {
                continue;
            }
            inst_->vars[i] = std::move(val);
        }
    }

    std::shared_ptr<LogicRuntime>  rt_;
    std::shared_ptr<const Program> program_;
    std::string                    path_;    // path_key of the file
    const ScriptApi*               api_ = nullptr;
    std::unique_ptr<Instance>     inst_;
    struct Key {
        int         code = 0;
        bool        down = false;
        std::string on_press, on_release;
    };
    std::vector<Key> keys_;
    bool             wants_bus_ = false;
    bool             begun_ = false;
    // Where this frame's work is (run_frame).
    enum class Phase : uint8_t { BeginPlay, Keys, Bus, Tick, Done };
    Phase  phase_ = Phase::Done;
    size_t key_i_ = 0;
    int    bus_i_ = 0, bus_n_ = -1, bus_step_ = 0;
    float  dt_ = 0.0f;
};

class LogicHost final : public ScriptHost {
public:
    const char* language() const override { return "Logic Graph"; }

    LogicHost() { g_debug_runtime = rt_.get(); }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path, const ScriptApi* api,
                                           std::string& err) override {
        // One compiled program per file, shared by every object that runs it
        // and kept until the file changes.
        const std::string key = path_key(source_path);
        std::error_code   ec;
        const auto        stamp = std::filesystem::last_write_time(source_path, ec);
        const uintmax_t   size = ec ? 0 : std::filesystem::file_size(source_path, ec);
        LogicRuntime::Compiled& slot = rt_->programs[key];
        if (!ec && slot.program && slot.stamp == stamp && slot.size == size)
            return std::make_unique<LogicInstance>(rt_, slot.program, key, api);
        slot.program.reset();

        std::ifstream f(source_path, std::ios::binary);
        if (!f) {
            err = "cannot open '" + source_path + "'";
            return nullptr;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        Script script;
        if (!from_text(ss.str(), script, &err)) return nullptr;
        if (script.name.empty()) script.name = source_path;
        auto                        compiled = std::make_shared<Program>();
        Program&                    program = *compiled;
        std::vector<CompileMessage> msgs;
        if (!compile(rt_->reg, script, program, msgs)) {
            // The first error, with where it is: the Inspector shows one line.
            for (const CompileMessage& m : msgs) {
                if (!m.error) continue;
                err = m.text;
                if (m.graph >= 0 && m.graph < static_cast<int>(script.graphs.size()))
                    err = script.graphs[static_cast<size_t>(m.graph)].name + " node " + std::to_string(m.node) + ": " + err;
                break;
            }
            return nullptr;
        }
        if (!ec) {
            slot.stamp = stamp;
            slot.size = size;
            slot.program = compiled;
        }
        return std::make_unique<LogicInstance>(rt_, std::move(compiled), key, api);
    }

private:
    std::shared_ptr<LogicRuntime> rt_ = std::make_shared<LogicRuntime>();
};

}  // namespace

std::unique_ptr<ScriptHost> make_logic_host() { return std::make_unique<LogicHost>(); }

LogicDebugger* logic_debugger() { return g_debug_runtime; }

}  // namespace schizo::editor
