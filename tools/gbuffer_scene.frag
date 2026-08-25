#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

// Per-frame values, shared by every draw in the pass. Set 0 was an empty
// layout until v0.7.11; the camera position cannot live in push constants
// because the vertex block is already at Vulkan's 128-byte floor.
layout(set = 0, binding = 0) uniform FrameUBO {
    vec4 cameraPos;   // xyz = world position, w unused
} frame;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    vec4  emissive_factor;
    // xy = UV scale, zw = UV offset. Appended, so a shader that does not
    // declare it still reads every field above at the same offset.
    vec4  uv_transform;
    vec4  detail;   // x=tiling multiplier, y=albedo strength, z=normal strength
    vec4  parallax; // x=depth scale, 0 = disabled
} mat;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D mrMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
layout(set = 1, binding = 6) uniform sampler2D detailAlbedoMap;
layout(set = 1, binding = 7) uniform sampler2D detailNormalMap;
layout(set = 1, binding = 8) uniform sampler2D heightMap;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outAlbedoMetallic;
layout(location = 3) out vec4 outMaterial;

// Parallax occlusion mapping.
//
// A normal map fakes the LIGHTING of surface relief but not its SHAPE: the
// silhouette stays flat and nothing occludes anything. POM walks the view ray
// through the height field and returns the UV where it first goes under the
// surface, so bricks actually hide their mortar when seen at an angle.
//
// Steep-parallax march plus one interpolation step. The fixed-step march alone
// stair-steps visibly at grazing angles, and the single refinement between the
// last two samples removes almost all of it for one extra fetch.
//
// Step count scales with viewing angle: face-on needs few steps because the ray
// barely travels in UV, grazing needs many because it travels far. A fixed
// count is either wasteful head-on or stepped at the edges.
vec2 parallaxUV(vec2 uv, vec3 view_ts, float depth_scale) {
    const float kMinSteps = 8.0;
    const float kMaxSteps = 48.0;
    float steps = mix(kMaxSteps, kMinSteps, clamp(abs(view_ts.z), 0.0, 1.0));
    float step_depth = 1.0 / steps;

    // Total UV travel across the full depth range. view_ts.z is guarded: a ray
    // exactly parallel to the surface would divide by zero and throw the UV to
    // infinity, which reads as a smeared band across the object.
    vec2 max_offset = (view_ts.xy / max(abs(view_ts.z), 0.05)) * depth_scale;
    vec2 duv = max_offset * step_depth;

    // Height is stored as 1 = surface, 0 = deepest, so depth is its complement.
    vec2  cur_uv    = uv;
    float cur_depth = 1.0 - texture(heightMap, cur_uv).r;
    float ray_depth = 0.0;

    for (int i = 0; i < int(kMaxSteps); ++i) {
        if (ray_depth >= cur_depth) break;
        cur_uv    -= duv;
        cur_depth  = 1.0 - texture(heightMap, cur_uv).r;
        ray_depth += step_depth;
        if (float(i) >= steps) break;
    }

    // Interpolate between the last sample outside the surface and the first one
    // inside it, weighted by how far each missed the crossing.
    vec2  prev_uv = cur_uv + duv;
    float after   = cur_depth - ray_depth;
    float before  = (1.0 - texture(heightMap, prev_uv).r) - ray_depth + step_depth;
    float w       = after / max(after - before, 1e-5);
    return mix(cur_uv, prev_uv, clamp(w, 0.0, 1.0));
}

void main() {
    // One transform, applied once, used by every sampler below. Doing it per
    // sampler is how one map ends up untransformed and the surface looks
    // subtly wrong in a way nobody can point at.
    vec2 uv = inUV * mat.uv_transform.xy + mat.uv_transform.zw;

    // Parallax displaces the UV that EVERY later sampler uses, so it has to run
    // first. Depth 0 is an exact bypass rather than a zero-scale march: the
    // loop and its texture fetches are skipped entirely, so a material with no
    // height map pays nothing at all.
    if (mat.parallax.x > 0.0) {
        // The view ray in tangent space. Built here rather than reusing the TBN
        // below because that one is bent by the normal map, and displacing UVs
        // with a perturbed basis feeds the normal map back into itself.
        mat3 tbn_geom = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
        vec3 view_ws  = normalize(frame.cameraPos.xyz - inWorldPos);
        vec3 view_ts  = normalize(transpose(tbn_geom) * view_ws);
        uv = parallaxUV(uv, view_ts, mat.parallax.x);
    }

    // Detail maps: the same surface at a much higher frequency, blended over
    // the base so close-up detail exists without authoring the base at a
    // density the whole surface cannot afford.
    //
    // NO DISTANCE FADE. Detail is meant for near surfaces and ideally fades out
    // with range, but that needs a camera position, and the G-buffer vertex
    // push block is already exactly 128 bytes -- Vulkan's guaranteed minimum --
    // so there is nowhere to put one without a per-frame descriptor set the
    // pass does not have. Mip selection carries most of it; the fade is the
    // reason set 0 will eventually exist.
    vec2 duv = uv * mat.detail.x;
    // Detail fades out with range. A detail map is a high-frequency texture; at
    // distance it is minified past the point of adding anything and only
    // contributes aliasing that mips cannot fully remove. Fading it also
    // reclaims the sampling cost where it buys nothing.
    //
    // The range is deliberately fixed rather than authored: it is a property of
    // how far the eye can resolve that frequency, not of the material, and one
    // more slider here would be one more thing to set wrong.
    const float kDetailFadeStart = 12.0;
    const float kDetailFadeEnd   = 40.0;
    float view_dist  = distance(inWorldPos, frame.cameraPos.xyz);
    float detail_fade = 1.0 - smoothstep(kDetailFadeStart, kDetailFadeEnd, view_dist);

    vec4 base = texture(albedoMap, uv) * mat.base_color_factor;
    // A mid-grey detail texel (0.5) doubles to 1.0 and is therefore neutral,
    // which is the convention detail maps are authored to. Strength 0 makes the
    // mix an exact identity, so a material without detail maps is unchanged.
    base.rgb *= mix(vec3(1.0), texture(detailAlbedoMap, duv).rgb * 2.0, mat.detail.y * detail_fade);

    float alpha_cutoff = mat.emissive_factor.a;
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff) discard;

    vec3 nmap = texture(normalMap, uv).xyz * 2.0 - 1.0;
    // Detail normal, added in TANGENT space before the TBN transform. The xy of
    // a tangent-space normal is its slope, and summing slopes is the standard
    // cheap blend -- correct enough for the small perturbations a detail map
    // carries, and it costs one add rather than a second basis.
    nmap.xy += (texture(detailNormalMap, duv).xy * 2.0 - 1.0) * (mat.detail.z * detail_fade);
    nmap.xy *= mat.normal_scale;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * nmap);

    vec3 mr  = texture(mrMap, uv).rgb;
    float roughness = clamp(mr.g * mat.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr.b * mat.metallic_factor, 0.0, 1.0);

    float ao   = texture(aoMap, uv).r * mat.occlusion_strength;
    vec3  emis = mat.emissive_factor.rgb + texture(emissiveMap, uv).rgb;

    outPosition         = vec4(inWorldPos, 1.0);
    outNormalRoughness  = vec4(N, roughness);
    outAlbedoMetallic   = vec4(base.rgb, metallic);
    outMaterial         = vec4(emis, ao);
}
