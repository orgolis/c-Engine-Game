"""Relink the editor's existing object files with an allocation probe wrapped in.

usage: link_instrumented.py alloc_count|alloc_live

Produces <build>/bin/editor_<probe>.exe next to editor.exe. Engine sources and
editor.exe are untouched: the probe is one extra object on the link line, and
the linker's --wrap redirects malloc/calloc/realloc/free and glfwPollEvents
(the once-per-frame marker) to it. libstdc++ is linked statically so that the
allocations made inside the C++ runtime are counted as well.

The build directory defaults to <repo>/build/windows-release; set GWS_BUILD_DIR
to use another. Build the editor there first.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get("GWS_BUILD_DIR") or os.path.normpath(os.path.join(HERE, "..", "..", "build", "windows-release"))
probe = sys.argv[1] if len(sys.argv) > 1 else "alloc_count"
if probe not in ("alloc_count", "alloc_live"):
    sys.exit("probe must be alloc_count or alloc_live")

cmds = subprocess.run(["ninja", "-t", "commands", "editor"], cwd=BUILD, capture_output=True, text=True).stdout
link = [l for l in cmds.splitlines() if " -o bin\\editor.exe " in l][-1]
# keep only the c++ invocation (the line also carries post-build copy steps)
cmd = re.search(r"cd \. && (.*?-ladvapi32)", link).group(1)

# Windows-style split: whitespace separates, double quotes group.
toks, cur, inq = [], "", False
for ch in cmd:
    if ch == '"':
        inq = not inq
    elif ch.isspace() and not inq:
        if cur:
            toks.append(cur)
            cur = ""
    else:
        cur += ch
if cur:
    toks.append(cur)

args, skip = [], 0
for t in toks:
    if skip:
        skip -= 1
        continue
    if t == "-o":
        skip = 1
        continue
    if t.startswith("-Wl,--out-implib"):
        continue
    args.append(t)

obj = os.path.join(BUILD, probe + ".o")
r = subprocess.run(["c++", "-O2", "-c", os.path.join(HERE, probe + ".cpp"), "-o", obj], capture_output=True, text=True)
if r.returncode:
    sys.exit(r.stderr)

args += [obj, "-o", "bin\\editor_" + probe + ".exe", "-static-libstdc++", "-static-libgcc",
         "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=glfwPollEvents"]
r = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
sys.stdout.write(r.stdout[-3000:])
sys.stderr.write(r.stderr[-3000:])
sys.exit(r.returncode)
