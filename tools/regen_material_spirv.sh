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

# gbuffer_scene has NO .vert source in this repo -- only its compiled words,
# inside gbuffer_scene_spirv.h. 607c5d9 ("extract inline GLSL to files") missed
# it, so that vertex shader cannot be rebuilt from anything. Its FRAGMENT array
# is therefore replaced in place and the vertex array left untouched: rewriting
# the whole header would delete a shader nobody can regenerate.
"$GV" --target-env vulkan1.2 -S frag -o gbuffer_scene.frag.spv gbuffer_scene.frag
python replace_spv_array.py ../engine/renderer/gpu/vulkan/gbuffer_scene_spirv.h \
                            kGBufferSceneFragSpv gbuffer_scene.frag.spv

for s in transparent_pass shadow_caster; do
    "$GV" --target-env vulkan1.2 -S vert -o "$s.vert.spv" "$s.vert"
    "$GV" --target-env vulkan1.2 -S frag -o "$s.frag.spv" "$s.frag"
done

python spv_to_header.py transparent_pass.vert.spv kTransparentVertSpv \
                        transparent_pass.frag.spv kTransparentFragSpv \
                        ../engine/renderer/gpu/vulkan/transparent_pass_spirv.h
python spv_to_header.py shadow_caster.vert.spv    kShadowCasterVertSpv \
                        shadow_caster.frag.spv    kShadowCasterFragSpv \
                        ../engine/renderer/gpu/vulkan/shadow_caster_spirv.h

echo "material-block shaders regenerated (.spv refreshed alongside)"
