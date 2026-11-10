"""Resolve alloc_live.cpp output: who owns the live heap, and who allocates the most during startup.
usage: resolve_live.py <live.txt> [top N] [live|total|count]"""
import os, subprocess, sys, bisect, re, collections

EXE = os.path.join(os.environ.get("GWS_BUILD_DIR") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "windows-release")), "bin", "editor_alloc_live.exe")
path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
mode = sys.argv[3] if len(sys.argv) > 3 else "live"

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
    n = n.replace("std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string")
    n = re.sub(r", std::allocator<[^<>]*(<[^<>]*>)?[^<>]*> ?", "", n)
    return n

lines = open(path).read().splitlines()
print(lines[1])
sites = []
for l in lines[2:]:
    p = l.split()
    sites.append(dict(live=int(p[1]), nlive=int(p[2]), total=int(p[3]), n=int(p[4]), pcs=[int(x, 16) for x in p[5:]]))

SKIP = ("__wrap_", "operator new", "malloc", "on_alloc", "std::", "__gnu_cxx", "void std::", "ImVector", "__DTOR", ".l_start", "__tmainCRT", "decltype", "auto std::")
def owner(pcs, depth=1):
    """first `depth` frames that are not allocator/STL plumbing"""
    out = []
    for pc in pcs:
        s = sym(pc)
        if any(s.startswith(k) for k in SKIP):
            continue
        out.append(s)
        if len(out) >= depth:
            break
    return out or ["(stl/runtime only)"]

MB = 1048576.0
key = {"live": "live", "total": "total", "count": "n"}[mode]
# aggregate by owning function (first non-plumbing frame)
agg = collections.defaultdict(lambda: [0, 0, 0, 0, None])
for s in sites:
    o = owner(s["pcs"], 1)[0]
    a = agg[o]
    a[0] += s["live"]; a[1] += s["nlive"]; a[2] += s["total"]; a[3] += s["n"]
    if a[4] is None or s[key] > a[4][key]:
        a[4] = s
idx = {"live": 0, "total": 2, "count": 3}[mode]
tot_live = sum(s["live"] for s in sites)
tot_n = sum(s["n"] for s in sites)
print(f"live heap {tot_live/MB:.1f} MB in {sum(s['nlive'] for s in sites)} blocks; startup made {tot_n} allocations totalling {sum(s['total'] for s in sites)/MB:.0f} MB")
print(f"{'live MB':>9} {'live blocks':>11} {'alloc MB':>9} {'allocs':>9}  owner (first engine frame) <- caller")
for o, a in sorted(agg.items(), key=lambda kv: -kv[1][idx])[:top]:
    chain = owner(a[4]["pcs"], 3)
    print(f"{a[0]/MB:9.2f} {a[1]:11d} {a[2]/MB:9.1f} {a[3]:9d}  {chain[0][:92]}")
    for c in chain[1:]:
        print(f"{'':43}<- {c[:92]}")
