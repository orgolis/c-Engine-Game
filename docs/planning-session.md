
Game: Worldshaper
/
The plan is to make a game via an own game engine, the plan for the game is mostly done. Now it comes to making the Engine, what would the plan be to make the own Engine

1:32 PM

Deliberated clarifying questions to guide engine development strategy
Deliberated clarifying questions to guide engine development strategy
Great ambition! Before laying out a roadmap, a few quick questions to tailor the plan:

Q: What kind of game is it? (This shapes the engine's core needs)
A: 3D (first/third person, open world, etc.)

Q: What language / tech stack are you planning to use?
A: C / C++

Q: What's your main motivation for building a custom engine? (Select all that apply)
A: Performance / specific optimization, Full control & learning, Avoid licensing / dependencies

1:33 PM

Synthesized game engine requirements across architecture, language, and motivation
Synthesized game engine requirements across architecture, language, and motivation
Solid foundation. Here's a full roadmap for building a 3D C/C++ game engine from scratch:

Here's a full structured roadmap for building your 3D C/C++ game engine, from the ground up:

Phase overview
A custom 3D engine is built in layers — each one depends on the one below it. The key is never building upward until the layer beneath is solid. Here's the full architecture:



Phase 1 — Foundation (weeks 1–3)
This is the unglamorous bedrock everything else depends on. Start with a custom math library — Vec2, Vec3, Vec4, Mat4, Quaternion, and all the transforms you'll need (projection, model-view, etc.). Don't use GLM yet if learning is a goal, but keep it as a reference. Alongside that, build a memory management layer: a general allocator, a stack allocator for per-frame scratch memory, and a pool allocator for fixed-size objects like components. Then add a logging system, a file I/O abstraction, and your own basic containers (or a thin wrapper around STL if you prefer). Get a CMake build system set up from day one — you'll thank yourself later.

Phase 2 — Platform & window (weeks 3–5)
Abstract the OS away early. Use GLFW or SDL3 for windowing and input — these are well-understood, license-friendly, and give you a GPU context in a day. Behind a platform abstraction layer, write thin wrappers so the engine never calls GLFW directly. This pays dividends if you later target consoles or other platforms. For the GPU API, OpenGL (4.6 Core) is the gentlest start; Vulkan gives maximum control and is worth the steeper ramp if performance is a primary goal — many custom engine devs start with OpenGL and migrate the renderer later.

Phase 3 — Renderer (weeks 5–12)
The biggest phase. Build it as a render graph — a DAG of passes (shadow pass → geometry pass → lighting pass → post-process pass → present). This keeps the renderer composable and makes features like deferred shading or screen-space effects drop in cleanly. The milestones, in order:

Forward renderer with a single directional light
Camera + frustum culling
Mesh loading (implement a simple OBJ loader first, then GLTF via cgltf or tinygltf)
Phong/Blinn-Phong shading in GLSL/HLSL
Shadow maps (PCF soft shadows)
Deferred shading pipeline (G-buffer: albedo, normal, position, material)
PBR materials (metallic-roughness workflow)
Skybox + IBL for ambient lighting
Post-processing: tonemapping (ACES), bloom, SSAO, FXAA/TAA
Phase 4 — Core systems (weeks 12–20)
With a renderer alive, layer in the engine's runtime systems:

Entity-Component System (ECS): Build a data-oriented ECS (think EnTT-style archetypes) rather than a GameObject hierarchy. This is critical for 3D open-world performance — tight memory layout means the CPU cache is happy. EnTT is an excellent, license-friendly reference or drop-in library.

Physics: Integrate Jolt Physics (MIT license, excellent 3D rigid body + character controller) or Bullet. Write a thin wrapper so the engine talks to a PhysicsWorld interface, not Jolt directly.

Input system: Debounce raw platform events into a clean action-mapping layer (actions like Jump, MoveForward map to keys/gamepad axes). This keeps game code free of raw keycodes.

Audio: Integrate miniaudio (single-header, public domain) for spatial 3D audio. Add a simple AudioSource component.

Resource/asset system: A reference-counted handle system (Handle<Mesh>, Handle<Texture>) backed by an async loader. Assets load off the main thread and the handle goes valid when ready.

Phase 5 — Editor & tools (weeks 20–28)
An in-engine editor accelerates every subsequent hour of game development. Integrate Dear ImGui for the UI — it's immediate-mode, renders into your own render pass, and is extremely fast to build with. Build a scene hierarchy panel, an inspector for components, a viewport with gizmos (translate/rotate/scale), and a basic asset browser. Add a profiler overlay (GPU timers per render pass, frame time graph) early — you'll need it.

Phase 6 — Game layer
Only now do you build game-specific systems on top of the engine: your open-world streaming system (chunk-based or cell-based world loading), character controller, AI, quest/dialogue systems, etc. The point of all the abstraction below is that this layer should feel like writing a game, not fighting infrastructure.

Recommended dependencies (all permissive licenses)
Purpose	Library
Windowing / input	GLFW 3 or SDL 3
Graphics API	OpenGL 4.6 or Vulkan (via volk)
Math	Your own or GLM
ECS	EnTT
Physics	Jolt Physics
Audio	miniaudio
UI (editor)	Dear ImGui
GLTF loading	cgltf or tinygltf
Image loading	stb_image
Shader compiler	glslang or shaderc
Key principles to hold throughout
Profile before you optimize. The engine will have hot paths you don't expect — measure with a GPU profiler (RenderDoc, NSight) before guessing. Data-oriented thinking from day one — lay out data in arrays of structs-of-arrays (SoA), not trees of objects on the heap. Keep layers decoupled — the renderer should not know what a Player is. And commit to iterating in vertical slices: a bouncing lit sphere on screen beats a perfectly architected but invisible system every time.

Would you like me to go deeper on any specific layer — for example, designing the ECS, the render graph architecture, or the Vulkan setup?

The Game in general should end up being an large Open world adventure RPG with Multiplayer support, Complex Ability set via Skill tree, Advanced movement capability, Complex Combat System, advanced and Intelligent security enemies and Bosses, Game inspirations are Wuthering Waves, Destiny 2, Elden Ring, Cyberpunk

1:51 PM

Synthesized architectural implications and prioritized development roadmap strategically
Synthesized architectural implications and prioritized development roadmap strategically
That's an incredibly ambitious scope — and a fantastic one. Those four inspirations together define almost every hard engineering problem in game development. Let me break down exactly what that means for your engine.

Those four games together represent almost every hard engineering challenge in the industry. Let me map exactly what each inspiration demands from your engine — this fundamentally changes the architecture from a generic 3D engine into something purpose-built.



Now let me break down the critical implications of each inspiration and what they uniquely demand from your engine:

What each game demands technically
Wuthering Waves is your movement and combat animation benchmark. The defining feature is a fluid, high-speed character controller where air dashes, wall interactions, grapple, and combat all blend seamlessly. This requires a movement state machine with dozens of states and a priority-based input buffer so inputs queued during animations don't get lost. The animation system needs full blend tree support with root motion — the character's position is driven by the animation clip, not just the physics capsule, which is what makes movement feel weighty and real.

Destiny 2 is your multiplayer and build complexity benchmark. The ability system there is a deep modifier pipeline — every piece of gear, every perk, every exotic buff feeds into a unified stat calculation. You'll need a data-driven ability graph where abilities are defined in data files (JSON/custom binary), not hardcoded, so designers can tune and compose builds without touching C++. The multiplayer side demands rollback netcode for combat (critical when abilities and hit detection need to feel immediate for all players) combined with server-authoritative state for anti-cheat.

Elden Ring is your open world, combat depth, and AI benchmark. The combat model is frame-data driven — every attack has startup frames, active hitbox frames, and recovery frames, and the parry/poise system is built on those exact windows. Your hitbox system needs to be decoupled from the render mesh (separate collision capsules per bone per animation frame). Boss AI requires a phase-based state machine sitting above a behavior tree — a boss transitions phases based on HP thresholds, changes its entire moveset, and the behavior tree governs the micro-decisions within each phase.

Cyberpunk 2077 is your world density, narrative, and rendering benchmark. The thing Cyberpunk does that costs the most is seamless world streaming at high visual density — thousands of unique props, NPCs with schedules, and interior spaces that load without visible seams. Your streaming system needs asynchronous chunk loading on a background thread pool with a priority queue based on player position and velocity prediction. The narrative system needs a directed graph of world state flags that quests, NPCs, and the environment all read from — so a choice made 10 hours ago can ripple into a side quest encountered later.

Revised build order given this scope
The game-specific requirements reshape the standard engine phases significantly. The most important adjustment is that networking and the movement system must be architected into the engine from Phase 2, not bolted on later. A multiplayer-first movement system is fundamentally different from a single-player one — every position update, every ability activation, every hit detection must be designed with authority and replication in mind from the start. Retrofitting this is one of the most painful rewrites in game development.

The second major adjustment is that the ECS design must support the ability modifier pipeline. Destiny-style builds require that any component on an entity can be a source of stat modifiers, and the final stats are computed lazily by aggregating all active modifiers. This is a specific data layout decision — an archetype ECS handles this differently than a sparse-set ECS, and you want to choose before writing gameplay code.

The third is that the AI system should be built in parallel with the combat system, not after it. Boss behavior trees need to query the same hit-state data, animation state, and ability cooldowns that the player combat system produces. If they're built sequentially, you'll spend weeks building bridges between them.

The hardest single problem: movement × networking × animation
The intersection of Wuthering Waves movement, Destiny multiplayer, and Elden Ring combat is the most technically brutal part of this project. In a multiplayer game, the server must be authoritative over player position — but if you wait for server confirmation on every dash or jump, the game feels laggy and dead. The solution is client-side prediction with server reconciliation: the client runs movement logic locally for instant response, the server runs the same logic, and when they diverge (due to latency or cheating), the client smoothly corrects. This works fine for simple movement but becomes extremely complex when abilities, hitboxes, and combat state are involved. Plan for this to be a 3–6 month engineering effort on its own.

Would you like to go deep on any specific system? The movement state machine architecture, the ability modifier pipeline design, the boss AI behavior tree structure, or the networking model are all worth a dedicated breakdown.

Would it be a good idea to try adding procedural animations so animations would feel more natural

2:00 PM

Weighed procedural animation benefits and layering strategies thoughtfully
Weighed procedural animation benefits and layering strategies thoughtfully
Short answer: yes, but it should layer on top of authored animations, not replace them. The distinction matters enormously for a game of this scope.

The right mental model
Think of it as a three-tier stack:

Base layer — hand-authored animations (attacks, idles, jumps). These define the character's personality and game feel. You cannot procedurally generate a satisfying sword swing — artists need to craft that.
Procedural adjustment layer — runtime corrections that make the authored animations respond to the world. Foot IK so feet don't float above uneven terrain. Aim IK so the spine rotates toward where the camera is pointing. Head look-at so the character's eyes track a nearby enemy.
Fully procedural layer — secondary motion that would be impractical to author. Cloth, hair, cape physics. Weapon sway and bob. Ragdoll on death. Hit reaction wobble.
The mistake to avoid is trying to make the base layer procedural — generating locomotion or attack animations entirely at runtime. Games like Overgrowth did this and while technically impressive, the results feel uncanny and lack the punch that Wuthering Waves or Elden Ring combat is built on.

What's worth building for your game specifically
Given your four inspirations, here's where procedural animation pays off most:

Inverse Kinematics (IK) — highest priority. Foot placement IK is essential for an open world. When a character stands on a slope or steps onto a rock, authored animations will have their feet floating or clipping through the ground. A two-bone leg IK solver corrects each foot's position and rotates the pelvis to compensate, and it runs every frame on top of whatever animation is playing. Aim IK (rotating the spine and shoulder to track the camera direction) is what makes third-person shooting feel responsive rather than like the character is locked to a facing direction. Both of these are relatively straightforward to implement — a two-bone IK solver is maybe 200 lines of C++.

Secondary motion — high priority for visual quality. A physics-driven chain solver for capes, hair, and equipment attachments makes characters feel alive in a way that authored animations simply can't match, especially during the high-speed movement Wuthering Waves is known for. You give each chain link a mass, a stiffness, and a damping value, and simulate it as a verlet integration chain each frame. The result is that a cape billows naturally during a dash and settles when the character lands, without a single animation clip being authored for it.

Procedural hit reactions — medium priority, high feel impact. When an enemy hits you, instead of interrupting the current animation with a dedicated hit-react clip, you can apply a procedural impulse to the spine and shoulder bones that decays over a few frames. This means hits feel impactful regardless of what animation is currently playing — you can be mid-combo and still visibly react to taking damage. Elden Ring's poise system is built on this idea.

Locomotion blending adjustments — medium priority. This isn't fully procedural locomotion, but rather using the character's actual velocity vector to warp the blend weights between directional movement animations. If the character is moving at a 45-degree angle, you interpolate between the forward and strafe animations based on the angle rather than snapping between them. Combined with a proper blend tree this makes movement feel significantly more fluid.

What to be cautious about
Full procedural locomotion (like Motion Matching, which Ubisoft developed and is used in games like For Honor) is genuinely powerful but represents months of engineering and requires a very large animation database to source from. It's worth knowing about — Unreal 5's Motion Matching plugin is a good reference — but it's a Phase 5+ feature, not something to architect in from the start.

Over-proceduralism in combat is a real risk. The reason Elden Ring attacks feel satisfying is that an animator crafted every frame of the windup and follow-through. If you procedurally warp those animations too aggressively to respond to the environment, you lose the authored timing that makes the combat feel weighty. A light touch — small additive corrections, not wholesale deformation — is the rule.

Implementation order
The practical sequence for your engine is to build IK solvers first (they're mathematically self-contained and immediately visible), then the secondary motion chains, then the hit reaction impulse system. Each one is a relatively bounded module that sits in the animation layer and reads from the same bone transform data your blend tree already produces.

The key architectural point is that all of these systems should be additive and stackable — each one takes the current bone pose as input, applies its correction, and outputs a modified pose. The rendering system just consumes the final pose and never knows whether a given bone position came from an animation clip, an IK solver, or a physics chain. This composability is what makes the system maintainable as it grows.

Would you like a deeper look at the IK solver architecture, or the secondary motion chain simulation?

So how would the plan for the development of the engine now look considering the discussed details

2:05 PM

Orchestrated interactive navigator and contextualized supporting narrative
Orchestrated interactive navigator and contextualized supporting narrative
This is now a significantly more specific plan than a generic engine roadmap. Everything discussed — the four game inspirations, the multiplayer-first architecture, the procedural animation stack, the ability modifier pipeline — reshapes the phase order and what gets built inside each phase. Here's the full revised plan:



A few things that are worth calling out about how this plan differs from a generic engine roadmap:

The three architectural decisions that can't be undone
The first is the networking model. The movement state machine in Phase 6 must be expressed as replayable deterministic commands before Phase 8 begins. This isn't just an implementation detail — it's a fundamental constraint on how you represent movement state. If you skip this, Phase 8 becomes a full rewrite of Phase 6.

The second is the ECS + modifier pipeline designed in Phase 4. Destiny 2's build depth requires that every equipped item, passive skill, and status effect can contribute to a unified stat calculation. If the ECS is designed without this in mind, the ability system in Phase 11 will fight the data layout instead of working with it.

The third is the render graph in Phase 3. Adding deferred shading, a shadow atlas, VFX passes, and clustered lighting to a monolithic renderer means rewriting it. Designing it as a DAG of composable passes from the start means each new feature is a new node, not a surgery.

Realistic timeline
The phases as shown overlap intentionally — the renderer and ECS run in parallel, the AI architecture starts while combat is being built, tooling begins incrementally from Phase 3 onward. A realistic estimate for a single developer reaching a polished vertical slice is 18–24 months. For a small team of 3–5 experienced C++ engineers, 12–16 months to vertical slice is achievable. The networking phase alone is typically a full-time engineer for 4–6 months.

Where to start tomorrow
Phases 1 and 2 are the only parts with no external dependencies and no risky architectural decisions — just solid C++ engineering. Get the math library, allocators, and window/GPU context working first. Everything else in this plan flows from that base.

Ok so i can follow this plan nicely i need to setup everything, i would like to use claude code and setup everything correctly in the github repository. currently as a planing tool is google docs in use. i have Visual Studio and Visual Studio Code on my Pc, with Windows 11 and a Linux debian, also WSL is installed. What and how should i setup the work enviroment

6:05 PM

Weighed environment setup priorities and identified key configuration decisions
Weighed environment setup priorities and identified key configuration decisions
A few quick questions before laying out the full setup — these decisions shape everything else:

Q: Where will you primarily write and compile code?
A: Both equally — cross-platform from day one

Q: Which GPU API are you starting with? (As discussed, this is a permanent decision)
A: OpenGL 4.6 (faster start, easier debug)

Q: How do you want to manage the GitHub repo?
A: Solo now, team later

6:11 PM

Architected comprehensive cross-platform development setup guide
Architected comprehensive cross-platform development setup guide
Perfect combination of choices. Cross-platform from day one with OpenGL is the right balance of ambition and pragmatism. Here's everything, in order:

Step 1 — Install Claude Code
Claude Code runs in your terminal. On both Windows (PowerShell) and WSL:

First, install Node.js if you don't have it (needed for Claude Code). Then in both PowerShell and WSL terminal:

bash
npm install -g @anthropic-ai/claude-code
To use it, cd into your project folder and run claude. It reads your codebase and you work with it conversationally right in the terminal. It can read, write, and run files — so you can ask it to implement a system, debug a build error, or refactor a module directly.

Step 2 — Create the GitHub repository
Create a new private repo on GitHub named something like engine or your project name. Then clone it locally on Windows and in WSL — you'll work from both.

bash
# Windows PowerShell
git clone https://github.com/yourusername/yourengine.git C:\dev\yourengine

# WSL terminal
git clone https://github.com/yourusername/yourengine.git ~/dev/yourengine
```

---

## Step 3 — Repository structure

This is the folder layout to create. It's designed so Visual Studio, VS Code, and CMake all understand it without fighting each other:
```
yourengine/
│
├── CMakeLists.txt              ← root CMake, wires everything together
├── CMakePresets.json           ← build configs for Windows + Linux
├── .clang-format               ← code style enforced across both platforms
├── .gitignore
├── .gitmodules                 ← third-party deps as submodules
│
├── engine/                     ← all engine code, split by phase
│   ├── core/                   ← Phase 1: math, memory, logging, I/O
│   ├── platform/               ← Phase 2: window, input, GPU context
│   ├── renderer/               ← Phase 3: render graph, PBR, VFX
│   ├── ecs/                    ← Phase 4: entity system, asset pipeline
│   ├── animation/              ← Phase 5: blend trees, IK, secondary motion
│   ├── physics/                ← Phase 7: Jolt integration, hitboxes
│   ├── network/                ← Phase 8: UDP, prediction, replication
│   ├── ai/                     ← Phase 10: behavior trees, pathfinding
│   └── audio/                  ← miniaudio integration
│
├── game/                       ← Phase 15: game-specific systems only
│
├── editor/                     ← Phase 14: Dear ImGui editor
│
├── tests/                      ← unit tests per module (Catch2)
│   ├── core/
│   ├── ecs/
│   └── ...
│
├── third_party/                ← git submodules, never modified directly
│   ├── glfw/
│   ├── glad/
│   ├── spdlog/
│   ├── glm/
│   ├── stb/
│   ├── entt/
│   ├── jolt/
│   ├── imgui/
│   └── catch2/
│
├── assets/                     ← meshes, textures, shaders (git-lfs later)
│   ├── shaders/
│   └── meshes/
│
└── docs/                       ← architecture notes, migrated from Google Docs
    ├── engine-plan.md
    └── systems/
Step 4 — Key config files
Here are the exact files to create, starting from scratch:

CMakePresets.json — this is what makes both Visual Studio and VS Code pick up the project automatically without manual configuration:

json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "windows-msvc-debug",
      "displayName": "Windows MSVC Debug",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_STANDARD": "20"
      },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    },
    {
      "name": "windows-msvc-release",
      "displayName": "Windows MSVC Release",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_STANDARD": "20"
      },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    },
    {
      "name": "linux-gcc-debug",
      "displayName": "Linux GCC Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_STANDARD": "20",
        "CMAKE_C_COMPILER": "gcc",
        "CMAKE_CXX_COMPILER": "g++"
      },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" }
    },
    {
      "name": "linux-gcc-release",
      "displayName": "Linux GCC Release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_STANDARD": "20"
      }
    }
  ],
  "buildPresets": [
    { "name": "windows-debug", "configurePreset": "windows-msvc-debug" },
    { "name": "windows-release", "configurePreset": "windows-msvc-release" },
    { "name": "linux-debug", "configurePreset": "linux-gcc-debug" },
    { "name": "linux-release", "configurePreset": "linux-gcc-release" }
  ]
}
.clang-format — enforces consistent style on both platforms:

yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
AccessModifierOffset: -4
NamespaceIndentation: None
SortIncludes: true
```

**`.gitignore`**:
```
build/
.vs/
.vscode/settings.json
*.user
*.suo
*.sdf
*.opensdf
CMakeCache.txt
CMakeFiles/
*.DS_Store
.gitmodules — add third-party deps as submodules so they're pinned to exact versions:

ini
[submodule "third_party/glfw"]
    path = third_party/glfw
    url = https://github.com/glfw/glfw.git
[submodule "third_party/spdlog"]
    path = third_party/spdlog
    url = https://github.com/gabime/spdlog.git
[submodule "third_party/glm"]
    path = third_party/glm
    url = https://github.com/g-truc/glm.git
[submodule "third_party/entt"]
    path = third_party/entt
    url = https://github.com/skypjack/entt.git
[submodule "third_party/imgui"]
    path = third_party/imgui
    url = https://github.com/ocornut/imgui.git
[submodule "third_party/catch2"]
    path = third_party/catch2
    url = https://github.com/catchorg/Catch2.git
To pull them all after adding .gitmodules:

bash
git submodule update --init --recursive
Step 5 — Visual Studio setup (Windows)
Visual Studio 2022 natively understands CMake — you don't generate a .sln. Just do:

File → Open → Folder → select your repo root.

Visual Studio reads CMakePresets.json automatically and shows the presets in the toolbar dropdown. Select windows-msvc-debug, hit build. That's it — no solution file needed, no manual configuration.

For VS to find IntelliSense correctly, add a CMakeSettings.json only if the preset isn't picked up. In practice with VS 2022 it just works.

Step 6 — VS Code setup (Windows + WSL)
Install these extensions:

ms-vscode.cpptools — C/C++ IntelliSense
ms-vscode.cmake-tools — CMake integration
ms-vscode-remote.remote-wsl — work inside WSL from VS Code on Windows
xaver.clang-format — auto-format on save
Create .vscode/extensions.json to recommend these to future team members:

json
{
  "recommendations": [
    "ms-vscode.cpptools",
    "ms-vscode.cmake-tools",
    "ms-vscode-remote.remote-wsl",
    "xaver.clang-format"
  ]
}
Create .vscode/launch.json for debugging (works on both platforms):

json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug Engine (Linux)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/linux-debug/game/game",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "MIMode": "gdb",
      "setupCommands": [
        { "text": "-enable-pretty-printing", "ignoreFailures": true }
      ]
    }
  ]
}
When you open the repo in VS Code and connect to WSL via Remote-WSL, VS Code runs entirely inside the Linux environment — the same repo, the same files, but compiled with GCC and debugged with GDB. This is seamless once Remote-WSL is installed.

Step 7 — WSL (Debian) toolchain
In your WSL terminal, install everything needed:

bash
sudo apt update && sudo apt install -y \
  build-essential \
  gcc g++ \
  cmake ninja-build \
  libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev \
  git clang-format \
  gdb valgrind
```

OpenGL headers and X11 are needed for GLFW to compile on Linux. Everything else is standard.

---

## Step 8 — Branching strategy (solo now, team later)

Keep it simple now but structured enough that adding a team member doesn't require reorganising:
```
main          ← always compiles, always passes tests. Protected branch.
dev           ← your active work branch. Merges into main via PR.
phase/01-foundation
phase/02-platform
phase/03-renderer
feature/math-library
feature/memory-allocators
Work on phase/ or feature/ branches, merge into dev when a milestone passes, merge dev into main when a full phase is complete. When a team joins, each person gets their own feature/ branch and opens PRs into dev. The structure is already there.

Step 9 — GitHub Actions CI
Add .github/workflows/ci.yml — this automatically compiles on both Windows and Linux on every push:

yaml
name: CI

on: [push, pull_request]

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y cmake ninja-build libgl1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
      - name: Configure
        run: cmake --preset linux-gcc-debug
      - name: Build
        run: cmake --build --preset linux-debug
      - name: Test
        run: ctest --test-dir build/linux-debug --output-on-failure

  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake --preset windows-msvc-debug
      - name: Build
        run: cmake --build --preset windows-debug
This catches cross-platform breakage immediately — if your code compiles on Windows but not Linux (or vice versa), the CI fails before you move on.

Step 10 — Google Docs → GitHub
Rather than keeping engine plans in Google Docs disconnected from the code, move them into the repo under docs/. Markdown in the repo means plans are versioned alongside the code, visible in GitHub's web UI, and accessible to Claude Code when it reads your codebase.

Create docs/engine-plan.md and paste the phase plan in there. Create docs/systems/ for individual system design notes as you write them. GitHub renders Markdown natively, and GitHub Wiki is another option if you want a navigable sidebar — but for a solo project, docs/ in the repo is simpler and more integrated.

For active task tracking, use GitHub Issues + Milestones instead of a Google Doc checklist. One issue per system, milestones matching your phases. When Claude Code helps you implement something, you close the issue. When a team joins, they can see exactly what's in progress.

The first thing to do right now
Once the repo is set up:

bash
cd yourengine
claude
Then tell Claude Code: "Set up the Phase 1 CMakeLists.txt with the core module, a math library stub with Vec3 and Mat4, and a basic logging system using spdlog. Cross-platform, C++20."

It'll scaffold the files, write the CMake, and you can start actually building from there.

Recommended apps and extensions
Claude Code
Build, debug, and ship from your terminal.

Claude Code for VS Code
Ask Claude to explain, edit, or fix code right in VS Code.
The VS Code extension is worth installing alongside the terminal version — it lets you use Claude Code directly inside VS Code with your files open, which is useful when reviewing what it's generated or asking it to fix something in a specific file you're looking at.

im currently at step 4 and wonder how i could get this chat into VSC

7:54 PM

Weighed export options and VS Code integration methods
Weighed export options and VS Code integration methods
There are two separate things you probably want here — the conversation content as a reference document, and Claude itself inside VS Code.

Getting this conversation into your repo
On this page, select all the text you want to keep (or use your browser's print/save feature), then paste it into a new file. The cleanest approach is saving it as docs/planning-session.md in your repo so it's versioned alongside your code and Claude Code can read it as context when you're working.

Alternatively, just copy the phase plan specifically into docs/engine-plan.md — that's the part you'll actually reference while coding.

Getting Claude inside VS Code
This is the more useful thing. The Claude Code VS Code extension I suggested gives you a Claude panel directly inside VS Code. Once installed:

Open VS Code
Press Ctrl+Shift+P → type Claude → open the Claude Code panel
Claude can see your open files, your repo structure, and you can continue conversations like this one right there while coding
The key advantage over this browser chat is that Claude Code in VS Code has direct access to your actual files — so you can say "implement the Vec3 struct in engine/core/math/vec3.h" and it reads and writes the file directly rather than you copying code back and forth.

To install it, the card above has the direct link. After installing, run claude once in your terminal to authenticate, and the VS Code extension picks up the same session automatically.
