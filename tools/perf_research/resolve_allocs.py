"""Resolve the allocation sites recorded by alloc_count.cpp to function names.
usage: resolve.py <alloc_out.txt> [top N]"""
import os, subprocess, sys, bisect, re

EXE = os.path.join(os.environ.get("GWS_BUILD_DIR") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "windows-release")), "bin", "editor_alloc_count.exe")
path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 30

hdr = subprocess.run(["objdump", "-p", EXE], capture_output=True, text=True).stdout
image_base = int(re.search(r"ImageBase\s+([0-9a-fA-F]+)", hdr).group(1), 16)

nm = subprocess.run(["nm", "-C", "--defined-only", "-n", EXE], capture_output=True, text=True, errors="replace").stdout
addrs, names = [], []
for ln in nm.splitlines():
    p = ln.split(" ", 2)
    if len(p) < 3 or p[1] not in "tTwW":
        continue
    try:
        a = int(p[0], 16)
    except ValueError:
        continue
    addrs.append(a - image_base)
    names.append(p[2])

def sym(rva):
    i = bisect.bisect_right(addrs, rva) - 1
    if i < 0:
        return "?"
    n = names[i]
    n = re.sub(r"std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string", n)
    return n[:110]

lines = open(path).read().splitlines()
frames = [l.split() for l in lines if l.startswith("frame ")]
nframes = len(frames)
sites = []
for l in lines:
    if l.startswith("site "):
        p = l.split()
        sites.append((int(p[1]), int(p[2]), [int(x, 16) for x in p[3:]]))
sites.sort(reverse=True)
total = sum(s[0] for s in sites)
print(f"{total} allocations in {nframes} frames = {total / nframes:.1f} per frame, {len(sites)} distinct stacks")
SKIP = ("__wrap_", "operator new", "malloc", "note(")
for n, b, pcs in sites[:top]:
    chain = [sym(pc) for pc in pcs]
    chain = [c for c in chain if not any(c.startswith(s) or s in c[:24] for s in SKIP)]
    print(f"\n{n / nframes:7.2f}/frame  {b / max(n,1):8.0f} B avg")
    for c in chain[:6]:
        print("      ", c)
