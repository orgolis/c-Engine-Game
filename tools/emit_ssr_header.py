"""Emit ssr_spirv.h from the four compiled SSR shaders.

spv_to_header.py takes exactly one vert/frag pair; this header carries four
arrays, so it needs its own emitter. Format matches the existing header
byte-for-byte (8 words per line, `u` suffix on the size) so a regeneration
that changes nothing produces no diff -- which is what makes a real diff worth
reading.
"""
from pathlib import Path

HERE = Path(__file__).parent
OUT = HERE.parent / "engine" / "renderer" / "gpu" / "vulkan" / "ssr_spirv.h"

SHADERS = [
    ("ssr.comp.spv", "kSsrComputeSpv"),
    ("ssr_rt.comp.spv", "kSsrRtComputeSpv"),
    ("ssr_composite.vert.spv", "kSsrCompositeVertSpv"),
    ("ssr_composite.frag.spv", "kSsrCompositeFragSpv"),
]


def emit(spv: Path, var: str) -> str:
    data = spv.read_bytes()
    assert len(data) % 4 == 0, f"{spv}: SPIR-V must be 4-byte aligned"
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    lines = [f"static const uint32_t {var}[] = {{"]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in words[i:i + 8]) + ",")
    lines.append("};")
    lines.append(f"static const uint32_t {var}_size = {len(data)}u;")
    return "\n".join(lines)


blocks = []
for name, var in SHADERS:
    p = HERE / name
    assert p.exists(), f"missing {p} — run regen_ssr_spirv.sh, not this directly"
    blocks.append(emit(p, var))
    print(f"  {var}: {p.stat().st_size} bytes")

OUT.write_text("#pragma once\n#include <cstdint>\n\n" + "\n\n".join(blocks) + "\n",
               encoding="utf-8")
print(f"wrote {OUT}")
