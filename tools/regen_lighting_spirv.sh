#!/bin/sh
# Regenerate BOTH lighting-pass fragment variants from the one source.
#
# They MUST be regenerated together. If only the ray-query variant is rebuilt,
# machines without ray tracing keep running an older shader and the two drift
# apart silently -- which is worse than the crash this split fixed, because it
# only shows up as "the lighting looks different on that PC".
set -e
GV="${GLSLANG:-/c/VulkanSDK/1.4.341.1/Bin/glslangValidator}"
cd "$(dirname "$0")"
"$GV" --target-env vulkan1.2 -DGWS_RAY_QUERY -S frag -o lighting_pass.frag.spv      lighting_pass.frag
"$GV" --target-env vulkan1.2                 -S frag -o lighting_pass_nort.frag.spv lighting_pass.frag
python spv_to_header.py lighting_pass.vert.spv kLightingPassVertSpv \
                        lighting_pass.frag.spv kLightingPassFragSpv \
                        ../engine/renderer/gpu/vulkan/lighting_pass_spirv.h
echo "regenerate the non-RT header via the inline emitter in the fix commit"
