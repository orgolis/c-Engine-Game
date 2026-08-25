#!/bin/sh
# Regenerate the three shaders that share the MaterialUBO block.
#
# TOGETHER, always. They read one uniform block backed by one C++ struct
# (MaterialUniforms), and a shader left on an older declaration reads every
# field after the drift point at the wrong offset -- which does not error, it
# shades wrongly. The G-buffer would apply a UV transform the shadow pass did
# not, and a cutout's shadow would be cut in a different shape than the object.
set -e
GV="${GLSLANG:-/c/VulkanSDK/1.4.357.0/Bin/glslangValidator.exe}"
cd "$(dirname "$0")"

# gbuffer_scene.vert was MISSING from the repo until 2026-08-25 -- 607c5d9
# ("extract inline GLSL to files") left it behind, so only its compiled words
# survived inside gbuffer_scene_spirv.h and the shader could not be rebuilt at
# all. It was recovered by disassembling that array with spirv-cross; the
# restored source compiles to an identical interface -- same locations, same
# entry point, same push-constant offsets, checked against the shipped SPIR-V.
#
# So all three are ordinary vert/frag pairs again, and replace_spv_array.py is
# not needed here any more. That script stays in the tree because the same
# rescue would work for any other header that loses its source.
for s in gbuffer_scene transparent_pass shadow_caster; do
    "$GV" --target-env vulkan1.2 -S vert -o "$s.vert.spv" "$s.vert"
    "$GV" --target-env vulkan1.2 -S frag -o "$s.frag.spv" "$s.frag"
done

python spv_to_header.py gbuffer_scene.vert.spv    kGBufferSceneVertSpv \
                        gbuffer_scene.frag.spv    kGBufferSceneFragSpv \
                        ../engine/renderer/gpu/vulkan/gbuffer_scene_spirv.h

python spv_to_header.py transparent_pass.vert.spv kTransparentVertSpv \
                        transparent_pass.frag.spv kTransparentFragSpv \
                        ../engine/renderer/gpu/vulkan/transparent_pass_spirv.h

python spv_to_header.py shadow_caster.vert.spv    kShadowCasterVertSpv \
                        shadow_caster.frag.spv    kShadowCasterFragSpv \
                        ../engine/renderer/gpu/vulkan/shadow_caster_spirv.h

echo "material-block shaders regenerated (.spv refreshed alongside)"
