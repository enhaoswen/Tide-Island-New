## Live RAM Monitor

Use this tool to check the RAM usage of Tide Island while it is running.

You need Python 3.10 or newer.

### Usage

```sh
python3 tools/memory_monitor.py -- ./build/Tide-island
```

You can see:

* RSS, PSS and USS memory usage
* Highest RAM usage during the run
* RAM history graph
* Where the RAM is used

**Controls:**

* `r` / `p` / `u` - Sort by RSS, PSS or USS
* `↑` / `↓` - Select a memory source
* `Enter` - Show mapped files
* `q` or `Ctrl+C` - Stop and save the report

It updates every second by default. To change this:

```sh
python3 tools/memory_monitor.py --interval 2 -- ./build/Tide-island
```

Use `--plain` for text output without the dashboard.

### Deep Analysis

To check which parts of the code allocate memory, use Heaptrack:

```sh
python3 tools/memory_monitor.py --deep -- ./build/Tide-island
```

You need to install `heaptrack` and `heaptrack_print` first.

Deep mode records heap allocations. It shows which code allocated the memory
that was still in use at the heap peak.

Build Tide Island with debug symbols (`RelWithDebInfo`) to get more useful results.

Deep mode uses extra RAM and CPU. Use normal mode to measure RAM usage, and deep
mode to look for memory problems.

### Results

Reports are saved in:

```
build/memory-monitor/<timestamp>-<monitor-pid>/
```

Main files:

* `report.txt` - RAM usage and memory sources
* `summary.json` - Results in JSON
* `samples.csv` - RAM usage over time
* `categories.csv` - RAM usage by category
* `application.log` - Tide Island's output

Deep mode also saves the Heaptrack data.

### About RAM Usage

* **RSS:** Total RAM used by the process, including shared memory.
* **PSS:** Shared pages are divided between the processes using them.
* **USS:** Private resident pages of this process.

This tool only checks RAM in the Tide Island process. It does not measure VRAM,
Hyprland memory, or driver memory outside this process.

Heaptrack results and RSS measure different things. Don't add them together.
High RAM usage alone does not mean there is a memory leak.

### NVIDIA compiler file pages

This feature is disabled by default. The program does not call it automatically.
The API is still available if you want to call it yourself on Linux with NVIDIA:

```cpp
#include "graphics/driver_memory.hpp"

// Call on the rendering thread, after shader warmup and a successful swap.
DriverMemory::release_idle_compiler_pages();
```

Call it after the shaders are ready and at least one frame has been drawn and
swapped. The driver may still do some work when a shader is first used. The GL
context must be current, and no other thread should be compiling shaders at the
same time.

The function calls `glFinish()` to wait for earlier GL commands, then calls
`glReleaseShaderCompiler()`. It reads `/proc/self/smaps` and looks for clean,
read-only file pages from `libnvidia-gpucomp`. Writable, relocated, anonymous,
dirty, swapped and locked memory is skipped.

It uses `MADV_NOHUGEPAGE` and `MADV_DONTNEED` on those file pages. The library stays
mapped, and existing shaders and textures are kept. It still uses GPU rendering.
If the renderer is not NVIDIA or the memory check fails, it skips the work. Failed
system calls are logged in debug builds, and rendering continues.

Each call waits and checks the memory mappings again. Don't call it every frame
or every time you switch state.

You can also block manual calls with:

```sh
TIDE_NVIDIA_RECLAIM_COMPILER=0 ./build/Tide-island
```

Setting it to `1` does not make the program call the API automatically.

Debug builds print how many memory ranges were processed and their RSS before
the call. This does not mean the system gets the same amount of free RAM. Other
processes may still use those pages, or the pages may stay in the file cache.

It mainly reduces this process's RSS and PSS. It does not free textures, VRAM, or
the driver's private allocations. Compiling new shaders may load the file pages
again and take extra time.

This uses Linux memory APIs, not an official NVIDIA API for unloading the
compiler. Test it again after updating the driver.
