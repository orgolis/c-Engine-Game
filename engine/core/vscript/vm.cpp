#include "vscript/vm.h"

#include <utility>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace schizo::vs {

std::unique_ptr<Instance> VM::instantiate(const Program& program, uint32_t self) const {
    auto inst = std::make_unique<Instance>();
    inst->program = &program;
    inst->self = self;
    inst->vars.reserve(program.vars.size());
    for (const VarInfo& v : program.vars) inst->vars.push_back(clone(v.def));
    const FnInfo& unit = program.fns[0];
    inst->frame = unit.proto;
    for (int r : unit.boxed) inst->frame[static_cast<size_t>(r)] = clone(unit.proto[static_cast<size_t>(r)]);
    inst->state.assign(static_cast<size_t>(program.nstate), 0);
    return inst;
}

uint64_t VM::next_random() {
    // xorshift64*: small, fast, and the same sequence on every machine.
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return rng_ * 0x2545F4914F6CDD1Dull;
}

uint64_t VM::bp_key(const Program* p, int graph, int node) const {
    return (reinterpret_cast<uintptr_t>(p) * 0x9E3779B97F4A7C15ull) ^
           (static_cast<uint64_t>(static_cast<uint32_t>(graph)) << 32) ^ static_cast<uint32_t>(node);
}

void VM::set_breakpoint(const Program* p, int graph, int node, bool on) {
    if (on) breakpoints_.insert(bp_key(p, graph, node));
    else breakpoints_.erase(bp_key(p, graph, node));
}

uint64_t VM::trace_key(const Instance* inst, int graph, int node) {
    return (reinterpret_cast<uintptr_t>(inst) * 0x9E3779B97F4A7C15ull) ^
           (static_cast<uint64_t>(static_cast<uint32_t>(graph)) << 40) ^ static_cast<uint32_t>(node);
}

double VM::last_run(const Instance& inst, int graph, int node) const {
    const auto it = trace_last_.find(trace_key(&inst, graph, node));
    return it == trace_last_.end() ? -1.0 : it->second;
}

const Value* VM::frame_register(const Instance& inst, int fn, int reg) const {
    if (reg < 0 || !inst.program) return nullptr;
    if (fn == 0) return reg < static_cast<int>(inst.frame.size()) ? &inst.frame[static_cast<size_t>(reg)] : nullptr;
    if (!paused_ || frames_.empty()) return nullptr;
    const Frame& f = frames_.back();
    if (f.inst != &inst || f.fn != fn) return nullptr;
    const FnInfo& info = inst.program->fns[static_cast<size_t>(fn)];
    return reg < static_cast<int>(info.proto.size()) ? &f.regs[reg] : nullptr;
}

void VM::abort() {
    while (!frames_.empty()) pop_frame();
    conts_.clear();
    stack_top_ = 0;
    paused_ = false;
    state_ = RunState::Idle;
    step_ = Step::Continue;
}

Where VM::paused_at() const {
    if (!paused_ || frames_.empty()) return Where{};
    const Frame&   f = frames_.back();
    const Program& p = *f.inst->program;
    const Where    s = p.stmt[static_cast<size_t>(f.pc)];
    return s.node != 0 ? s : p.where[static_cast<size_t>(f.pc)];
}

const Value* VM::paused_register(int reg) const {
    if (!paused_ || frames_.empty() || reg < 0) return nullptr;
    const Frame&  f = frames_.back();
    const FnInfo& fn = f.inst->program->fns[static_cast<size_t>(f.fn)];
    return reg < static_cast<int>(fn.proto.size()) ? &f.regs[reg] : nullptr;
}

/// Copy arguments into an event's parameter registers, converted to the
/// parameter types. An argument that cannot be converted leaves the
/// parameter at its default, so a mismatched sender cannot put a value of
/// the wrong type on a pin.
void VM::store_event_args(Instance& inst, const EventInfo& ev, const Value* args, int nargs) {
    for (size_t i = 0; i < ev.param_regs.size(); ++i) {
        Value& dst = inst.frame[static_cast<size_t>(ev.param_regs[i])];
        const Type want = i < ev.param_types.size() ? ev.param_types[i] : dst.type;
        if (static_cast<int>(i) < nargs && args) convert(args[i], want, dst);
        else dst = Value::default_of(want);
    }
}

void VM::push_event_frame(Instance& inst, int pc, bool top) {
    Frame f;
    f.inst = &inst;
    f.fn = 0;
    f.pc = pc;
    f.regs = inst.frame.data();
    f.stack_base = stack_top_;
    f.cont_base = conts_.size();
    f.top = top;
    frames_.push_back(f);
}

bool VM::push_function_frame(Instance& inst, int fn, const Value* args, int nargs, Value* ret_to, bool top) {
    const FnInfo& info = inst.program->fns[static_cast<size_t>(fn)];
    const size_t  n = info.proto.size();
    if (static_cast<int>(frames_.size()) >= max_call_depth || stack_top_ + n > stack_.size()) {
        fail("the call chain is too deep (a function that calls itself without end?)");
        return false;
    }
    Frame f;
    f.inst = &inst;
    f.fn = fn;
    f.pc = info.entry;
    f.regs = stack_.data() + stack_top_;
    f.stack_base = stack_top_;
    f.cont_base = conts_.size();
    f.ret_to = ret_to;
    f.top = top;
    for (size_t i = 0; i < n; ++i) f.regs[i] = info.proto[i];
    for (int r : info.boxed) f.regs[r] = clone(info.proto[static_cast<size_t>(r)]);
    for (int i = 0; i < info.nparams && i < nargs; ++i) f.regs[i] = args[i];
    stack_top_ += n;
    frames_.push_back(f);
    return true;
}

void VM::pop_frame() {
    const Frame f = frames_.back();
    frames_.pop_back();
    if (f.fn != 0) {
        const FnInfo& info = f.inst->program->fns[static_cast<size_t>(f.fn)];
        if (f.ret_to) {
            const int n = f.ret_limit >= 0 ? std::min(info.nresults, f.ret_limit) : info.nresults;
            for (int i = 0; i < n; ++i) {
                const Value& src = f.regs[info.result_base + i];
                if (!f.ret_convert) {
                    f.ret_to[i] = src;
                } else {   // the caller's register already holds a default of the type it declared
                    Value conv;
                    if (convert(src, f.ret_to[i].type, conv)) f.ret_to[i] = std::move(conv);
                }
            }
        }
        // Drop what the frame held so arrays and strings are freed now.
        for (size_t i = 0; i < info.proto.size(); ++i) f.regs[i] = Value();
        stack_top_ = f.stack_base;
    }
    conts_.resize(f.cont_base);
}

void VM::fail(std::string_view text) {
    if (host_ && !frames_.empty()) {
        const Frame& f = frames_.back();
        const auto&  wh = f.inst->program->where;
        const Where  w = f.pc >= 0 && f.pc < static_cast<int>(wh.size()) ? wh[static_cast<size_t>(f.pc)] : Where{};
        host_->script_error(*f.inst, w.graph, w.node, text);
    }
    while (!frames_.empty()) pop_frame();
    conts_.clear();
    stack_top_ = 0;
    paused_ = false;
    state_ = RunState::Idle;
}

bool VM::fire(Instance& inst, std::string_view event, const Value* args, int nargs) {
    if (!inst.program || paused_) return false;
    const int e = inst.program->find_event(event);
    if (e < 0) return false;
    const EventInfo& ev = inst.program->events[static_cast<size_t>(e)];
    store_event_args(inst, ev, args, nargs);
    const bool outermost = frames_.empty();
    if (outermost) budget_left_ = instruction_budget;
    push_event_frame(inst, ev.entry, true);
    run();
    return true;
}

bool VM::call(Instance& inst, std::string_view function, const Value* args, int nargs, std::vector<Value>* results) {
    if (!inst.program || paused_) return false;
    const int fn = inst.program->find_function(function);
    if (fn < 0) return false;
    const FnInfo& info = inst.program->fns[static_cast<size_t>(fn)];
    std::vector<Value> out(static_cast<size_t>(info.nresults));
    const bool outermost = frames_.empty();
    if (outermost) budget_left_ = instruction_budget;
    if (!push_function_frame(inst, fn, args, nargs, out.data(), true)) return false;
    run();
    if (results) *results = std::move(out);
    return true;
}

namespace {
/// The shape of a timeline's 0..1 run.
double ease(int curve, double t) {
    t = std::min(std::max(t, 0.0), 1.0);
    switch (curve) {
        case 1: return t * t;
        case 2: return 1.0 - (1.0 - t) * (1.0 - t);
        case 3: return t * t * (3.0 - 2.0 * t);
        default: return t;
    }
}
}  // namespace

/// Move every running timeline on by `dt`, running its Update output, and its
/// Finished output when it arrives.
void VM::run_timelines(double dt) {
    // By value: an Update may start or stop timelines, this one included.
    const std::vector<RunningTimeline> now = timelines_;
    for (const RunningTimeline& r : now) {
        if (paused_) return;   // a breakpoint: the rest move on next frame
        const auto live = std::find_if(timelines_.begin(), timelines_.end(), [&](const RunningTimeline& x) {
            return x.inst == r.inst && x.index == r.index;
        });
        if (live == timelines_.end()) continue;   // stopped by an earlier one's Update
        Instance&           inst = *r.inst;
        const TimelineInfo& ti = inst.program->timelines[static_cast<size_t>(r.index)];
        int64_t&            mode = inst.state[static_cast<size_t>(ti.slot)];
        Value&              time = inst.frame[static_cast<size_t>(ti.out_base + 1)];
        const double        dur = live->duration;
        double              t = time.u.f + (mode == 2 ? -dt : dt);
        bool                arrived = false;
        if (mode == 1 && t >= dur) {
            if (live->loop) t = std::fmod(t, dur);
            else { t = dur; arrived = true; }
        } else if (mode == 2 && t <= 0.0) {
            if (live->loop) t = dur + std::fmod(t, dur);
            else { t = 0.0; arrived = true; }
        }
        time = Value::number(t);
        inst.frame[static_cast<size_t>(ti.out_base)] = Value::number(ease(ti.curve, t / dur));
        if (arrived) {
            mode = 0;
            timelines_.erase(live);
        }
        budget_left_ = instruction_budget;
        push_event_frame(inst, ti.update_pc, true);
        run();
        if (arrived && !paused_) {
            budget_left_ = instruction_budget;
            push_event_frame(inst, ti.finish_pc, true);
            run();
        }
    }
}

void VM::tick(double dt) {
    if (paused_) return;
    std::vector<Latent> due;
    for (Latent& l : latent_) l.remaining -= dt;
    for (size_t i = 0; i < latent_.size();) {
        if (latent_[i].remaining <= 0.0) {
            due.push_back(latent_[i]);
            latent_.erase(latent_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const Latent& l : due) {
        if (paused_) {   // a breakpoint hit while running an earlier one: keep the rest for later
            latent_.push_back(Latent{l.inst, l.pc, l.slot, 0.0});
            continue;
        }
        l.inst->state[static_cast<size_t>(l.slot)] = 0;
        budget_left_ = instruction_budget;
        push_event_frame(*l.inst, l.pc, true);
        run();
    }
    if (dt > 0.0) run_timelines(dt);
}

// (the trace keeps a destroyed instance's entries until it is cleared; they
// are only ever read for an instance that is alive)
void VM::forget(const Instance& inst) {
    latent_.erase(std::remove_if(latent_.begin(), latent_.end(), [&](const Latent& l) { return l.inst == &inst; }),
                  latent_.end());
    timelines_.erase(std::remove_if(timelines_.begin(), timelines_.end(), [&](const RunningTimeline& r) { return r.inst == &inst; }),
                     timelines_.end());
}

void VM::resume(Step how) {
    if (!paused_) return;
    paused_ = false;
    state_ = RunState::Idle;
    step_ = how;
    step_depth_ = frames_.size();
    step_from_ = frames_.empty() ? Where{} : frames_.back().inst->program->where[static_cast<size_t>(frames_.back().pc)];
    skip_break_once_ = true;
    budget_left_ = instruction_budget;
    // The C++ callers that started these frames returned when the VM paused,
    // so finish every frame that is still there, innermost first.
    while (!frames_.empty() && !paused_) run();
    // Stepping off the end of what was running is "continue": the next event
    // the engine fires must not stop at its first node.
    if (!paused_) step_ = Step::Continue;
}

void VM::run() {
    // Run until the frame that was on top when we were called has finished.
    // (resume() continues whatever is there, down to an empty stack.)
    size_t floor = 0;
    for (size_t i = frames_.size(); i-- > 0;)
        if (frames_[i].top) {
            floor = i;
            break;
        }
    Where           traced{};
    const Instance* traced_inst = nullptr;

    while (frames_.size() > floor) {
        Frame*         f = &frames_.back();
        const Program& p = *f->inst->program;
        Value*         R = f->regs;
        const Instr&   in = p.code[static_cast<size_t>(f->pc)];

        // ---- debugger ---------------------------------------------------
        // Stops happen only where a flow node's run starts (Program::stmt).
        if (!breakpoints_.empty() || step_ != Step::Continue) {
            const Where s = p.stmt[static_cast<size_t>(f->pc)];
            if (s.node != 0 && !skip_break_once_) {
                bool stop = breakpoints_.count(bp_key(&p, s.graph, s.node)) != 0;
                if (step_ == Step::Into) stop = true;
                if (step_ == Step::Over && frames_.size() <= step_depth_) stop = true;
                if (step_ == Step::Out && frames_.size() < step_depth_) stop = true;
                if (stop) {
                    paused_ = true;
                    state_ = RunState::Paused;
                    step_ = Step::Continue;
                    step_from_ = s;
                    if (on_pause) on_pause(*this);
                    return;
                }
            }
            skip_break_once_ = false;
        }
        if (trace) {
            const Where w = p.where[static_cast<size_t>(f->pc)];
            if (w.node != 0 && (w.node != traced.node || w.graph != traced.graph || f->inst != traced_inst)) {
                if (trace_last_.size() > 200000) trace_last_.clear();
                trace_last_[trace_key(f->inst, w.graph, w.node)] = trace_time;
                traced = w;
                traced_inst = f->inst;
            }
        }

        if (--budget_left_ < 0) {
            fail("stopped after " + std::to_string(instruction_budget) + " steps: this looks like a loop that never ends");
            return;
        }
        ++f->pc;

        auto end_chain = [&]() {
            if (conts_.size() > f->cont_base) {
                f->pc = conts_.back();
                conts_.pop_back();
            } else {
                pop_frame();
            }
        };

        switch (in.op) {
            case OP_NOP: break;
            case OP_LOADK: {
                const Value& k = p.consts[static_cast<size_t>(in.b)];
                R[in.a] = (k.type.array || k.type.kind == Kind::Struct) ? clone(k) : k;
                break;
            }
            case OP_MOVE: R[in.a] = R[in.b]; break;
            case OP_COPY: R[in.a] = clone(R[in.b]); break;
            case OP_GETV: R[in.a] = f->inst->vars[static_cast<size_t>(in.b)]; break;
            case OP_SETV: f->inst->vars[static_cast<size_t>(in.a)] = clone(R[in.b]); break;
            case OP_SELF: R[in.a] = Value::entity(f->inst->self); break;
            case OP_CONV: {
                Value out;
                convert(R[in.b], p.types[static_cast<size_t>(in.c)], out);
                R[in.a] = std::move(out);
                break;
            }
            case OP_JMP: f->pc = in.a; break;
            case OP_JMPT: if (R[in.a].u.i != 0) f->pc = in.b; break;
            case OP_JMPF: if (R[in.a].u.i == 0) f->pc = in.b; break;
            case OP_PUSHC: conts_.push_back(in.a); break;
            case OP_END: end_chain(); break;
            case OP_RET: pop_frame(); break;
            case OP_CALLN: {
                const NodeDef*  def = p.natives[static_cast<size_t>(in.a)];
                const NativeFn& fn = in.d ? def->overloads[static_cast<size_t>(in.d - 1)].fn : def->fn;
                const Where     w = p.where[static_cast<size_t>(f->pc - 1)];
                Context ctx{this, f->inst, host_, w.graph, w.node};
                fn(ctx, R + in.b, R + in.c);
                if (paused_ || frames_.size() <= floor) return;   // a native ran script code that paused or failed
                break;
            }
            case OP_CALLF:
                if (!push_function_frame(*f->inst, in.a, R + in.b, 1 << 20, R + in.c, false)) return;
                break;
            case OP_CALLE: {
                const EventInfo& ev = p.events[static_cast<size_t>(in.a)];
                store_event_args(*f->inst, ev, R + in.b, static_cast<int>(ev.param_regs.size()));
                if (static_cast<int>(frames_.size()) >= max_call_depth) {
                    fail("the call chain is too deep (an event that calls itself without end?)");
                    return;
                }
                push_event_frame(*f->inst, ev.entry, false);
                break;
            }
            case OP_STSET: f->inst->state[static_cast<size_t>(in.a)] = in.b; break;
            case OP_STLOAD: R[in.a] = Value::integer(f->inst->state[static_cast<size_t>(in.b)]); break;
            case OP_STSTORE: f->inst->state[static_cast<size_t>(in.a)] = R[in.b].u.i; break;
            case OP_JSTEQ: if (f->inst->state[static_cast<size_t>(in.a)] == in.b) f->pc = in.c; break;
            case OP_LATENT: {
                int64_t&     st = f->inst->state[static_cast<size_t>(in.c)];
                const double secs = std::max(0.0, R[in.a].u.f);
                if (st == 0) {
                    st = 1;
                    latent_.push_back(Latent{f->inst, in.b, in.c, secs});
                } else if (in.d) {   // retriggerable: start the wait over
                    for (Latent& l : latent_)
                        if (l.inst == f->inst && l.slot == in.c) l.remaining = secs;
                }
                end_chain();
                break;
            }
            case OP_BIND: {
                const uint32_t target = R[in.b].u.entity;
                Instance*      owner = target == 0 || target == f->inst->self ? f->inst
                                       : (host_ ? host_->find_instance(target) : nullptr);
                if (!owner) break;   // no script there: nothing to bind to
                const std::string& disp = p.consts[static_cast<size_t>(in.a)].str();
                const std::string& ev = R[in.c].str();
                const uint32_t     me = f->inst->self;
                const auto it = std::find_if(owner->bindings.begin(), owner->bindings.end(), [&](const Instance::Binding& x) {
                    return x.listener == me && x.dispatcher == disp && x.event == ev;
                });
                if (in.d) {
                    if (it == owner->bindings.end()) owner->bindings.push_back(Instance::Binding{disp, me, ev});
                } else if (it != owner->bindings.end()) {
                    owner->bindings.erase(it);
                }
                break;
            }
            case OP_DISPATCH: {
                Instance&          owner = *f->inst;
                const std::string& disp = p.consts[static_cast<size_t>(in.a)].str();
                std::vector<std::pair<Instance*, const EventInfo*>> listeners;
                for (const Instance::Binding& x : owner.bindings) {
                    if (x.dispatcher != disp) continue;
                    Instance* l = x.listener == owner.self ? &owner : (host_ ? host_->find_instance(x.listener) : nullptr);
                    if (!l || !l->program) continue;   // the listener is gone
                    const int e = l->program->find_event(x.event);
                    if (e >= 0) listeners.emplace_back(l, &l->program->events[static_cast<size_t>(e)]);
                }
                if (static_cast<int>(frames_.size() + listeners.size()) >= max_call_depth) {
                    fail("the call chain is too deep (a dispatcher whose listener calls it again without end?)");
                    return;
                }
                for (const auto& [l, ev] : listeners) store_event_args(*l, *ev, R + in.b, in.d);
                // Pushed last to first, so they run in the order they bound.
                for (size_t i = listeners.size(); i-- > 0;) push_event_frame(*listeners[i].first, listeners[i].second->entry, false);
                break;
            }
            case OP_CALLFO: {
                const int      nargs = in.d & 0xFF, nres = in.d >> 8;
                const uint32_t target = R[in.b].u.entity;
                Instance*      other = target != 0 && target == f->inst->self ? f->inst : (host_ ? host_->find_instance(target) : nullptr);
                if (!other || !other->program) break;   // the results keep their defaults
                const int fn = other->program->find_function(p.consts[static_cast<size_t>(in.a)].str());
                if (fn <= 0) break;
                const FnInfo&      info = other->program->fns[static_cast<size_t>(fn)];
                std::vector<Value> args(static_cast<size_t>(info.nparams));
                for (int i = 0; i < info.nparams; ++i) {
                    const Value& proto = info.proto[static_cast<size_t>(i)];
                    if (i < nargs) convert(R[in.b + 1 + i], proto.type, args[static_cast<size_t>(i)]);
                    else args[static_cast<size_t>(i)] = proto;
                }
                if (!push_function_frame(*other, fn, args.data(), info.nparams, R + in.c, false)) return;
                frames_.back().ret_limit = nres;
                frames_.back().ret_convert = true;
                break;
            }
            case OP_TIMELINE: {
                Instance&           inst = *f->inst;
                const TimelineInfo& ti = p.timelines[static_cast<size_t>(in.c)];
                int64_t&            mode = inst.state[static_cast<size_t>(ti.slot)];
                Value&              alpha = inst.frame[static_cast<size_t>(ti.out_base)];
                Value&              time = inst.frame[static_cast<size_t>(ti.out_base + 1)];
                const double        dur = std::max(1e-6, R[in.a].u.f);
                switch (in.d) {
                    case kTimelinePlay: mode = 1; break;
                    case kTimelinePlayFromStart: time = Value::number(0.0); mode = 1; break;
                    case kTimelineStop: mode = 0; break;
                    case kTimelineReverse: mode = 2; break;
                    default: time = Value::number(dur); mode = 2; break;
                }
                time = Value::number(std::min(std::max(time.u.f, 0.0), dur));
                alpha = Value::number(ease(ti.curve, time.u.f / dur));
                auto running = std::find_if(timelines_.begin(), timelines_.end(), [&](const RunningTimeline& r) {
                    return r.inst == &inst && r.index == in.c;
                });
                if (mode == 0) {
                    if (running != timelines_.end()) timelines_.erase(running);
                } else if (running == timelines_.end()) {
                    timelines_.push_back(RunningTimeline{&inst, in.c, dur, R[in.b].u.i != 0});
                } else {
                    running->duration = dur;
                    running->loop = R[in.b].u.i != 0;
                }
                break;
            }
            case OP_ALEN: R[in.a] = Value::integer(static_cast<int64_t>(R[in.b].items().size())); break;
            case OP_AGET: {
                const auto&   items = R[in.b].items();
                const int64_t i = R[in.c].u.i;
                if (i >= 0 && i < static_cast<int64_t>(items.size())) {
                    R[in.a] = items[static_cast<size_t>(i)];
                } else {
                    R[in.a] = Value::default_of(R[in.b].type.element());
                }
                break;
            }
            case OP_MKARR: {
                std::vector<Value> items(R + in.b, R + in.b + in.d);
                R[in.a] = Value::array(p.types[static_cast<size_t>(in.c)], std::move(items));
                break;
            }
            case OP_SELECT: {
                const int64_t i = std::clamp<int64_t>(R[in.c].u.i, 0, static_cast<int64_t>(in.d) - 1);
                R[in.a] = R[in.b + static_cast<int>(i)];
                break;
            }
            case OP_EQ: R[in.a] = Value::boolean(equals(R[in.b], R[in.c])); break;
            case OP_NE: R[in.a] = Value::boolean(!equals(R[in.b], R[in.c])); break;

            case OP_ADDI: R[in.a] = Value::integer(R[in.b].u.i + R[in.c].u.i); break;
            case OP_SUBI: R[in.a] = Value::integer(R[in.b].u.i - R[in.c].u.i); break;
            case OP_MULI: R[in.a] = Value::integer(R[in.b].u.i * R[in.c].u.i); break;
            case OP_DIVI:
            case OP_MODI: {
                const int64_t x = R[in.b].u.i, y = R[in.c].u.i;
                if (y == 0) {
                    const Where w = p.where[static_cast<size_t>(f->pc - 1)];
                    if (host_) host_->script_error(*f->inst, w.graph, w.node, "division by zero; the result is 0");
                    R[in.a] = Value::integer(0);
                } else if (y == -1) {   // INT64_MIN / -1 overflows
                    R[in.a] = Value::integer(in.op == OP_DIVI ? static_cast<int64_t>(0 - static_cast<uint64_t>(x)) : 0);
                } else {
                    R[in.a] = Value::integer(in.op == OP_DIVI ? x / y : x % y);
                }
                break;
            }
            case OP_NEGI: R[in.a] = Value::integer(-R[in.b].u.i); break;
            case OP_LTI: R[in.a] = Value::boolean(R[in.b].u.i < R[in.c].u.i); break;
            case OP_LEI: R[in.a] = Value::boolean(R[in.b].u.i <= R[in.c].u.i); break;
            case OP_GTI: R[in.a] = Value::boolean(R[in.b].u.i > R[in.c].u.i); break;
            case OP_GEI: R[in.a] = Value::boolean(R[in.b].u.i >= R[in.c].u.i); break;
            case OP_EQI: R[in.a] = Value::boolean(R[in.b].u.i == R[in.c].u.i); break;
            case OP_NEI: R[in.a] = Value::boolean(R[in.b].u.i != R[in.c].u.i); break;

            case OP_ADDF: R[in.a] = Value::number(R[in.b].u.f + R[in.c].u.f); break;
            case OP_SUBF: R[in.a] = Value::number(R[in.b].u.f - R[in.c].u.f); break;
            case OP_MULF: R[in.a] = Value::number(R[in.b].u.f * R[in.c].u.f); break;
            case OP_DIVF: {
                const double y = R[in.c].u.f;
                if (y == 0.0) {
                    const Where w = p.where[static_cast<size_t>(f->pc - 1)];
                    if (host_) host_->script_error(*f->inst, w.graph, w.node, "division by zero; the result is 0");
                    R[in.a] = Value::number(0.0);
                } else {
                    R[in.a] = Value::number(R[in.b].u.f / y);
                }
                break;
            }
            case OP_NEGF: R[in.a] = Value::number(-R[in.b].u.f); break;
            case OP_LTF: R[in.a] = Value::boolean(R[in.b].u.f < R[in.c].u.f); break;
            case OP_LEF: R[in.a] = Value::boolean(R[in.b].u.f <= R[in.c].u.f); break;
            case OP_GTF: R[in.a] = Value::boolean(R[in.b].u.f > R[in.c].u.f); break;
            case OP_GEF: R[in.a] = Value::boolean(R[in.b].u.f >= R[in.c].u.f); break;
            case OP_EQF: R[in.a] = Value::boolean(R[in.b].u.f == R[in.c].u.f); break;
            case OP_NEF: R[in.a] = Value::boolean(R[in.b].u.f != R[in.c].u.f); break;

            case OP_NOT: R[in.a] = Value::boolean(R[in.b].u.i == 0); break;
            case OP_AND: R[in.a] = Value::boolean(R[in.b].u.i != 0 && R[in.c].u.i != 0); break;
            case OP_OR: R[in.a] = Value::boolean(R[in.b].u.i != 0 || R[in.c].u.i != 0); break;
            case OP_XOR: R[in.a] = Value::boolean((R[in.b].u.i != 0) != (R[in.c].u.i != 0)); break;
            case OP_SENDE: {
                Instance* other = host_ ? host_->find_instance(R[in.c].u.entity) : nullptr;
                if (!other || !other->program) break;   // no script there: nothing to run
                const int e = other->program->find_event(p.consts[static_cast<size_t>(in.a)].str());
                if (e < 0) break;
                const EventInfo& ev = other->program->events[static_cast<size_t>(e)];
                store_event_args(*other, ev, R + in.b, in.d);
                if (static_cast<int>(frames_.size()) >= max_call_depth) {
                    fail("the call chain is too deep (scripts sending events to each other without end?)");
                    return;
                }
                push_event_frame(*other, ev.entry, false);
                break;
            }
            case OP_GETVO: {
                const Type want = p.types[static_cast<size_t>(in.d)];
                Instance*  other = host_ ? host_->find_instance(R[in.b].u.entity) : nullptr;
                Value*     v = other ? other->variable(p.consts[static_cast<size_t>(in.c)].str()) : nullptr;
                Value      out;
                if (v) convert(*v, want, out);
                else out = Value::default_of(want);
                R[in.a] = std::move(out);
                break;
            }
            case OP_SETVO: {
                Instance* other = host_ ? host_->find_instance(R[in.a].u.entity) : nullptr;
                Value*    v = other ? other->variable(p.consts[static_cast<size_t>(in.c)].str()) : nullptr;
                if (!v) break;
                Value conv;
                if (convert(R[in.b], v->type, conv)) *v = clone(conv);
                break;
            }
            default: fail("internal: unknown instruction"); return;
        }
    }
    if (frames_.empty()) {
        step_ = Step::Continue;
        step_from_ = Where{};
    }
}

}  // namespace schizo::vs
