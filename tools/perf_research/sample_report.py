"""Turn stack_sampler output into "share of wall time under each function".

usage: sample_report.py <samples.txt> [top N] [exe path]

"inclusive" counts a sample for every engine function on its stack, so it reads
as "this much of the main thread's wall time was spent in or below it".
"self" counts only the innermost frame. Time blocked in the driver or the OS
shows up under the module it was blocked in.
"""
import bisect, collections, os, re, subprocess, sys

path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 30
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get("GWS_BUILD_DIR") or os.path.normpath(os.path.join(HERE, "..", "..", "build", "windows-release"))
exe = sys.argv[3] if len(sys.argv) > 3 else os.path.join(BUILD, "bin", "editor.exe")

mods, samples = [], []
for ln in open(path, errors="replace"):
    p = ln.split(None, 3)
    if not p:
        continue
    if p[0] == "module":
        mods.append((int(p[1], 16), int(p[2], 16), p[3].strip()))
    elif p[0] == "s":
        samples.append([int(x, 16) for x in ln.split()[1:]])
mods.sort()
bases = [m[0] for m in mods]

def module_of(addr):
    i = bisect.bisect_right(bases, addr) - 1
    if i >= 0 and addr < mods[i][0] + mods[i][1]:
        return mods[i]
    return None

exe_name = os.path.basename(exe).lower()
exe_mod = next((m for m in mods if os.path.basename(m[2]).lower() == exe_name), None)

nm = subprocess.run(["nm", "-C", "--defined-only", "-n", exe], capture_output=True, text=True, errors="replace").stdout
hdr = subprocess.run(["objdump", "-p", exe], capture_output=True, text=True).stdout
image_base = int(re.search(r"ImageBase\s+([0-9a-fA-F]+)", hdr).group(1), 16)
addrs, names = [], []
for ln in nm.splitlines():
    q = ln.split(" ", 2)
    if len(q) < 3 or q[1] not in "tTwW":
        continue
    try:
        addrs.append(int(q[0], 16) - image_base)
    except ValueError:
        continue
    names.append(q[2])

def short(n):
    n = n.replace("std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string")
    n = re.sub(r"\(.*", "", n)
    return n[:86]

def name_of(addr):
    m = module_of(addr)
    if m is None:
        return "?"
    if exe_mod is not None and m[0] == exe_mod[0]:
        i = bisect.bisect_right(addrs, addr - m[0]) - 1
        return short(names[i]) if i >= 0 else "?"
    return "[" + os.path.basename(m[2]) + "]"

PLUMBING = ("std::", "__gnu_cxx", "operator ", "void std::", "glm::", "auto std::", "decltype", "__tmainCRTStartup",
            ".l_start", "__DTOR_LIST__", "mainCRTStartup", "?", "bool std::", "unsigned long long std::")

self_c = collections.Counter()
incl_c = collections.Counter()
for st in samples:
    resolved = [name_of(a) for a in st]
    self_c[resolved[0]] += 1
    seen = set()
    for r in resolved:
        if r.startswith("[") or r.startswith(PLUMBING):
            continue
        if r not in seen:
            seen.add(r)
            incl_c[r] += 1

n = len(samples)
print(f"{n} samples")
print("\n== inclusive: share of the thread's wall time in or below each engine function")
for k, v in incl_c.most_common(top):
    print(f"  {100.0 * v / n:5.1f}%  {k}")
print("\n== self: innermost frame")
for k, v in self_c.most_common(top):
    print(f"  {100.0 * v / n:5.1f}%  {k}")
