#!/usr/bin/env bash
set -euo pipefail

# Export one GameWorldshaper project as a directly launchable Linux folder.
# Usage: tools/export_game_linux.sh /path/to/project.schizo [output-directory]

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 /path/to/project.schizo [output-directory]" >&2
    exit 2
fi

gws_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
gws_repo_root="$(cd -- "$gws_script_dir/.." && pwd)"
gws_manifest="$(realpath -- "$1")"
gws_project_dir="$(dirname -- "$gws_manifest")"

if [[ ! -f "$gws_manifest" ]]; then
    echo "Project manifest not found: $gws_manifest" >&2
    exit 2
fi

gws_project_name="$(awk -F= '
    /^[[:space:]]*name[[:space:]]*=/ {
        value=$0; sub(/^[^=]*=[[:space:]]*/, "", value); print value; exit
    }' "$gws_manifest")"
gws_default_scene="$(awk -F= '
    /^[[:space:]]*default_scene[[:space:]]*=/ {
        value=$0; sub(/^[^=]*=[[:space:]]*/, "", value); print value; exit
    }' "$gws_manifest")"

if [[ -z "$gws_project_name" || -z "$gws_default_scene" ]]; then
    echo "project.schizo must contain name and default_scene" >&2
    exit 2
fi
if [[ ! -f "$gws_project_dir/$gws_default_scene" ]]; then
    echo "Default scene not found: $gws_project_dir/$gws_default_scene" >&2
    echo "Save the scene in the editor before exporting." >&2
    exit 2
fi

gws_game_name="$(printf '%s' "$gws_project_name" | tr -cs 'A-Za-z0-9._-' '_')"
gws_output="${2:-$gws_project_dir/dist/linux}"
gws_output="$(realpath -m -- "$gws_output")"
if [[ -e "$gws_output" ]]; then
    echo "Output already exists: $gws_output" >&2
    echo "Choose a new output directory or remove the old export intentionally." >&2
    exit 2
fi

echo "[1/5] Configuring the optimized Linux build"
cmake --preset linux-release -S "$gws_repo_root"

echo "[2/5] Building the runtime"
cmake --build --preset linux-release --target editor --parallel

gws_runtime="$gws_repo_root/build/linux-release/bin/editor"
if [[ ! -x "$gws_runtime" ]]; then
    echo "Runtime build missing: $gws_runtime" >&2
    exit 1
fi

gws_stage="$(mktemp -d /tmp/gameworldshaper-export.XXXXXX)"
trap 'rm -rf -- "$gws_stage"' EXIT
mkdir -p "$gws_stage/bin" "$gws_stage/project" "$gws_stage/assets"

echo "[3/5] Copying the saved project and runtime content"
tar -C "$gws_project_dir" \
    --exclude='./dist' \
    --exclude='./cache' \
    --exclude='./diagnostics' \
    --exclude='./editor.ini' \
    --exclude='./editor_layout.version' \
    --exclude='./.git' \
    -cf - . | tar -C "$gws_stage/project" -xf -

install -m 755 "$gws_runtime" "$gws_stage/bin/worldshaper-runtime"
for gws_asset_group in skies scripts; do
    if [[ -d "$gws_repo_root/assets/$gws_asset_group" ]]; then
        cp -a "$gws_repo_root/assets/$gws_asset_group" "$gws_stage/assets/"
    fi
done

# Script SDK files are defaults, not project content. Merge them without
# replacing a project file with the same name.
mkdir -p "$gws_stage/project/assets/scripts"
if [[ -d "$gws_repo_root/assets/scripts" ]]; then
    cp -an "$gws_repo_root/assets/scripts/." "$gws_stage/project/assets/scripts/"
fi

if [[ -x "$gws_repo_root/build/linux-release/bin/glslangValidator" ]]; then
    install -m 755 "$gws_repo_root/build/linux-release/bin/glslangValidator" \
        "$gws_stage/bin/glslangValidator"
fi

echo "[4/5] Creating the game launcher"
printf '%s\n' \
    '#!/bin/sh' \
    'gws_game_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)' \
    'cd -- "$gws_game_dir"' \
    'exec "$gws_game_dir/bin/worldshaper-runtime" --game --project "$gws_game_dir/project/project.schizo" "$@"' \
    > "$gws_stage/$gws_game_name"
chmod 755 "$gws_stage/$gws_game_name"

printf '%s\n' \
    "$gws_project_name — Linux export" \
    "Start: ./$gws_game_name" \
    "Scene: $gws_default_scene" \
    "ESC releases input; click the game to resume. Close the window to quit." \
    > "$gws_stage/README.txt"

echo "[5/5] Publishing the export"
mkdir -p "$(dirname -- "$gws_output")"
mv -- "$gws_stage" "$gws_output"
trap - EXIT
tar -C "$(dirname -- "$gws_output")" -czf "$gws_output.tar.gz" \
    "$(basename -- "$gws_output")"

echo
echo "Export complete: $gws_output"
echo "Archive:         $gws_output.tar.gz"
echo "Start with:      $gws_output/$gws_game_name"
