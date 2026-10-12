## Live RAM Monitor

This tool helps you monitor the RAM usage of Tide Island in real time. It runs in the terminal with a btop-like interface.

It does not change Tide Island's code. You only need Python 3.10+ for normal monitoring.

### Usage

```
python3 tools/memory_monitor.py -- ./build/Tide-island
```

You can see:

* RSS, PSS and USS memory usage
* Highest RAM usage during monitoring
* RAM history graph
* Where the RAM is used

**Controls:**

* `r` / `p` / `u` - Sort by RSS, PSS or USS
* `↑` / `↓` - Select a memory source
* `Enter` - Show mapped files
* `q` or `Ctrl+C` - Stop and save the report

You can also change the update interval (default is 1 second):

```
python3 tools/memory_monitor.py --interval 2 -- ./build/Tide-island
```

Use `--plain` if you don't want the dashboard.

### Deep Analysis

If you want to know which parts of the code are allocating memory, you can use Heaptrack:

```
python3 tools/memory_monitor.py --deep -- ./build/Tide-island
```

This requires `heaptrack` and `heaptrack_print` to be installed.

Deep mode records heap allocations and shows where memory was allocated at the highest heap usage.

For better results, you can build Tide Island with debug symbols (`RelWithDebInfo`).

**Note:** Deep mode uses extra memory and CPU, so don't use it to measure normal RAM usage. It is mainly for finding memory problems.

### Results

Reports are saved in:

```
build/memory-monitor/<timestamp>-<monitor-pid>/
```

Main files:

* `report.txt` - RAM usage and memory sources
* `summary.json` - Summary of the results
* `samples.csv` - RAM usage over time
* `categories.csv` - RAM usage by category
* `application.log` - Tide Island's output

Deep mode also saves Heaptrack data for more analysis.

### About RAM Usage

* **RSS:** Total RAM used by the process, including shared memory.
* **PSS:** RAM usage after dividing shared memory between processes.
* **USS:** RAM only used by this process.

This tool only monitors the process's RAM. It does not measure GPU memory (VRAM), Hyprland memory, or all driver memory.

Heaptrack results and RSS are different measurements, so don't add them together. Also, high memory usage does not always mean a memory leak.
