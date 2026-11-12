# Visual scripting: how the Logic Graph core works

**Status (v0.8.18):** milestone A1 (the core) is done; A2 (engine binding) and
A3 (the editor) of [ROAD_TO_TEST_GAME.md](ROAD_TO_TEST_GAME.md) are partly done.
The per-scene Logic Graph panel still runs the old `gameplay_logic.h` runtime
until the rest of A2 replaces it.

Code: `engine/core/vscript/`. Checks: `tools/vscript_check` (71 checks) for the
core, `tools/script_check` for graphs running on entities.

## In the editor today

**A logic script is a file.** A file ending in `.lgraph` on an entity's Script
component is a logic graph. It runs through the same `ScriptSystem` as Python,
C++ and C# scripts: Begin Play once, Tick every frame, End Play when Play
stops, hot reload when the file changes, and a compile error shown as the
component's status with the node it is about.

**Getting to the graph editor.** Any of these opens the script in a tab of the
Logic Script panel (View > Logic Script Editor):

- double-click a `.lgraph` in the Asset Browser, or right-click > Open;
- "Edit Graph" in the Inspector's Script section;
- "New Logic Graph" in the Inspector, on a Script component with no file: it
  creates `assets/scripts/<object name>.lgraph`, assigns it and opens it;
- New > Script > Logic Graph in the Asset Browser;
- "Open..." in the panel, which lists every `.lgraph` under `assets/`.

Each script has its own tab, its own undo history and its own compile result.
Closing a tab with changes asks first.

**Nodes for the engine.** `editor/src/script_host_logic.cpp` registers every
`ScriptApi` entry as a node (entity, input, physics, rendering, audio,
gameplay, items, weapons, world flags and events, script parameters), so adding
an engine capability to `ScriptApi` needs one more line there.

**Events the engine raises.** Begin Play, Tick, End Play, Key Pressed and Key
Released (one node per key), and On Gameplay Event (one node per event name:
trigger enter and exit, damage, pickups, anything sent with Emit Event).

**Variables in the Inspector.** A variable with "show in Inspector" ticked is a
field of the Script component, so two objects with the same script can differ.
Bool, int, float, string, vec2, vec3, vec4, color and entity are supported. An
entity field is kept as the object's **name** (type it, or drag the object from
the Hierarchy), so it survives save and load; it is looked up when the script
starts.

**Scripts talking to each other.** Three nodes take a Target entity:

| Node | What it does |
|------|--------------|
| Send Event to Entity | Runs the custom event with that name on the target's script, now, then continues. The node lists what it sends; the values are matched to the event's parameters by position and converted to their types |
| Get Variable of Entity | Reads a variable of the target's script, as the type picked on the node |
| Set Variable of Entity | Writes one, converted to the variable's own type |

A target with no logic script, or without that event or variable, is not an
error: nothing happens, and a read gives the type's default. Every script in
the scene is loaded before any Begin Play runs, so Begin Play may send to
another object. That object's own Begin Play may not have run yet.

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
  `max_call_depth` frames (two scripts sending events to each other without
  end are stopped the same way).
- **An event's parameters always have their declared types.** Whoever fires it
  (the engine, another script) has its arguments converted; one that cannot be
  converted, or was not given, is the type's default.

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
and structs, maps and sets, calling a function (with results) on another
entity, collision events, nodes for component properties, the debugger UI,
comments, copy and paste. `Type::sub` and `Kind::Enum` / `Kind::Struct` are
reserved for the first group.
