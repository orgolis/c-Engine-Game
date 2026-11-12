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

#include "vscript/io.h"
#include "vscript/program.h"
#include "vscript/vm.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace schizo::editor {

namespace {

using namespace schizo::vs;

/// What every node needs at run time: the engine table of the current frame.
struct Shared {
    const ScriptApi* api = nullptr;
};

glm::vec3 v3(const float f[3]) { return glm::vec3(f[0], f[1], f[2]); }

void register_engine_nodes(Registry& r, Shared* s) {
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

/// One registry and one VM for every logic script: programs are small, the
/// VM's stack is not.
struct LogicRuntime : Host {
    Registry reg;
    Shared   shared;
    VM       vm{this};
    double   last_tick_time = -1.0;
    std::string last_error;

    LogicRuntime() {
        register_standard_library(reg);
        register_engine_nodes(reg, &shared);
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

class LogicInstance final : public ScriptInstance {
public:
    LogicInstance(std::shared_ptr<LogicRuntime> rt, Program program, const ScriptApi* api)
        : rt_(std::move(rt)), program_(std::move(program)), api_(api) {}
    ~LogicInstance() override {
        if (inst_) {
            rt_->shared.api = api_;
            rt_->vm.fire(*inst_, "event.end_play");
            rt_->vm.forget(*inst_);
        }
    }

    bool start(uint32_t entity, std::string&) override {
        rt_->shared.api = api_;
        inst_ = rt_->vm.instantiate(program_, entity);
        rt_->vm.fire(*inst_, "event.begin_play");
        return true;
    }
    bool update(uint32_t, float dt, std::string&) override {
        rt_->shared.api = api_;
        // Waits belong to the shared VM: advance them once per frame, not once
        // per script.
        if (api_ && api_->time != rt_->last_tick_time) {
            rt_->last_tick_time = api_->time;
            rt_->vm.tick(static_cast<double>(dt));
        }
        const Value d = Value::number(static_cast<double>(dt));
        rt_->vm.fire(*inst_, "event.tick", &d, 1);
        return true;
    }
    bool invoke(const char* token, std::string& err) override {
        rt_->shared.api = api_;
        if (inst_ && token && rt_->vm.fire(*inst_, token)) return true;
        err = std::string("the logic graph has no event named '") + (token ? token : "") + "'";
        return false;
    }

private:
    std::shared_ptr<LogicRuntime> rt_;
    Program                       program_;
    const ScriptApi*              api_ = nullptr;
    std::unique_ptr<Instance>     inst_;
};

class LogicHost final : public ScriptHost {
public:
    const char* language() const override { return "Logic Graph"; }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path, const ScriptApi* api,
                                           std::string& err) override {
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
        Program                     program;
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
        return std::make_unique<LogicInstance>(rt_, std::move(program), api);
    }

private:
    std::shared_ptr<LogicRuntime> rt_ = std::make_shared<LogicRuntime>();
};

}  // namespace

std::unique_ptr<ScriptHost> make_logic_host() { return std::make_unique<LogicHost>(); }

}  // namespace schizo::editor
