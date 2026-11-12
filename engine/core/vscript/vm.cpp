#include "vscript/vm.h"

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
        if (f.ret_to)
            for (int i = 0; i < info.nresults; ++i) f.ret_to[i] = f.regs[info.result_base + i];
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
}

// (the trace keeps a destroyed instance's entries until it is cleared; they
// are only ever read for an instance that is alive)
void VM::forget(const Instance& inst) {
    latent_.erase(std::remove_if(latent_.begin(), latent_.end(), [&](const Latent& l) { return l.inst == &inst; }),
                  latent_.end());
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
