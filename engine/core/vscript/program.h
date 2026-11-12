#pragma once

// ============================================================================
// vscript/program.h -- what a Script compiles to.
// ============================================================================
//
// A Program is flat bytecode for a register machine. Function 0 is the "event
// unit": every event graph of the script, compiled together, sharing one set of
// registers that live as long as the instance does. That is what lets a Delay
// in the middle of an event resume later with every pin value still there.
// Functions 1..n are the script's functions; each call gets a fresh frame.
//
// Exec flow is compiled to jumps. A chain of nodes ends in OP_END, which pops a
// continuation (pushed by Sequence and by loops, so they regain control when
// the chain they started finishes) or, when there is none, leaves the frame.

#include "vscript/registry.h"
#include "vscript/value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace schizo::vs {

enum Op : uint16_t {
    OP_NOP = 0,
    OP_LOADK,    // R[a] = K[b]                       (arrays are copied so the constant stays intact)
    OP_MOVE,     // R[a] = R[b]
    OP_COPY,     // R[a] = deep copy of R[b]
    OP_GETV,     // R[a] = variable b
    OP_SETV,     // variable a = deep copy of R[b]
    OP_SELF,     // R[a] = the instance's entity
    OP_CONV,     // R[a] = R[b] converted to type T[c]
    OP_JMP,      // pc = a
    OP_JMPT,     // if R[a] is true: pc = b
    OP_JMPF,     // if R[a] is false: pc = b
    OP_PUSHC,    // push continuation a
    OP_END,      // end of an exec chain: pop a continuation, else leave the frame
    OP_CALLN,    // native N[a], inputs at R[b...], outputs at R[c...]
    OP_CALLF,    // function a, arguments at R[b...], results to R[c...]
    OP_CALLE,    // event a of this instance, arguments at R[b...]
    OP_RET,      // leave the function now (results are in its result registers)
    // per-instance node state (Do Once, Gate, Flip Flop, Delay ...)
    OP_STSET,    // state[a] = b
    OP_STLOAD,   // R[a] = Int(state[b])
    OP_STSTORE,  // state[a] = R[b] as an integer
    OP_JSTEQ,    // if state[a] == b: pc = c
    OP_LATENT,   // wait R[a] seconds, then run from pc b; state[c] marks it pending; d = 1 restarts a pending wait
    // arrays
    OP_ALEN,     // R[a] = Int(length of R[b])
    OP_AGET,     // R[a] = R[b][R[c]], or the element default when out of range
    OP_MKARR,    // R[a] = array of element type T[c] from R[b .. b+d)
    OP_SELECT,   // R[a] = R[b + clamp(R[c], 0, d-1)]
    // any type
    OP_EQ,       // R[a] = Bool(R[b] equals R[c])
    OP_NE,
    // Int
    OP_ADDI, OP_SUBI, OP_MULI, OP_DIVI, OP_MODI, OP_NEGI,
    OP_LTI, OP_LEI, OP_GTI, OP_GEI, OP_EQI, OP_NEI,
    // Float
    OP_ADDF, OP_SUBF, OP_MULF, OP_DIVF, OP_NEGF,
    OP_LTF, OP_LEF, OP_GTF, OP_GEF, OP_EQF, OP_NEF,
    // Bool
    OP_NOT, OP_AND, OP_OR, OP_XOR,
    // other instances (found through Host::find_instance)
    OP_SENDE,    // run event K[a] on the script of entity R[c], arguments R[b .. b+d)
    OP_GETVO,    // R[a] = variable K[c] of entity R[b]'s script, as type T[d]
    OP_SETVO,    // variable K[c] of entity R[a]'s script = R[b]
    OP_COUNT_,
};

struct Instr {
    uint16_t op = OP_NOP;
    uint16_t d = 0;
    int32_t  a = 0, b = 0, c = 0;
};
static_assert(sizeof(Instr) == 16, "an instruction is 16 bytes");

struct FnInfo {
    std::string name;
    int  graph = -1;          // index into the script's graphs; -1 for the event unit
    int  entry = 0;           // first instruction
    int  nparams = 0;         // parameters are registers [0, nparams)
    int  nresults = 0;        // results are registers [result_base, result_base + nresults)
    int  result_base = 0;
    bool pure = false;
    std::vector<Value> proto; // initial value of every register
    std::vector<int>   boxed; // registers whose initial value must be deep-copied per frame (arrays, structs)
};

struct EventInfo {
    std::string      name;
    int              entry = 0;       // instruction to start at, in the event unit
    std::vector<int> param_regs;      // event unit registers the arguments are copied into
    std::vector<Type> param_types;    // and the type each one must have
    bool             custom = false;  // a Custom Event node, as opposed to a host event
    int              graph = 0;
    int              node = 0;
};

struct VarInfo {
    std::string name;
    Type        type;
    Value       def;
    bool        exposed = false;
};

struct Where {
    int32_t graph = -1;
    int32_t node = 0;
};

struct Program {
    std::string            name;
    std::vector<Instr>     code;
    std::vector<Where>     where;      // per instruction: the node it was compiled from
    std::vector<Value>     consts;
    std::vector<Type>      types;
    std::vector<const NodeDef*> natives;
    std::vector<FnInfo>    fns;        // [0] is the event unit
    std::vector<EventInfo> events;
    std::vector<VarInfo>   vars;
    std::vector<std::string> graph_names;
    int                    nstate = 0; // per-instance node state slots

    int find_event(std::string_view n) const {
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].name == n) return static_cast<int>(i);
        return -1;
    }
    int find_function(std::string_view n) const {
        for (size_t i = 1; i < fns.size(); ++i)
            if (fns[i].name == n) return static_cast<int>(i);
        return -1;
    }
    int find_variable(std::string_view n) const {
        for (size_t i = 0; i < vars.size(); ++i)
            if (vars[i].name == n) return static_cast<int>(i);
        return -1;
    }
};

struct CompileMessage {
    bool        error = true;   // false: a warning
    int         graph = -1;     // index into the script's graphs
    int         node = 0;       // node id, 0 when the message is about the graph or script
    std::string text;
};

/// Compile `script` against `reg`. Returns true when there are no errors;
/// `messages` gets errors and warnings either way. On failure `out` is not
/// runnable.
bool compile(const Registry& reg, const Script& script, Program& out, std::vector<CompileMessage>& messages);

/// One line per instruction, for tests and for reading what the compiler did.
std::string disassemble(const Program& p);

}  // namespace schizo::vs
