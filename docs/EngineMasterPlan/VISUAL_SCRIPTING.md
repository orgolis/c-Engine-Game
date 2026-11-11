# Visual scripting: how the Logic Graph core works

**Status:** milestone A1 of [ROAD_TO_TEST_GAME.md](ROAD_TO_TEST_GAME.md), shipped in
v0.8.14. The core exists and is checked headless. It is not in the editor yet:
the Logic Graph panel still runs the old `gameplay_logic.h` runtime until A2
and A3 replace it.

Code: `engine/core/vscript/`. Check: `tools/vscript_check` (58 checks).

## The pieces

| File | What it is |
|------|------------|
| `value.h` | `Type` and `Value`. A value is 32 bytes: type, 16 inline bytes, one pointer to a reference-counted box for strings, arrays and transforms |
| `graph.h` | The asset: a `Script` has variables and graphs; a graph has nodes and links. Links name pins by name |
| `registry.h` | Every node kind (`NodeDef`). `Registry::fn()` and `pure()` turn a C++ lambda into a node, pins derived from its signature. `shape_of()` gives the pins of one node in one graph |
| `compiler.cpp` | `compile()`: Script to `Program` (bytecode for a register machine), with errors that name the graph and node |
| `vm.h` | `VM`: runs programs for any number of `Instance`s. An explicit frame stack, so it can pause and resume |
| `stdlib.cpp` | The nodes that need no engine: flow, variables, functions, events, maths, vectors, strings, arrays |
| `io.h` | Text format `VSCRIPT 2`: one record per line, tab-separated |

## Rules an author can rely on

These match Unreal's Blueprints on purpose, so that what people already know
transfers.

- **Flow.** A flow output has one wire. A chain runs until a node with nothing
  wired after it. Sequence and the loops then take control back.
- **Pure nodes** (no flow pins) are evaluated when a consumer needs them: once
  per consumer, each time that consumer runs. Two Print nodes reading one
  Random node get two different numbers.
- **Nodes with flow pins** keep their outputs. Reading one later gives the
  value from the last time that node ran.
- **Event graphs share one set of registers per instance**, which live as long
  as the instance. That is why a Delay can resume with every pin intact, and
  why a Delay is not allowed in a function (a function's frame is gone when it
  returns).
- **Delay** ignores being run again while it waits. Retriggerable Delay starts
  over. A Sequence does not wait for a Delay inside one of its chains.
- **Arrays** are shared when read and copied when written: `Get list` then
  `Add` changes the variable; `Set other = list` makes an independent copy.
- **Conversions on a wire:** Int to Float, Bool to Int or Float, anything to
  String. Float to Int is allowed with a warning (it truncates).
- **Run-time mistakes do not stop the game.** A bad array index or a division
  by zero gives the default value and reports the node. A loop that never ends
  is stopped after `instruction_budget` steps; a runaway recursion after
  `max_call_depth` frames.

## How a graph becomes code

Function 0 of a program is the event unit: every event graph, compiled
together. Functions 1..n are the script's functions.

Each (node, flow input) is a block. A block evaluates its data inputs, does
its work, and jumps to the next block or emits `END`. `END` pops a
continuation if a Sequence or loop pushed one, otherwise it leaves the frame.

Registers are persistent (outputs of flow nodes, loop counters, event
parameters, locals) or temporary (scratch for one block, reused by the next).
Nothing reads a temporary after control left the block and came back, which is
why loop code reloads its constants after each continuation label.

Operators on Int and Float compile to single opcodes. Everything else is a
call through the registry. The measured cost of one loop pass doing get
variable, convert, add, set variable is about 60 ns on the development
machine.

## Adding nodes

```cpp
reg.pure("vec3.length", "Math|Vector", "Length",
         [](glm::vec3 v) { return double(glm::length(v)); });

reg.fn("entity.set_position", "Entity", "Set Position",
       [](Context& ctx, EntityId e, glm::vec3 p) { /* ... */ },
       {"Target", "Position"});
```

A leading `Context&` parameter is not a pin. Several outputs are a
`std::tuple`. Supported C++ types are in `detail::Marshal` in `registry.h`.

## Debugging support already in the VM

`set_breakpoint(program, graph, node)`, `state()`, `paused_at()`,
`paused_register()`, `resume(Step::Continue | Into | Over | Out)`. A
breakpoint stops before its node runs; while paused, `fire()` and `tick()` do
nothing. The editor UI for this is milestone A4.

## Not built yet

Macros, event dispatchers, interfaces, parent scripts, timelines, user enums
and structs, maps and sets, calling events and functions on other entities.
`Type::sub` and `Kind::Enum` / `Kind::Struct` are reserved for them.
