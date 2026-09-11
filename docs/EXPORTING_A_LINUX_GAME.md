# Exporting a playable Linux game

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

## 2. Run the exporter

In the editor, open `Project > Export Linux Game...`, choose a destination
folder and leave the editor open while the background task is running. The
editor saves the current scene first and makes it the project's start scene.
On completion it shows the full path to the generated executable `.run` file.

The command-line form below performs the same operation and is useful for
automation or debugging.

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

## 3. What the exporter does

1. Reads `name` and `default_scene` from `project.schizo`.
2. Verifies that the saved start scene exists.
3. Configures and compiles the optimized `linux-release` runtime.
4. Copies the project while excluding editor caches, diagnostics and layout.
5. Adds engine runtime assets and language scripting SDK files.
6. Creates a small launcher that calls the runtime with:

```bash
worldshaper-runtime --game --project project/project.schizo
```

7. Produces a runnable directory, a `.tar.gz` archive and one self-extracting
   `.run` file.

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

## 4. Play and distribute it

```bash
cd "/path/to/MyGame/dist/linux"
./MyGame
```

Send `linux.tar.gz` to another Linux computer, extract it and run `./MyGame`.
The target computer still needs a Vulkan-capable driver and the standard Linux
desktop libraries used by GLFW. This is a portable folder build, not yet an
AppImage or Flatpak.

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

During play, `ESC` releases gameplay input without stopping the simulation.
Click the game window to capture input again. Close the OS window to quit.
