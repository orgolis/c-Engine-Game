"""Replace ONE SPIR-V array inside an existing generated header, in place.

Needed because `gbuffer_scene.vert` has no source in the repo -- only its
compiled words survive, inside gbuffer_scene_spirv.h. spv_to_header.py rewrites
a whole header from a vert/frag pair, which would require a vert source that
does not exist, so regenerating the fragment shader any other way would delete
the vertex shader outright.

Usage: replace_spv_array.py <header.h> <symbol> <new.spv>
"""
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    header, symbol, spv = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])

    data = spv.read_bytes()
    assert len(data) % 4 == 0, f"{spv}: SPIR-V must be 4-byte aligned"
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]

    lines = [f"static const uint32_t {symbol}[] = {{"]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in words[i:i + 8]) + ",")
    lines.append("};")
    block = "\n".join(lines)

    text = header.read_text(encoding="utf-8")

    # The array, then whatever size line follows it (with or without a `u`).
    pattern = re.compile(
        r"static const uint32_t " + re.escape(symbol) + r"\[\] = \{.*?\n\};\n"
        r"static const uint32_t " + re.escape(symbol) + r"_size = \d+u?;",
        re.S,
    )
    if not pattern.search(text):
        print(f"error: {symbol} not found in {header}")
        return 1

    # Preserve the existing size line's suffix style so the diff stays minimal.
    had_u = re.search(re.escape(symbol) + r"_size = \d+u;", text) is not None
    size_line = f"static const uint32_t {symbol}_size = {len(data)}{'u' if had_u else ''};"

    header.write_text(pattern.sub(block + "\n" + size_line, text, count=1), encoding="utf-8")
    print(f"  {symbol}: {len(data)} bytes -> {header.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
