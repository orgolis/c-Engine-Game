# Open crash — v0.8.2, SIGSEGV on a worker thread (2026-08-30)

**Status:** open, not reproduced here, **not** attributed. Recorded so it is not lost.
**Verdict on relatedness:** **not the performance work.** Reasoning below — stated as evidence rather than
conclusion, because the stack is unsymbolized and a confident wrong attribution costs more than an honest
"unknown".

---

## What happened

Two crashes, 58 seconds apart, both while testing v0.8.2.

| | |
|---|---|
| Reports | `crash_report_20260830_032850.txt`, `crash_report_20260830_032948.txt` |
| Minidumps | `crash_20260830_032850.dmp`, `crash_20260830_032948.dmp` (8 MB each) |
| App | editor **0.8.2** |
| Reason | `SIGSEGV` |
| GPU | NVIDIA GeForce RTX 3060, driver 560.376.0, Vulkan 1.3.280 |
| Project | `MyGame` |
| Frame health at the time | 240 frames, mean **16.53 ms (60 fps)**, worst 18.32 ms — no stall, no hitch |

**The two stacks are byte-identical**, so this is reproducible rather than a one-off.

## Why it is almost certainly not the performance work

- **The crashing thread is a worker, not the render thread.** Frames #08–#13 sit under
  `BaseThreadInitThunk` → `RtlUserThreadStart`, four frames deep. Everything added in v0.8.2 —
  `GpuZones`, the timestamp writes, `GPUProfiler`'s additive API, the Copy-breakdown button — runs on the
  render thread or the UI thread, never a worker.
- **The profiler was working normally right up to the crash.** The health lines carry per-stage GPU times
  for the whole session, including 4 seconds before the fault.
- **Frame times were flat at 60 fps.** No stall, no growing frame time, nothing that looks like a query pool
  or fence problem.

## What the evidence does point at, weakly

- The log records `[ext] my_extension (Python (pocketpy)) registered 2 command(s)` **15 s after startup** —
  which means a *reload*, since extensions load at startup. The crash followed 29 s later.
- The project's `editor_scripts/` holds **four identical copies** of the starter template —
  `my_extension.py` plus `_1`, `_2`, `_3` — because "New Extension…" was clicked four times. That is four
  live pocketpy VMs, all registering the same two command titles.
- Every symbolized frame resolves to `pkpy_new_vm`.

**That last point is much weaker than it looks** and is the reason this is not being called a pocketpy bug.
The shipped binary is stripped, so the reporter attributes each address to the *nearest exported symbol*;
in a MinGW build with pocketpy linked in, unrelated addresses land on `pkpy_new_vm` routinely. It is
suggestive only because it coincides with an extension reload.

## What was ruled out

`extension_check` gained a group reproducing the exact reported configuration — four identical extensions
all registering the same command titles, five reload cycles, invoke-by-shared-title, and deleting one of
the four. **All pass.** So the loader's bookkeeping (owner tags, slot stability, reload de-duplication,
dangling instances) is not the fault. Whatever this is, it is below that layer — most plausibly in VM
lifetime inside the real pocketpy host, which the fake-host tests cannot see.

## To get further

1. **Symbolize properly.** `engine-v0.8.2-win64-symbols.tar.gz` (161 MB) is published on the release, and
   the report prints the exact `addr2line` invocation. This is the designed path and would settle it.
2. **Narrow by bisection in the UI:** delete three of the four duplicate extensions and see whether the
   crash survives. Cheap, and either result is informative.
3. The minidumps are kept — they carry the worker thread's full context.

## Two things worth fixing regardless

- **"New Extension…" silently creating numbered duplicates is bad behaviour.** Four identical extensions
  all registering the same command titles means three of them are unreachable through the palette
  (`run_by_title` returns the first match) while all four still cost a VM. It should offer to open the
  existing file rather than quietly making `_3`.
- **The crash report's "recent log" section is labelled *newest last* but is dominated by startup lines**,
  so the 40 seconds before the fault are missing. A ring buffer that keeps the newest N lines would have
  made this far easier to place.
