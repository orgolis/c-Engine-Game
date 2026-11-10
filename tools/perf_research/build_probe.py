"""Compile a probe TU with the exact flags/includes of an engine TU.

usage: build_probe.py <probe.cpp> <out.o> [like=editor/src/main.cpp] [extra flags...]
Runs the compiler directly (no shell), from the build directory.
"""
import json, re, subprocess, sys, os

BUILD = os.environ.get("GWS_BUILD_DIR") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "windows-release"))
src, out = sys.argv[1], sys.argv[2]
like = "editor/src/main.cpp"
extra = []
for a in sys.argv[3:]:
    if a.startswith("like="):
        like = a[5:]
    else:
        extra.append(a)

d = json.load(open(os.path.join(BUILD, "compile_commands.json")))
entry = next(e for e in d if e["file"].replace("\\", "/").endswith(like))
cmd = entry.get("command") or " ".join(entry.get("arguments", []))

# Windows-style split: whitespace separates, double quotes group (and may sit
# in the middle of a token, as in -I"C:\a b").
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
for i, t in enumerate(toks):
    if skip:
        skip -= 1
        continue
    if t in ("-o", "-c", "-MT", "-MF"):
        skip = 1
        continue
    if t == "-MD":
        continue
    args.append(t)

args += extra + ["-c", src, "-o", out]
r = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
sys.exit(r.returncode)
