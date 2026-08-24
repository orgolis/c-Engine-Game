"""Emit lighting_pass_nort_spirv.h from the non-ray-query fragment SPIR-V.

The RT header is a vert/frag pair and spv_to_header.py handles it. This one is
a single array, which that script cannot express -- so regen_lighting_spirv.sh
used to end by telling the reader to regenerate it "via the inline emitter in
the fix commit". That is a regeneration step nobody can repeat, which is how
the two variants drift apart, and the whole reason the header at the top of the
file insists they be rebuilt together.
"""
from pathlib import Path

HERE = Path(__file__).parent
OUT = HERE.parent / "engine" / "renderer" / "gpu" / "vulkan" / "lighting_pass_nort_spirv.h"
SPV = HERE / "lighting_pass_nort.frag.spv"

PREAMBLE = """// Auto-generated: the NON-RAY-TRACED lighting fragment shader.
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
// Regenerate BOTH variants together with tools/regen_lighting_spirv.sh.
#pragma once
#include <cstdint>

"""

assert SPV.exists(), f"missing {SPV} — run regen_lighting_spirv.sh, not this directly"
data = SPV.read_bytes()
assert len(data) % 4 == 0, "SPIR-V must be 4-byte aligned"
words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]

lines = ["static const uint32_t kLightingPassFragNoRtSpv[] = {"]
for i in range(0, len(words), 8):
    lines.append("    " + ", ".join(f"0x{w:08X}u" for w in words[i:i + 8]) + ",")
lines.append("};")
lines.append(f"static const uint32_t kLightingPassFragNoRtSpv_size = {len(data)};")

OUT.write_text(PREAMBLE + "\n".join(lines) + "\n", encoding="utf-8")
print(f"wrote {OUT}: {len(data)} bytes")
