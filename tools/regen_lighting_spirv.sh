#!/bin/sh
# Regenerate ALL THREE lighting-pass fragment variants from the one source:
#   lighting_pass.frag.spv            ray query + warp vote (the default)
#   lighting_pass_rt_novote.frag.spv  ray query, no subgroup vote (RT GPUs whose
#                                     fragment stage lacks vote/ballot)
#   lighting_pass_nort.frag.spv       no ray query (no RT on the device)
#
# They MUST be regenerated together. If only one variant is rebuilt, machines
# that get another keep running an older shader and the variants drift apart
# silently -- which is worse than the crash this split fixed, because it only
# shows up as "the lighting looks different on that PC".
set -e
GV="${GLSLANG:-/c/VulkanSDK/1.4.357.0/Bin/glslangValidator.exe}"
cd "$(dirname "$0")"
"$GV" --target-env vulkan1.2 -DGWS_RAY_QUERY -DGWS_SUBGROUP_VOTE -S frag -o lighting_pass.frag.spv            lighting_pass.frag
"$GV" --target-env vulkan1.2 -DGWS_RAY_QUERY                     -S frag -o lighting_pass_rt_novote.frag.spv lighting_pass.frag
"$GV" --target-env vulkan1.2                                     -S frag -o lighting_pass_nort.frag.spv      lighting_pass.frag
python spv_to_header.py lighting_pass.vert.spv kLightingPassVertSpv \
                        lighting_pass.frag.spv kLightingPassFragSpv \
                        ../engine/renderer/gpu/vulkan/lighting_pass_spirv.h
python emit_lighting_nort_header.py
echo "all three lighting variants regenerated"
