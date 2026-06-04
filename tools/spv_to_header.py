"""Convert a pair of .spv files into a C++ header with two uint32_t arrays."""
import sys
from pathlib import Path

def emit(spv_path: Path, var_name: str) -> tuple[str, int]:
    data = spv_path.read_bytes()
    assert len(data) % 4 == 0, f"{spv_path}: SPIR-V must be 4-byte aligned"
    words = [int.from_bytes(data[i:i+4], "little") for i in range(0, len(data), 4)]
    lines = [f"static const uint32_t {var_name}[] = {{"]
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in chunk) + ",")
    lines.append("};")
    lines.append(f"static const uint32_t {var_name}_size = {len(data)};")
    return "\n".join(lines), len(data)

def main():
    if len(sys.argv) != 6:
        print("Usage: spv_to_header.py <vert.spv> <vert_var> <frag.spv> <frag_var> <out.h>")
        sys.exit(1)
    vert_spv = Path(sys.argv[1])
    vert_var = sys.argv[2]
    frag_spv = Path(sys.argv[3])
    frag_var = sys.argv[4]
    out_path = Path(sys.argv[5])

    vert_block, vert_bytes = emit(vert_spv, vert_var)
    frag_block, frag_bytes = emit(frag_spv, frag_var)

    header = (
        f"// Auto-generated SPIR-V fallback for {vert_spv.name} and {frag_spv.name}.\n"
        f"// Compiled via glslangValidator --target-env vulkan1.2.\n"
        f"// Regenerate with c-Engine-Game/tools/spv_to_header.py.\n"
        f"#pragma once\n"
        f"#include <cstdint>\n\n"
        f"{vert_block}\n\n"
        f"{frag_block}\n"
    )
    out_path.write_text(header, encoding="utf-8")
    print(f"Wrote {out_path}: {vert_bytes}B vert + {frag_bytes}B frag")

if __name__ == "__main__":
    main()
