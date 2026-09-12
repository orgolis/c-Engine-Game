#!/usr/bin/env bash
set -euo pipefail

# Build a Windows game on Linux and emit exactly one portable ZIP.
# Usage: tools/export_game_windows_cross.sh project.schizo output-directory

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 /path/to/project.schizo /path/to/output-directory" >&2
    exit 2
fi

gws_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
gws_repo_root="$(cd -- "$gws_script_dir/.." && pwd)"
gws_manifest="$(realpath -- "$1")"
gws_project_dir="$(dirname -- "$gws_manifest")"
gws_output_hint="$(realpath -m -- "$2")"
gws_output_parent="$(dirname -- "$gws_output_hint")"

if [[ ! -f "$gws_repo_root/CMakeLists.txt" ]]; then
    echo "Windows export from Linux requires the engine source checkout." >&2
    exit 2
fi

gws_project_name="$(awk -F= '/^[[:space:]]*name[[:space:]]*=/ { value=$0; sub(/^[^=]*=[[:space:]]*/, "", value); print value; exit }' "$gws_manifest")"
gws_default_scene="$(awk -F= '/^[[:space:]]*default_scene[[:space:]]*=/ { value=$0; sub(/^[^=]*=[[:space:]]*/, "", value); print value; exit }' "$gws_manifest")"
if [[ -z "$gws_project_name" || -z "$gws_default_scene" ]]; then
    echo "project.schizo must contain name and default_scene" >&2
    exit 2
fi
if [[ ! -f "$gws_project_dir/$gws_default_scene" ]]; then
    echo "Default scene not found: $gws_project_dir/$gws_default_scene" >&2
    exit 2
fi

gws_game_name="$(printf '%s' "$gws_project_name" | tr -cs 'A-Za-z0-9._-' '_')"
gws_archive="$gws_output_parent/${gws_game_name}-windows-x86_64.zip"
if [[ -e "$gws_archive" ]]; then
    echo "Export archive already exists: $gws_archive" >&2
    exit 2
fi

gws_toolchain_version="20260908"
gws_toolchain_name="llvm-mingw-${gws_toolchain_version}-ucrt-ubuntu-22.04-x86_64"
gws_toolchain_url="https://github.com/mstorsjo/llvm-mingw/releases/download/${gws_toolchain_version}/${gws_toolchain_name}.tar.xz"
gws_toolchain_sha="2258c745e3155870c80793f3e8c80b28fbde11b9ff73c4c78783635b3440b092"
gws_toolchain_cache="$gws_repo_root/build/toolchains"
gws_toolchain="$gws_toolchain_cache/$gws_toolchain_name"
gws_archive_cache="$gws_toolchain_cache/$gws_toolchain_name.tar.xz"

echo "[1/8] Preparing the portable Windows compiler"
mkdir -p "$gws_toolchain_cache"
if [[ ! -x "$gws_toolchain/bin/x86_64-w64-mingw32-clang++" ]]; then
    if [[ ! -f "$gws_archive_cache" ]]; then
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 -o "$gws_archive_cache.part" "$gws_toolchain_url"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$gws_archive_cache.part" "$gws_toolchain_url"
        else
            echo "curl or wget is required for the first Windows export." >&2
            exit 1
        fi
        mv -- "$gws_archive_cache.part" "$gws_archive_cache"
    fi
    printf '%s  %s\n' "$gws_toolchain_sha" "$gws_archive_cache" | sha256sum --check --status || {
        echo "The downloaded Windows compiler archive failed verification." >&2
        exit 1
    }
    tar -xJf "$gws_archive_cache" -C "$gws_toolchain_cache"
fi

echo "[2/8] Preparing Windows Vulkan headers and import library"
gws_deps="$gws_repo_root/build/windows-cross-deps"
mkdir -p "$gws_deps/include" "$gws_deps/lib"
if [[ ! -f "$gws_deps/include/vulkan/vulkan.h" ]]; then
    [[ -d /usr/include/vulkan ]] || { echo "Vulkan development headers are required." >&2; exit 1; }
    cp -a /usr/include/vulkan "$gws_deps/include/"
    [[ ! -d /usr/include/vk_video ]] || cp -a /usr/include/vk_video "$gws_deps/include/"
fi
if [[ ! -f "$gws_deps/lib/libvulkan-1.dll.a" ]]; then
    gws_vulkan_loader=""
    for gws_candidate in /usr/lib/libvulkan.so /usr/lib64/libvulkan.so; do
        [[ -f "$gws_candidate" ]] && gws_vulkan_loader="$gws_candidate" && break
    done
    [[ -n "$gws_vulkan_loader" ]] || { echo "The Linux Vulkan loader was not found." >&2; exit 1; }
    {
        echo "LIBRARY vulkan-1.dll"
        echo "EXPORTS"
        nm -D --defined-only "$gws_vulkan_loader" | awk '$3 ~ /^vk/ { print $3 }' | sort -u
    } > "$gws_deps/vulkan-1.def"
    "$gws_toolchain/bin/llvm-dlltool" -d "$gws_deps/vulkan-1.def" \
        -l "$gws_deps/lib/libvulkan-1.dll.a" -m i386:x86-64
fi

gws_build="$gws_repo_root/build/windows-cross-release"
echo "[3/8] Configuring the optimized Windows build"
cmake -S "$gws_repo_root" -B "$gws_build" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$gws_toolchain/bin/x86_64-w64-mingw32-clang" \
    -DCMAKE_CXX_COMPILER="$gws_toolchain/bin/x86_64-w64-mingw32-clang++" \
    -DCMAKE_RC_COMPILER="$gws_toolchain/bin/x86_64-w64-mingw32-windres" \
    -DVulkan_INCLUDE_DIR="$gws_deps/include" \
    -DVulkan_LIBRARY="$gws_deps/lib/libvulkan-1.dll.a" \
    -DGWS_ENABLE_GLSLANG_RUNTIME=OFF -DBUILD_TESTS=OFF

echo "[4/8] Building the Windows runtime"
cmake --build "$gws_build" --target editor --parallel 4
gws_runtime="$gws_build/bin/editor.exe"
[[ -f "$gws_runtime" ]] || { echo "Runtime build missing: $gws_runtime" >&2; exit 1; }

gws_stage="$(mktemp -d /tmp/gameworldshaper-windows-export.XXXXXX)"
trap 'rm -rf -- "$gws_stage"' EXIT
mkdir -p "$gws_stage/project" "$gws_stage/assets"

echo "[5/8] Copying the saved project"
tar -C "$gws_project_dir" --exclude='./dist' --exclude='./cache' \
    --exclude='./diagnostics' --exclude='./editor.ini' \
    --exclude='./editor_layout.version' --exclude='./.git' -cf - . | \
    tar -C "$gws_stage/project" -xf -

echo "[6/8] Adding the Windows runtime and engine assets"
install -m 755 "$gws_runtime" "$gws_stage/$gws_game_name.exe"
for gws_dll in libc++.dll libunwind.dll; do
    install -m 755 "$gws_build/bin/$gws_dll" "$gws_stage/$gws_dll"
done
for gws_asset_group in skies scripts; do
    [[ ! -d "$gws_repo_root/assets/$gws_asset_group" ]] || \
        cp -a "$gws_repo_root/assets/$gws_asset_group" "$gws_stage/assets/"
done
mkdir -p "$gws_stage/project/assets/scripts"
[[ ! -d "$gws_repo_root/assets/scripts" ]] || \
    cp -an "$gws_repo_root/assets/scripts/." "$gws_stage/project/assets/scripts/"
printf '%s\n' 'GameWorldshaper packaged game' > "$gws_stage/game-export.marker"
printf '%s\n' "$gws_project_name - Windows export" "Start: $gws_game_name.exe" \
    "Scene: $gws_default_scene" > "$gws_stage/README.txt"

echo "[7/8] Creating the portable Windows archive"
mkdir -p "$gws_output_parent"
(cd -- "$gws_stage" && cmake -E tar cf "$gws_archive" --format=zip .)

echo "[8/8] Windows export complete"
echo "Export complete: $gws_archive"
