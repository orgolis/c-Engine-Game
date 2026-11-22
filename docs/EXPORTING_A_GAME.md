# Exporting a game

**Project > Export Game...** makes the open project into a game other people
play: a folder with the game's own program, and a `.zip` of it to share. The
game is the engine's player with your project inside, so what it shows is what
Play showed in the editor -- from the start scene you choose, with no editor
around it.

## Windows

### The dialog

| | |
|---|---|
| **Title** | the game's name: the program (`The Beacon.exe`), the window title, the folder, the zip. Default: the project's name |
| **Version** | goes into the zip's name and the program's file properties (`1.0.0`) |
| **Start scene** | where the game starts. Only the exported copy's manifest is changed -- your project's default scene stays |
| **Icon** | an image in the project (a `.png`, any size; it is fitted into a square). It becomes the program's icon in Explorer and the taskbar, and the window's. **Choose...** copies an image from outside the project into `config/` |
| **Start in full screen** | how the game starts the first time; the player switches with **F11** or **Alt+Enter**, and the game remembers their choice |
| **Also make a .zip** | the folder packed into `<Title>-<version>-windows-x86_64.zip` beside it |
| **Leave out / Keep anyway** | patterns (below) |
| **Export to** | a folder outside the project |

The lower half shows what an export would make while you change things: how
many files and how much, which scripts are built, what is left out (largest
first), and warnings -- for example a scene that uses a file a pattern leaves
out. **Export** is enabled when nothing blocks it (a missing start scene, a
folder inside the project). If the open scene has unsaved changes, the dialog
offers to save it first; nothing else in the project is changed.

When it is done: **Play** starts the exported game as a player would, **Open
Folder** shows it. Exporting again to the same folder replaces the earlier
export (a folder of that name that is not an export is never touched -- the
export stops instead).

The choices are kept in the project's `config/export.ini`:

```
title = The Beacon
version = 1.2.0
start_scene = scenes/start.scene
icon = config/icon.png
fullscreen = 1
zip = 1
output = D:/Exports
exclude = docs/
exclude = *.wav.bak
include = art_source/credits.txt
```

### What goes in

Everything in the project folder, except what a game does not need. These are
always left out:

| | |
|---|---|
| your own working files | `art_source/`, `saves/`, `script_cache/`, `cache/`, `diagnostics/`, `exports/`, `dist/`, `editor.ini`, `editor_layout.version`, the editor's `__mp_session.scene` |
| other manifests | `*.schizo` -- the game's `project.schizo` is written fresh, with the start scene |
| authoring files | `*.blend`, `*.blend1`, `*.psd`, `*.kra`, `*.xcf`, `*.spp`, `*.ztl`, `*.max`, `*.ma`, `*.mb`, `*.c4d` |
| notes and launchers | `*.md`, `*.cmd`, `*.bat`, `*.ps1`, `*.sh` |
| leftovers | `*.bak`, `*.bak_*`, `*.tmp`, `*.orig`, `*.log`, `Thumbs.db`, `.DS_Store`, `desktop.ini` |
| version control | `.git/`, `.svn/`, `.hg/`, `.vs/`, `.vscode/`, `.idea/` |

**Leave out** adds patterns, **Keep anyway** overrides any of them. A pattern
is like a `.gitignore` line, case does not matter:

- `*.wav` -- a name, at any depth (`*` and `?` stay within one name)
- `docs/` -- a folder (the trailing `/`), at any depth
- `config/dev.ini` -- with a `/` inside: from the project root
- `art_source/credits.txt` under **Keep anyway** keeps that one file of a folder that is left out

### Scripts

A player's computer has no compiler. Every C++ and C# script the game uses --
one a scene, a `.ui`, a data file or another script names -- is built when you
export, into the game's `script_cache/prebuilt/`, and the game loads those.

- **C++** (`.cpp`, `.cc`): built with the same g++ the editor uses
  (`GWS_SCRIPT_CXX`, else `C:/Strawberry/c/bin/g++.exe`, else `g++` on the PATH),
  as one self-contained DLL -- its C++ runtime is inside, so it depends on
  nothing but Windows.
- **C#** (`.cs`): built with `dotnet build`. Players need the **.NET 9 runtime**
  (not the SDK); the dialog says so, and so does the game's README.
- Python and logic scripts need nothing: they run inside the engine.

A script that does not build stops the export with the compiler's message; a
`.cpp` that is only `#include`d by a script, or that nothing names, is copied
but not built on its own. Each entity running a script still gets its own copy
(its own globals / statics), as in the editor.

### The game

```
The Beacon/
    The Beacon.exe          the engine's player: your icon and version inside; no console window
    *.dll, glslangValidator.exe
    game.ini                title, version, the engine, full screen at first start
    game-export.marker      tells the program it is a game
    icon.png                the window's icon
    README.txt              for players: how to start, where their settings are, what it needs
    licenses/               the engine's licence and third-party notices
    assets/scripts/         the engine's script headers
    project/                your project (as above) with script_cache/prebuilt/
The Beacon-1.2.0-windows-x86_64.zip
```

A player unpacks the zip anywhere -- also under Program Files: the game never
writes into its own folder -- and starts the program. It needs 64-bit Windows
10 or 11 and a graphics card with Vulkan.

**The player's files**, per player and game, in
`%APPDATA%\GameWorldshaper\games\<project>\`:

| | |
|---|---|
| `render_settings.txt` | graphics. On first start a preset is picked from the GPU; a settings menu changes it (Set Setting) |
| `controls.txt` | key bindings, mouse, field of view, full screen |
| `saves/` | F6 / F9 and scripted saves |

The game's own log and crash reports go where the engine's do,
`%LOCALAPPDATA%\GameWorldshaper\diagnostics\`.

**Nothing of the editor in it.** The game draws its whole HUD as Play does in
the editor -- what E would do and the names of things to pick up, the
crosshair and ammunition, the ability bar, a script's messages, the death
notice, stamina, being hit -- and none of the editor's: Esc is only ever the
game's. With `escape = game` in `project.schizo` -- or no `escape` line and game
logic that listens for Escape -- the project's own pause menu gets it; else the
engine's (Paused: Resume / Quit game). Shift+F1 and the
editor's "click to resume" do not exist in a game.

**If the graphics driver resets** (a frame ran too long and Windows reset the
GPU), the game says so and closes rather than staying black, and the log
names it (`[gpu] the GPU device was lost`).

### Without the dialog

```
editor.exe --project "C:\path\to\MyGame\project.schizo" --export-game "D:\Exports"
```

exports with the project's saved `config/export.ini`, printing its progress,
and exits 0 when the game was made. `tools/export_check` (part of `gws test`)
exports a small project this way and reads the result back.

## Linux

The Linux editor exports a Linux game and, through a cross compiler, a Windows
one -- from **Project > Export Game...**, choosing `Linux`, `Windows` or both:

- `MyGame-linux-x86_64.run` -- one self-extracting file: `chmod +x` it and run it.
- `MyGame-windows-x86_64.zip`. The first Windows export downloads a pinned
  portable llvm-mingw compiler (about 84 MB) into `build/toolchains`.

From a terminal:

```bash
tools/export_game_linux.sh "/path/to/MyGame/project.schizo" "/path/to/exports/MyGame-linux"
tools/export_game_windows_cross.sh "/path/to/MyGame/project.schizo" "/path/to/exports/MyGame-windows"
```

These scripts start the game at the scene that is open in the editor (they
save it as the project's default scene), and do not yet read
`config/export.ini`, build scripts ahead, or leave out the patterns above:
the Windows exporter is the complete one.
