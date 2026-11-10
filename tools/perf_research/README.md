# perf_research: the measuring tools behind OPTIMIZATION_RESEARCH.md

These are the tools that produced the numbers in
[docs/EngineMasterPlan/OPTIMIZATION_RESEARCH.md](../../docs/EngineMasterPlan/OPTIMIZATION_RESEARCH.md).
None of them is built by CMake and none changes the engine. Each one measures
the editor from the outside, so a number can be re-taken after a change.

All scripts assume the Windows release build in `build/windows-release`. Set
`GWS_BUILD_DIR` to point them at another build directory.

## GPU memory and the Vulkan call stream: `vk_inventory.py`

The Vulkan SDK ships a layer that logs every Vulkan call with its arguments.
Run the editor with it and parse the log:

```sh
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump \
VK_APIDUMP_LOG_FILENAME=api.txt VK_APIDUMP_OUTPUT_RANGE="0,250" \
  build/windows-release/bin/editor.exe --project <project.schizo> --frames 300

python tools/perf_research/vk_inventory.py api.txt            # everything
python tools/perf_research/vk_inventory.py api.txt images     # one section
```

Frame 0 is everything up to the first present (all resource creation). Frame
250 is one steady frame. Sections: `images`, `buffers`, `memory`, `accel`,
`pipelines`, `frame`. Sizes are in MiB. For images the "drv" column is what the
driver reported through `vkGetImageMemoryRequirements`.

Add `VK_APIDUMP_TIMESTAMP=true VK_APIDUMP_DETAILED=false` to get a timestamp on
every call, which turns the same log into a startup profile.

## Heap allocations: `alloc_count.cpp`, `alloc_live.cpp`

`link_instrumented.py` relinks the editor's existing object files with one
extra object and `--wrap`, so every `malloc`/`calloc`/`realloc`/`free` in the
process's own code goes through the probe. The result is a separate
executable; `editor.exe` is untouched.

```sh
python tools/perf_research/link_instrumented.py alloc_count   # bin/editor_alloc_count.exe
python tools/perf_research/link_instrumented.py alloc_live    # bin/editor_alloc_live.exe

GWS_ALLOC_OUT=count.txt build/windows-release/bin/editor_alloc_count.exe --project <p> --frames 300
python tools/perf_research/resolve_allocs.py count.txt 30

GWS_ALLOC_OUT=live.txt build/windows-release/bin/editor_alloc_live.exe --project <p> --frames 300
python tools/perf_research/resolve_live.py live.txt 25 live    # who owns the live heap
python tools/perf_research/resolve_live.py live.txt 25 count   # who allocates most during startup
```

- `alloc_count` counts allocations per frame over frames 151..250 and records
  the call stacks that allocate in that window.
- `alloc_live` tracks every block from process start and, at frame 150, writes
  how many bytes each call stack still owns.

Pass `--log-debug` only if you want the debug log's own allocations in the
count: the console panel re-copies its entries whenever a line is logged.

## Struct layouts: `layout_probe.cpp`, `build_probe.py`

`build_probe.py` compiles a file with the exact flags and include paths of an
engine translation unit, so `sizeof` and member offsets are the real ones.

```sh
python tools/perf_research/build_probe.py tools/perf_research/layout_probe.cpp probe.o \
    -g -O0 -fno-eliminate-unused-debug-types
c++ probe.o -o probe.exe && ./probe.exe            # sizes and alignments
gdb -batch -ex "ptype /o schizo::scene::Entity" probe.exe   # offsets and holes
```

`-Wpadded` does not report interior holes on MinGW (the MS bit-field layout
suppresses the warning), which is why the offsets come from gdb.

## Microbenchmarks: `layout_bench.cpp`, `mat_bench.cpp`

Self-contained; they need only GLM.

```sh
c++ -std=c++20 -O3 -DNDEBUG -Ithird_party/glm tools/perf_research/layout_bench.cpp -o lb.exe && ./lb.exe 20000
c++ -std=c++20 -O3 -DNDEBUG -Ithird_party/glm tools/perf_research/mat_bench.cpp -o mb.exe && ./mb.exe
```

- `layout_bench` walks 20,000 entity-sized heap objects through pointers and
  compares that with a dense array and a struct of arrays.
- `mat_bench` compares GLM's `mat4 * mat4` and a scalar frustum test with
  hand-written SSE versions and checks that the results are identical.

Rebuild them with other flags (`-march=x86-64-v3`, `-DGLM_FORCE_INTRINSICS
-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES`) to see what a flag change does to the
same loops.

## Code the profiler zones miss: `stack_sampler.cpp`, `sample_report.py`

A sampling profiler for the editor's main thread. It runs as a separate
process, suspends the thread every few milliseconds, walks its stack with
dbghelp (the unwind tables are in the executable, so no debug info is needed)
and resumes it.

```sh
c++ -O2 tools/perf_research/stack_sampler.cpp -o stack_sampler.exe -ldbghelp -lpsapi -lwinmm
./stack_sampler.exe <pid of editor.exe> 12 samples.txt
python tools/perf_research/sample_report.py samples.txt 25
```

The report lists, for each engine function, the share of the main thread's
wall time spent in or below it. Multiply by the frame time to get
milliseconds. Time blocked in the present call shows up under
`VulkanSwapchain::present_image`.

Do not use a debugger attach for this. While it attaches, a debugger stops and
restarts the process once per thread and module, so the samples pile up on
whatever runs right after a blocking call returns.

## Idle cost: `thread_cpu.ps1`

With the editor running and idle, prints the CPU time each of its threads used
over ten seconds:

```powershell
powershell -ExecutionPolicy Bypass -File tools/perf_research/thread_cpu.ps1
nvidia-smi --query-gpu=utilization.gpu,clocks.gr,memory.used --format=csv
```
