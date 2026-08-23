#!/bin/sh
# Regenerate ALL FOUR shaders embedded in ssr_spirv.h from source.
#
# All four together, for the same reason regen_lighting_spirv.sh insists on it:
# the header is one file, and emitting it from a partial set silently reverts
# whichever shader was not recompiled. A reflection change that lands in the
# ray-traced variant but not the screen-space one shows up only as "reflections
# look different when I tick the box", which is the hardest kind of bug to
# attribute.
#
# ssr_rt.comp needs vulkan1.2 for ray query + int64 buffer references.
set -e
GV="${GLSLANG:-/c/VulkanSDK/1.4.357.0/Bin/glslangValidator.exe}"
cd "$(dirname "$0")"

"$GV" --target-env vulkan1.2 -S comp -o ssr.comp.spv            ssr.comp
"$GV" --target-env vulkan1.2 -S comp -o ssr_rt.comp.spv         ssr_rt.comp
"$GV" --target-env vulkan1.2 -S vert -o ssr_composite.vert.spv  ssr_composite.vert
"$GV" --target-env vulkan1.2 -S frag -o ssr_composite.frag.spv  ssr_composite.frag

python emit_ssr_header.py

# The .spv files are LEFT IN PLACE, not cleaned up: 48 of them are tracked in
# git, and deleting three as a side effect of regenerating would leave the set
# inconsistent. Refreshing them keeps each checked-in .spv matching its source
# -- ssr_rt.comp.spv had silently drifted from ssr_rt.comp for exactly this
# reason and had to be removed instead.
echo "ssr_spirv.h regenerated from all four sources (.spv refreshed alongside)"
