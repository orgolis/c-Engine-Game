# Visual scripting: how the Logic Graph core works

**Status (v0.8.22):** milestones A1 (the core), A3 (the editor) and A4 (the
debugger) of [ROAD_TO_TEST_GAME.md](ROAD_TO_TEST_GAME.md) are done; A2 (engine
binding) is partly done.
The per-scene Logic Graph panel still runs the old `gameplay_logic.h` runtime
until the rest of A2 replaces it.

Code: `engine/core/vscript/`. Checks: `tools/vscript_check` (104 checks) for the
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

**Working on the canvas.**

| To | Do |
|----|----|
| Add a node | Right-click empty canvas, or drag a wire off a pin and drop it |
| Select | Click; Shift- or Ctrl-click adds or removes; drag on empty canvas for a box |
| Move | Drag a selected node: the whole selection moves |
| Copy, cut, paste, duplicate | Ctrl+C, Ctrl+X, Ctrl+V (at the mouse), Ctrl+D. Works between graphs, tabs and editors: the clipboard holds text |
| Delete | Del |
| Cut a wire | Alt-click the wire, or Alt-click a pin for all of its wires |
| Route a wire | Double-click it: a knot appears that can be dragged. Works on flow and data wires |
| Comment a group | Select nodes and press C: a titled box wraps them. Drag its title to move it with the nodes inside, its corner to resize. Text and colour are in the side panel |
| Note on one node | The text field under "Selected node"; shown above the node |
| Find the graph again | Home frames everything, F frames the selection |
| Pan, zoom | Middle-drag, wheel |

Right-clicking a node gives the same commands as a menu.

**Debugging, while the game plays.** With a script open in the graph editor
and an object running it:

- The nodes that just ran glow, and the flow wires the run took light up.
- Each variable shows what it holds now. Hovering a pin shows the value on it.
- **F9** on a node with flow pins sets a breakpoint (a red dot). They can be
  set before Play, belong to the file, and so apply to every object running
  it.
- When one is hit the game pauses, the mouse is released and the editor brings
  the node up, marked in orange. **Continue**, **Step** (F10: run this node,
  stop at the next), **Step Into** (F11: follow a function or event call),
  **Step Out** (Shift+F11).
- "Running on" picks which object is shown when several run the script.

Continuing finishes the frame the breakpoint interrupted: the stopped script's
other events, then the scripts whose turn had not come. Nobody loses a Tick to
a breakpoint. Stopping Play while a script is stopped is safe.

**Nodes for the engine.** `editor/src/script_host_logic.cpp` registers every
`ScriptApi` entry as a node (entity, input, physics, rendering, audio,
gameplay, items, weapons, world flags and events, script parameters), so adding
an engine capability to `ScriptApi` needs one more line there.

**Events the engine raises.**

| Node | Runs when |
|------|-----------|
| Begin Play, Tick, End Play | The object's life in Play mode |
| Key Pressed, Key Released | That key goes down or up (one node per key) |
| On Gameplay Event | An event with that name is published by anyone: damage, pickups, quests, anything sent with Emit Event |
| On Event About Me | The same, but only when this object is the event's target or instigator |
| On Trigger Enter, On Trigger Exit | For a script on a trigger volume: an actor enters or leaves it |

**Timeline.** Runs a value from 0 to 1 over a duration: the way to open a door,
move a platform or fade a light. Flow inputs Play, Play from Start, Stop,
Reverse, Reverse from End; Update runs every frame while it moves, with Alpha
(0 to 1, shaped by the node's curve: linear, ease in, ease out, ease in out)
and Time; Finished runs when it arrives. Feed Alpha to a Lerp. Like Delay, it
cannot be used in a function.

**Variables in the Inspector.** A variable with "show in Inspector" ticked is a
field of the Script component, so two objects with the same script can differ.
Bool, int, float, string, vec2, vec3, vec4, color and entity are supported. An
entity field is kept as the object's **name** (type it, or drag the object from
the Hierarchy), so it survives save and load; it is looked up when the script
starts.

**Scripts talking to each other.** These nodes take a Target entity:

| Node | What it does |
|------|--------------|
| Send Event to Entity | Runs the custom event with that name on the target's script, now, then continues. The node lists what it sends; the values are matched to the event's parameters by position and converted to their types |
| Call Function of Entity | Calls a function of the target's script and waits for it. The node lists what it hands over and what it expects back; both are converted. Results the function gives beyond what the node lists are dropped |
| Get Variable of Entity | Reads a variable of the target's script, as the type picked on the node |
| Set Variable of Entity | Writes one, converted to the variable's own type |

**Dispatchers** turn that round: a script announces something without knowing
who cares. The door's script declares a dispatcher `opened` (side panel,
"Dispatchers") and runs **Call opened** when it opens. A lamp's script runs
**Bind to Dispatcher** once, naming the door, the dispatcher and one of its own
custom events; from then on that event runs whenever the door calls. Listeners
run in the order they bound; **Unbind** stops one; a listener whose object is
gone is skipped. Bind and Unbind with Target unwired mean this object.

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
| `io.h` | Text format `VSCRIPT 2`: one record per line, tab-separated. Also copy and paste: a selection is the same text |

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
- **Timeline** keeps its position when stopped: Play and Reverse go on from
  where it is. Its Duration and Loop pins are read when a flow input runs, not
  every frame.
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

## How the debugger works

- **Where a stop can happen.** A flow node's code is interleaved with the pure
  nodes that feed it, so "the node changed" is true several times while one
  node runs. The compiler therefore marks the instruction where each flow
  node's run *starts* (`Program::stmt`), and breakpoints and steps look only at
  those. A pure node cannot have a breakpoint: it has no such moment.
- **The VM** (`vm.h`): `set_breakpoint`, `state()`, `paused_at()`,
  `resume(Step)`, `abort()` (drop a paused run when its script is going away),
  `frame_register()` and tracing (`trace`, `last_run()`), which costs nothing
  while off.
- **Debug information** (`Program::outputs`): where each flow node keeps its
  data outputs. An event graph's registers live as long as the instance, so
  its pins can be read at any time; a function's only while it is the paused
  frame.
- **The host** (`script_host_logic.cpp`) implements `LogicDebugger`
  (`editor/include/logic_debug.h`), which is all the graph editor sees.
  Compiled programs are shared per file, so a breakpoint on the file reaches
  every object running it.
- **The game.** `main.cpp` pauses playback when the VM stops and resumes it
  when the VM continues. A paused game does not update its scripts, in any
  language.

## Not built yet

Macros, interfaces, parent scripts, user enums and structs, maps and sets,
collision events, nodes for component properties, watch expressions
and a call stack view in the debugger, collapsing nodes to a function, find in
graph. `Type::sub` and `Kind::Enum` / `Kind::Struct` are
reserved for the first group.
