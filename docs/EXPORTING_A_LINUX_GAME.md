# Exporting a playable game

The repository's historical `game` target is only a window test. A real export
uses the production editor renderer in `--game` mode: it loads a project
manifest, loads the manifest's default scene, starts playback immediately and
draws only the game viewport.

## 1. Save before exporting

The exporter reads files from disk; it cannot see unsaved objects that exist
only in editor memory. Press `Ctrl+S` in the editor first.

The manifest chooses the scene that becomes the game entry point:

```text
name = MyGame
default_scene = scenes/main.scene
```

That scene needs an entity named `Player` and a playable camera, because the
current `ScenePlaybackManager` requires both when it starts.

## 2. Export from the editor (Linux and Windows)

Open `Project > Export Game...` and choose a destination folder. This is the
same workflow on Linux and Windows; there is no platform selector to configure.
The editor automatically uses the exporter for the operating system on which
it is running.

The editor saves the current scene first and makes it the project's start
scene. A fixed-size progress window then shows both the current build step and
the real compiler percentage. Keep the editor open until it finishes.

The result depends only on the current operating system:

- Linux creates `MyGame-linux-x86_64.run`, a single executable file.
- Windows creates `MyGame-windows-x86_64.zip` and a playable
  `MyGame-windows/MyGame.exe` folder. Extract the ZIP before sharing or playing.

This is a native export, not cross-compilation: use the editor on Linux for a
Linux game and the editor on Windows for a Windows game. The menu and the steps
are identical on both systems.

## 3. Command-line export (optional)

The command-line forms perform the same operation and are useful for automation
or debugging. Normal users can stay entirely in the editor UI.

### Linux

From the engine repository:

```bash
tools/export_game_linux.sh "/path/to/MyGame/project.schizo"
```

An optional second argument chooses the output directory:

```bash
tools/export_game_linux.sh \
  "/path/to/MyGame/project.schizo" \
  "/path/to/output/MyGame-linux"
```

The script deliberately refuses to overwrite an existing export. This makes a
mistyped path recoverable instead of deleting an older build.

### Windows

From PowerShell in the engine repository:

```powershell
.\tools\export_game_windows.ps1 `
  -ProjectManifest "C:\path\to\MyGame\project.schizo" `
  -OutputDirectory "C:\path\to\exports\MyGame-windows"
```

The Windows exporter also refuses to overwrite an existing folder or ZIP.

## 4. What the exporter does

1. Reads `name` and `default_scene` from `project.schizo`.
2. Verifies that the saved start scene exists.
3. Configures and compiles the optimized release runtime for the current OS.
4. Copies the project while excluding editor caches, diagnostics and layout.
5. Adds engine runtime assets and language scripting SDK files.
6. Creates the platform entry point. On Linux, a small launcher calls the
   runtime with:

```bash
worldshaper-runtime --game --project project/project.schizo
```

   On Windows, `game-export.marker` next to `MyGame.exe` makes the runtime find
   the packaged project automatically when the player double-clicks the EXE.
7. Produces the native distributable files described above.

The resulting layout is intentionally simple:

```text
linux/
├── MyGame
├── README.txt
├── bin/
│   └── worldshaper-runtime
├── assets/
│   ├── scripts/
│   └── skies/
└── project/
    ├── project.schizo
    ├── scenes/
    ├── assets/
    └── cooked/
```

`MyGame` is the launcher a player opens. It resolves its own directory, so the
game still works after the entire export folder is moved elsewhere.

## 5. Play and distribute it

```bash
cd "/path/to/MyGame/dist/linux"
./MyGame
```

The target computer still needs a Vulkan-capable driver and the standard
desktop libraries used by the engine.

For a literal single-file export, send `MyGame-linux-x86_64.run`. Make it
executable once after downloading, then start it directly:

```bash
chmod +x MyGame-linux-x86_64.run
./MyGame-linux-x86_64.run
```

The `.run` file contains the same compressed game package after a short shell
launcher. At startup it extracts into a temporary directory, launches `MyGame`
and removes the temporary files when the game closes. It is one distributable
file, but still relies on a compatible x86-64 Linux system and Vulkan driver.

On Windows, extract `MyGame-windows-x86_64.zip` and double-click `MyGame.exe`.
Keep the other extracted folders next to the EXE; they contain the scene,
project data and runtime assets.

During play, `ESC` releases gameplay input without stopping the simulation.
Click the game window to capture input again. Close the OS window to quit.
