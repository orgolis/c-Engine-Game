#pragma once

// ============================================================================
// vscript/vm.h -- runs Programs.
// ============================================================================
//
// One VM runs any number of Instances (one per scripted entity). It is a plain
// loop over an explicit frame stack: no C++ recursion per script call, so
// execution can stop at a breakpoint in the middle of a chain and continue
// later from exactly there. That is what the debugger is built on.
//
// Single-threaded: a VM, its instances and their values belong to one thread.

#include "vscript/program.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace schizo::vs {

class VM;

struct Instance {
    const Program*       program = nullptr;
    uint32_t             self = 0;        // the entity this script is on
    std::vector<Value>   vars;
    std::vector<Value>   frame;           // the event unit's registers
    std::vector<int64_t> state;           // per-node state
    void*                user = nullptr;  // the host's own data

    Value* variable(std::string_view name) {
        const int i = program ? program->find_variable(name) : -1;
        return i < 0 ? nullptr : &vars[static_cast<size_t>(i)];
    }
};

/// What the VM asks of whoever embeds it.
struct Host {
    virtual ~Host() = default;
    /// "Print" nodes.
    virtual void print(const Instance&, std::string_view text) { (void)text; }
    /// The script instance on an entity, for nodes that reach another entity's
    /// script (Send Event, Get / Set its variables). nullptr when it has none.
    virtual Instance* find_instance(uint32_t entity) {
        (void)entity;
        return nullptr;
    }
    /// A script did something wrong at run time (bad index, runaway loop).
    /// `graph` and `node` say where; the script keeps running where it can.
    virtual void script_error(const Instance&, int graph, int node, std::string_view text) {
        (void)graph;
        (void)node;
        (void)text;
    }
};

/// Handed to every native function.
struct Context {
    VM*       vm = nullptr;
    Instance* instance = nullptr;
    Host*     host = nullptr;
    int       graph = -1;
    int       node = 0;
    void error(std::string_view text) const {
        if (host && instance) host->script_error(*instance, graph, node, text);
    }
};

enum class RunState : uint8_t { Idle, Paused };

class VM {
public:
    explicit VM(Host* host = nullptr) : host_(host) { stack_.resize(kStackSize); }

    std::unique_ptr<Instance> instantiate(const Program& program, uint32_t self = 0) const;

    /// Run an event by name. Returns false if the script has no such event.
    /// Arguments beyond the event's parameters are ignored; missing ones keep
    /// their previous value.
    bool fire(Instance& inst, std::string_view event, const Value* args = nullptr, int nargs = 0);
    bool has_event(const Instance& inst, std::string_view event) const {
        return inst.program && inst.program->find_event(event) >= 0;
    }
    /// Call a script function from the host. `results` receives nresults values.
    bool call(Instance& inst, std::string_view function, const Value* args, int nargs, std::vector<Value>* results = nullptr);

    /// Advance waits (Delay) by `dt` seconds and run what became due.
    void tick(double dt);
    /// Forget every pending wait of an instance. Call before destroying it.
    void forget(const Instance& inst);
    size_t pending_waits() const { return latent_.size(); }

    // ---- deterministic randomness for the standard library ----------------
    void     seed(uint64_t s) { rng_ = s ? s : 0x9E3779B97F4A7C15ull; }
    uint64_t next_random();
    double   random_unit() { return static_cast<double>(next_random() >> 11) * (1.0 / 9007199254740992.0); }

    // ---- limits -----------------------------------------------------------
    /// Instructions one fire()/tick() may execute before it is stopped as a
    /// runaway loop.
    int64_t instruction_budget = 2'000'000;
    int     max_call_depth = 200;

    // ---- debugging --------------------------------------------------------
    /// Break before a node runs. Identified by program, graph index and node id.
    void set_breakpoint(const Program* p, int graph, int node, bool on);
    void clear_breakpoints() { breakpoints_.clear(); }
    RunState state() const { return state_; }
    /// Where execution is paused: valid while state() == Paused.
    Instance* paused_instance() const { return paused_ ? frames_.back().inst : nullptr; }
    Where     paused_at() const;
    /// A register of the paused frame, to show pin values.
    const Value* paused_register(int reg) const;
    enum class Step : uint8_t { Continue, Into, Over, Out };
    /// Continue after a pause. Returns when the run finishes or pauses again.
    void resume(Step how = Step::Continue);
    /// Called when execution pauses.
    std::function<void(VM&)> on_pause;

    Host* host() const { return host_; }

private:
    static constexpr size_t kStackSize = 1 << 16;

    struct Frame {
        Instance* inst = nullptr;
        int       fn = 0;
        int       pc = 0;
        Value*    regs = nullptr;
        size_t    stack_base = 0;    // function frames: where their registers start on stack_
        size_t    cont_base = 0;
        Value*    ret_to = nullptr;  // caller registers receiving the results
        bool      top = false;       // pushed by the host: its end finishes the run
    };
    struct Latent {
        Instance* inst;
        int       pc;
        int       slot;
        double    remaining;
    };

    void push_event_frame(Instance& inst, int pc, bool top);
    static void store_event_args(Instance& inst, const EventInfo& ev, const Value* args, int nargs);
    bool push_function_frame(Instance& inst, int fn, const Value* args, int nargs, Value* ret_to, bool top);
    void pop_frame();
    void run();
    void fail(std::string_view text);
    uint64_t bp_key(const Program* p, int graph, int node) const;

    Host*               host_ = nullptr;
    std::vector<Value>  stack_;
    size_t              stack_top_ = 0;
    std::vector<Frame>  frames_;
    std::vector<int>    conts_;
    std::vector<Latent> latent_;
    uint64_t            rng_ = 0x9E3779B97F4A7C15ull;
    int64_t             budget_left_ = 0;

    RunState state_ = RunState::Idle;
    bool     paused_ = false;
    std::unordered_set<uint64_t> breakpoints_;
    Step     step_ = Step::Continue;
    size_t   step_depth_ = 0;
    Where    step_from_{};
    bool     skip_break_once_ = false;
    std::vector<Value> call_results_;
};

}  // namespace schizo::vs
