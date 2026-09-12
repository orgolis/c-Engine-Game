# Exporting a playable game

The editor exports the saved project with the production renderer in `--game`
mode. It loads `project.schizo`, opens its `default_scene`, starts play mode and
shows only the game viewport.

## Export from the editor

1. Open the project and save the current scene.
2. Choose `Project > Export Game...`.
3. Select `Linux`, `Windows`, or `Linux + Windows`.
4. Click `Choose Location...` and select a folder.
5. Keep the editor open until the single progress bar reaches 100%.

The selected folder receives only the distributable files:

- `MyGame-linux-x86_64.run` for Linux.
- `MyGame-windows-x86_64.zip` for Windows.
- Both files when `Linux + Windows` is selected.

On Linux, the first Windows export downloads a pinned portable llvm-mingw
compiler (about 84 MB) into `build/toolchains`. Later exports reuse it. On
Windows, the editor creates the Windows ZIP natively; create Linux packages
from the Linux editor.

The Windows ZIP contains `MyGame.exe`, its two compiler runtime DLLs, the saved
project and the required engine assets. A player extracts the complete ZIP and
starts `MyGame.exe`—the EXE must stay beside those files.

The Linux `.run` is already one self-extracting executable. After copying it to
another computer:

```bash
chmod +x MyGame-linux-x86_64.run
./MyGame-linux-x86_64.run
```

Both targets require an x86-64 computer, a Vulkan-capable graphics driver and
the normal platform desktop libraries.

## Command-line equivalents

Linux file:

```bash
tools/export_game_linux.sh "/path/to/MyGame/project.schizo" \
  "/path/to/exports/MyGame-linux"
```

Windows ZIP from Linux:

```bash
tools/export_game_windows_cross.sh "/path/to/MyGame/project.schizo" \
  "/path/to/exports/MyGame-windows"
```

Windows ZIP from Windows PowerShell:

```powershell
.\tools\export_game_windows.ps1 `
  -ProjectManifest "C:\path\to\MyGame\project.schizo" `
  -OutputDirectory "C:\path\to\exports\MyGame-windows"
```

The output-directory argument tells the scripts where to place the final named
file. No playable folder remains. Existing `.run` or `.zip` files are never
overwritten.

## What happens internally

1. The exporter reads the project name and start scene from `project.schizo`.
2. It checks that the saved start scene exists.
3. It incrementally builds the optimized runtime for the selected platform.
4. It copies the project without caches, diagnostics, editor layout or Git data.
5. It adds the runtime assets and platform libraries.
6. It packs everything into the one final `.run` or `.zip` file.

The editor maps those numbered script steps—and compiler percentages inside a
step—onto one fixed-size progress bar. For `Linux + Windows`, each exporter gets
half of that same bar, so progress never appears twice or grows the window.
