"""Emit the single-array lighting fragment headers: the non-ray-traced variant
(lighting_pass_nort_spirv.h) and the ray-query variant WITHOUT the warp vote
(lighting_pass_rt_novote_spirv.h).

The default RT header is a vert/frag pair and spv_to_header.py handles it.
These are single arrays, which that script cannot express -- so
regen_lighting_spirv.sh used to end by telling the reader to regenerate it "via
the inline emitter in the fix commit". That is a regeneration step nobody can
repeat, which is how the variants drift apart, and the whole reason the header
at the top of the file insists they be rebuilt together.
"""
from pathlib import Path

HERE = Path(__file__).parent
VULKAN = HERE.parent / "engine" / "renderer" / "gpu" / "vulkan"

NORT_PREAMBLE = """// Auto-generated: the NON-RAY-TRACED lighting fragment shader.
//
// Same source as lighting_pass.frag, compiled WITHOUT -DGWS_RAY_QUERY, so the
// SPIR-V carries no RayQueryKHR capability and no SPV_KHR_ray_query extension.
//
// This exists because v0.6.2 crashed on an AMD RX 5700. The lighting pipeline
// was built from ray-query SPIR-V unconditionally; on a GPU without
// VK_KHR_ray_query that is invalid usage, and the driver crashed inside
// vkCreateGraphicsPipelines rather than returning an error. RDNA 1, GTX
// 10-series and most integrated GPUs are all in that category -- this was not
// an exotic configuration.
//
// Regenerate ALL variants together with tools/regen_lighting_spirv.sh.
#pragma once
#include <cstdint>

"""

RT_NOVOTE_PREAMBLE = """// Auto-generated: the ray-query lighting fragment shader WITHOUT the warp vote.
//
// Same source as lighting_pass.frag, compiled with -DGWS_RAY_QUERY but WITHOUT
// -DGWS_SUBGROUP_VOTE, so the SPIR-V declares no GroupNonUniformVote/Ballot
// capability. Subgroup operations are only guaranteed in compute shaders; a
// ray-tracing GPU whose fragment stage lacks them gets this variant rather than
// invalid SPIR-V (the same reason the non-RT variant exists -- see
// lighting_pass_nort_spirv.h). The shadows are the same estimator; only the
// warp-wide early-out is missing, so it costs more, never looks different.
//
// Regenerate ALL variants together with tools/regen_lighting_spirv.sh.
#pragma once
#include <cstdint>

"""

VARIANTS = [
    (HERE / "lighting_pass_nort.frag.spv", VULKAN / "lighting_pass_nort_spirv.h",
     "kLightingPassFragNoRtSpv", NORT_PREAMBLE),
    (HERE / "lighting_pass_rt_novote.frag.spv", VULKAN / "lighting_pass_rt_novote_spirv.h",
     "kLightingPassFragRtNoVoteSpv", RT_NOVOTE_PREAMBLE),
]

for spv, out, symbol, preamble in VARIANTS:
    assert spv.exists(), f"missing {spv} — run regen_lighting_spirv.sh, not this directly"
    data = spv.read_bytes()
    assert len(data) % 4 == 0, "SPIR-V must be 4-byte aligned"
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    lines = [f"static const uint32_t {symbol}[] = {{"]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in words[i:i + 8]) + ",")
    lines.append("};")
    lines.append(f"static const uint32_t {symbol}_size = {len(data)};")
    out.write_text(preamble + "\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out}: {len(data)} bytes")
