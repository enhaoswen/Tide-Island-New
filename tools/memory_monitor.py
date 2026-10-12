#!/usr/bin/env python3
"""Launch a Linux program and inspect its RAM without changing its source.

The mapping view and the optional heap profile are separate accounting views.
Only the newly launched process is sampled; the monitor is never included.
"""

from __future__ import annotations

import argparse
import collections
import csv
from dataclasses import dataclass, field
from datetime import datetime
import heapq
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unicodedata


ROOT = Path(__file__).resolve().parent.parent
METRICS = ("rss", "pss", "uss", "size", "swap")
CATEGORIES = {
    "application": "Application executable",
    "nvidia": "NVIDIA driver libraries",
    "mesa": "Mesa driver libraries",
    "compiler": "Graphics compiler (LLVM)",
    "graphics_support": "Graphics support libraries",
    "opengl": "OpenGL / EGL libraries",
    "wayland": "Wayland libraries",
    "font_libraries": "Font libraries",
    "font_files": "Font files",
    "font_cache": "Fontconfig cache files",
    "heap": "brk heap (app + libraries)",
    "anonymous": "Anonymous (app + libraries)",
    "stack": "Identified stacks",
    "gpu_device": "GPU device mappings (RAM)",
    "profiler": "Heaptrack instrumentation",
    "other_libraries": "Other shared libraries",
    "other_files": "Other mapped files",
    "other": "Other / kernel mappings",
}
NOTES = (
    "RAM only; this report does not measure VRAM or driver memory outside this process.",
    "RSS is resident memory, PSS apportions shared pages, USS is Private_Clean + Private_Dirty.",
    "brk heap and anonymous mappings contain allocations from both application and libraries.",
    "Library rows measure mapped pages, not every allocation made by those libraries.",
    "Device mappings show the kernel's RAM accounting, not a separate VRAM measurement.",
    "Peaks are observed samples; brief peaks between samples can be missed.",
    "PSS changes when other processes share pages. Category peaks occur independently and are not additive.",
)
HEADER = re.compile(
    r"^([\da-fA-F]+)-([\da-fA-F]+)\s+([rwxps-]{4})\s+"
    r"([\da-fA-F]+)\s+([\da-fA-F]+:[\da-fA-F]+)\s+(\d+)(?:\s+(.*))?$"
)
FIELD = re.compile(r"^(\w+):\s+(\d+)\s+kB$")
REQUIRED_FIELDS = {"Rss", "Pss", "Private_Clean", "Private_Dirty", "Size", "Swap"}


def without_deleted(path: str) -> str:
    return path.removesuffix(" (deleted)")


def category(path: str, executable: str) -> str:
    """Assign one category per VMA. Names are evidence, not heap ownership."""
    path = without_deleted(path)
    name = Path(path).name
    if path == without_deleted(executable):
        return "application"
    if path == "[heap]":
        return "heap"
    if re.fullmatch(r"\[stack(?::\d+)?\]", path):
        return "stack"
    if path == "[anonymous]" or path.startswith(("[anon:", "[anon_shmem:")):
        return "anonymous"
    if path.startswith(("/dev/nvidia", "/dev/dri/")) or "dmabuf" in path.lower():
        return "gpu_device"
    if name.startswith("libheaptrack"):
        return "profiler"
    if name.startswith(("libnvidia", "libEGL_nvidia", "libGLX_nvidia")):
        return "nvidia"
    if name.startswith(("libEGL_mesa", "libGLX_mesa", "libgallium", "libglapi")) or (
        "/dri/" in path and ".so" in name
    ):
        return "mesa"
    if name.startswith("libLLVM"):
        return "compiler"
    if name.startswith(("libdrm", "libgbm")):
        return "graphics_support"
    if name.startswith(("libEGL", "libGLES", "libGL.", "libGLX", "libGLdispatch", "libOpenGL")):
        return "opengl"
    if name.startswith("libwayland"):
        return "wayland"
    if name.startswith(("libfreetype", "libharfbuzz", "libfontconfig", "libgraphite")):
        return "font_libraries"
    if re.search(r"\.(?:ttf|otf|ttc|otc|woff2?)$", name, re.IGNORECASE):
        return "font_files"
    if "fontconfig" in path and not re.search(r"\.so(?:\.|$)", name):
        return "font_cache"
    if re.search(r"\.so(?:\.|$)", name) or name.startswith("ld-linux"):
        return "other_libraries"
    if path.startswith("["):
        return "other"
    return "other_files"


def zeros() -> dict[str, int]:
    return dict.fromkeys(METRICS, 0)


@dataclass
class Mapping:
    path: str
    fields: dict[str, int] = field(default_factory=dict)

    def metrics(self) -> dict[str, int]:
        return {
            "rss": self.fields["Rss"],
            "pss": self.fields["Pss"],
            "uss": self.fields["Private_Clean"] + self.fields["Private_Dirty"],
            "size": self.fields["Size"],
            "swap": self.fields["Swap"],
        }


def parse_smaps(text: str) -> list[Mapping]:
    mappings: list[Mapping] = []
    current = None
    for line in text.splitlines():
        if match := HEADER.fullmatch(line):
            current = Mapping(match[7] or "[anonymous]")
            mappings.append(current)
        elif current is not None and (match := FIELD.fullmatch(line)):
            current.fields[match[1]] = int(match[2]) * 1024
    if not mappings:
        raise ValueError("smaps contains no mappings (the process may have exited)")
    for mapping in mappings:
        missing = REQUIRED_FIELDS - mapping.fields.keys()
        if missing:
            raise ValueError(f"incomplete smaps mapping: {mapping.path!r}, missing {sorted(missing)}")
    return mappings


@dataclass
class Sample:
    elapsed: float
    pid: int
    totals: dict[str, int]
    categories: dict[str, dict[str, int]]
    paths: dict[str, dict[str, dict[str, int]]]
    mapping_count: int

    def as_dict(self) -> dict:
        return {
            "elapsed_seconds": round(self.elapsed, 4),
            "pid": self.pid,
            "totals_bytes": self.totals,
            "categories_bytes": self.categories,
            "paths_bytes": self.paths,
            "mapping_count": self.mapping_count,
        }


def summarize(text: str, executable: str, pid: int, elapsed: float) -> Sample:
    mappings = parse_smaps(text)
    totals = zeros()
    groups: dict[str, dict[str, int]] = {}
    paths: dict[str, dict[str, dict[str, int]]] = {}
    for mapping in mappings:
        key = category(mapping.path, executable)
        group = groups.setdefault(key, zeros())
        by_path = paths.setdefault(key, {}).setdefault(mapping.path, zeros())
        for metric, value in mapping.metrics().items():
            totals[metric] += value
            group[metric] += value
            by_path[metric] += value
    return Sample(elapsed, pid, totals, groups, paths, len(mappings))


def format_bytes(value: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(value) < 1024 or unit == "TiB":
            return f"{value:.2f} {unit}"
        value /= 1024
    raise AssertionError("unreachable")


def proc_identity(pid: int) -> tuple[int, str]:
    # comm can contain spaces and ')' characters; fields after its last ')' are stable.
    tail = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return int(tail[19]), tail[0]  # starttime (field 22), state (field 3)


def descendants(pid: int) -> list[int]:
    pending = [pid]
    result = []
    seen = {pid}
    while pending:
        parent = pending.pop()
        try:
            children = Path(f"/proc/{parent}/task/{parent}/children").read_text().split()
        except OSError:
            continue
        for child in map(int, children):
            if child not in seen:
                seen.add(child)
                result.append(child)
                pending.append(child)
    return result


def resolve_command(command: list[str]) -> list[str]:
    if not command:
        raise ValueError("provide a command after --, e.g. -- ./build/Tide-island")
    found = shutil.which(command[0])
    if not found or not Path(found).is_file():
        raise ValueError(f"executable not found or not executable: {command[0]}")
    return [str(Path(found).resolve()), *command[1:]]


def find_heaptrack(directory: Path | None) -> tuple[Path, Path]:
    candidates = []
    if directory is not None:
        candidates.extend(directory / suffix for suffix in ("", "bin", "usr/bin"))
    else:
        if found := shutil.which("heaptrack"):
            candidates.append(Path(found).resolve().parent)
        candidates.append(ROOT / "build/memory-analysis/tools/usr/bin")
    errors = []
    for base in candidates:
        base = base.expanduser().resolve()
        launcher, printer = base / "heaptrack", base / "heaptrack_print"
        if not all(p.is_file() and os.access(p, os.X_OK) for p in (launcher, printer)):
            continue
        try:
            result = subprocess.run([str(printer), "--version"], capture_output=True, text=True, timeout=5)
            if result.returncode:
                errors.append(result.stderr.strip() or result.stdout.strip())
                continue
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.append(str(exc))
            continue
        return launcher.resolve(), printer.resolve()
    detail = "; ".join(errors)
    raise ValueError(
        "--deep needs working heaptrack and heaptrack_print in the same directory. "
        "Install Heaptrack or pass --heaptrack-dir /path/to/its/bin."
        + (f" Details: {detail}" if detail else "")
    )


class OwnedProcess:
    def __init__(self, command: list[str], out: Path, heaptrack: Path | None):
        self.command, self.out, self.heaptrack = command, out, heaptrack
        self.process: subprocess.Popen | None = None
        self.target_pid: int | None = None
        self.target_start: int | None = None
        self.pidfd: int | None = None
        self.forced_cleanup = False
        self.profiler_drained = False
        self.log = None
        self.profile_directory = None

    def start(self):
        self.log = (self.out / "application.log").open("w")
        command = self.command
        if self.heaptrack:
            # Older heaptrack shell launchers do not quote output paths correctly.
            # Stage only the trace in an ASCII path; keep the target's cwd unchanged.
            self.profile_directory = tempfile.TemporaryDirectory(prefix="tide-heaptrack-", dir="/tmp")
            prefix = Path(self.profile_directory.name) / "heaptrack"
            command = [str(self.heaptrack), "--record-only", "--output", str(prefix), *command]
        self.process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=self.log, stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        if not self.heaptrack:
            try:
                self.bind_target(self.process.pid)
            except FileNotFoundError:
                # Very short-lived programs can exit before /proc is read.
                self.target_pid = self.process.pid

    def bind_target(self, pid: int):
        self.target_start, _ = proc_identity(pid)
        self.target_pid = pid
        if hasattr(os, "pidfd_open"):
            try:
                self.pidfd = os.pidfd_open(pid)
            except OSError:
                pass

    def discover_target(self):
        if self.target_pid is not None or self.process is None:
            return
        for pid in descendants(self.process.pid):
            try:
                if os.path.samefile(f"/proc/{pid}/exe", self.command[0]):
                    self.bind_target(pid)
                    return
            except OSError:
                continue

    def target_alive(self) -> bool:
        if self.target_pid is None:
            return False
        try:
            start, state = proc_identity(self.target_pid)
            return start == self.target_start and state not in ("Z", "X")
        except OSError:
            return False

    def signal_target(self, sig: int):
        if not self.target_alive():
            return
        try:
            if self.pidfd is not None and hasattr(signal, "pidfd_send_signal"):
                signal.pidfd_send_signal(self.pidfd, sig)
            else:
                os.kill(self.target_pid, sig)
        except ProcessLookupError:
            pass

    def signal_group(self, sig: int):
        # start_new_session gives this launch a private process group.
        try:
            os.killpg(self.process.pid, sig)
        except ProcessLookupError:
            pass

    def close(self):
        try:
            if self.process is None:
                return
            if self.heaptrack:
                self.discover_target()
                if self.target_alive():
                    # Let the recorder/interpreter/compressor see EOF before stopping them.
                    for pid in descendants(self.target_pid):
                        try:
                            os.kill(pid, signal.SIGTERM)
                        except ProcessLookupError:
                            pass
                    self.signal_target(signal.SIGTERM)
                try:
                    self.process.wait(timeout=5)
                    self.profiler_drained = True
                except subprocess.TimeoutExpired:
                    self.forced_cleanup = True
                    self.signal_target(signal.SIGKILL)
                    try:
                        self.process.wait(timeout=3)
                        self.profiler_drained = True
                    except subprocess.TimeoutExpired:
                        self.signal_group(signal.SIGTERM)
                        try:
                            self.process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            self.signal_group(signal.SIGKILL)
                            self.process.wait(timeout=2)
            elif self.process.poll() is None:
                self.signal_group(signal.SIGTERM)
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.forced_cleanup = True
                    self.signal_group(signal.SIGKILL)
                    self.process.wait(timeout=2)
            # Also clean up descendants that outlived their parent.
            self.signal_group(signal.SIGTERM)
            deadline = time.monotonic() + 0.5
            while time.monotonic() < deadline:
                try:
                    os.killpg(self.process.pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.02)
            else:
                self.forced_cleanup = True
                self.signal_group(signal.SIGKILL)
        finally:
            if self.pidfd is not None:
                os.close(self.pidfd)
                self.pidfd = None
            if self.log is not None:
                self.log.close()
            if self.profile_directory is not None:
                for trace in Path(self.profile_directory.name).glob("heaptrack*"):
                    if trace.is_file():
                        shutil.move(str(trace), str(self.out / trace.name))
                self.profile_directory.cleanup()
                self.profile_directory = None


class Recorder:
    def __init__(self, out: Path):
        self.out = out
        self.history = collections.deque(maxlen=600)
        self.latest: Sample | None = None
        self.rss_peak: Sample | None = None
        self.last_raw = ""
        self.count = 0
        self.peaks = zeros()
        self.category_peaks: dict[str, dict[str, int]] = {}
        self.streams = []
        for filename, columns in (
            ("samples.csv", ["elapsed_seconds", "pid", *[f"{m}_bytes" for m in METRICS]]),
            ("categories.csv", ["elapsed_seconds", "category", *[f"{m}_bytes" for m in METRICS]]),
        ):
            stream = (out / filename).open("w", newline="")
            self.streams.append(stream)
            writer = csv.writer(stream)
            writer.writerow(columns)
            if filename == "samples.csv":
                self.samples_writer = writer
            else:
                self.categories_writer = writer

    def add(self, sample: Sample, raw: str):
        self.count += 1
        self.latest, self.last_raw = sample, raw
        self.history.append((sample.elapsed, sample.totals))
        self.samples_writer.writerow([f"{sample.elapsed:.4f}", sample.pid, *[sample.totals[m] for m in METRICS]])
        for key, metrics in sample.categories.items():
            self.categories_writer.writerow([f"{sample.elapsed:.4f}", key, *[metrics[m] for m in METRICS]])
            peaks = self.category_peaks.setdefault(key, zeros())
            for metric in METRICS:
                peaks[metric] = max(peaks[metric], metrics[metric])
        for metric in METRICS:
            self.peaks[metric] = max(self.peaks[metric], sample.totals[metric])
        if self.count == 1:
            (self.out / "first.smaps").write_text(raw)
        if self.rss_peak is None or sample.totals["rss"] > self.rss_peak.totals["rss"]:
            self.rss_peak = sample
            (self.out / "peak-rss.smaps").write_text(raw)
        for stream in self.streams:
            stream.flush()

    def close(self):
        for stream in self.streams:
            stream.close()
        if self.last_raw:
            (self.out / "latest.smaps").write_text(self.last_raw)


class Monitor:
    def __init__(self, owned: OwnedProcess, recorder: Recorder, interval: float):
        self.owned, self.recorder, self.interval = owned, recorder, interval
        self.started = time.monotonic()
        self.started_at = datetime.now().astimezone().isoformat()
        self.ended: float | None = None
        self.stop_reason: str | None = None
        self.cancel_analysis = False
        self.status = "Starting target..."
        self.error: str | None = None
        self.next_sample = 0.0
        self.last_plain = -math.inf
        self.transient_error_since: float | None = None
        self.metric = "rss"
        self.selection = 0
        self.expanded = False
        self.warnings = []

    def request_stop(self, reason="user_stop"):
        if self.stop_reason is None:
            self.stop_reason = reason
        else:
            self.cancel_analysis = True

    def tick(self):
        if self.stop_reason is not None:
            return
        elapsed = time.monotonic() - self.started
        self.owned.discover_target()
        if self.owned.process.poll() is not None:
            self.stop_reason = "target_exited"
            return
        if self.owned.target_pid is None:
            if elapsed > 10:
                raise RuntimeError("Heaptrack did not launch the requested ELF executable within 10 seconds; see application.log")
            return
        if not self.owned.target_alive():
            self.status = "Target exited; waiting for profiler..."
            # close() performs the bounded wait and cleanup.
            self.stop_reason = "target_exited"
            return
        if elapsed < self.next_sample:
            return
        proc = Path(f"/proc/{self.owned.target_pid}")
        raw = ""
        try:
            raw = (proc / "smaps").read_text()
            executable = os.readlink(proc / "exe")
            sample = summarize(raw, executable, self.owned.target_pid, elapsed)
        except (OSError, ValueError) as exc:
            if not self.owned.target_alive():
                self.stop_reason = "target_exited"
                return
            # During exit the kernel can detach mm/exe before reporting a zombie.
            # Retry briefly without replacing the last sample with zeroes.
            if isinstance(exc, (FileNotFoundError, ProcessLookupError)) or (isinstance(exc, ValueError) and not raw.strip()):
                if self.transient_error_since is None:
                    self.transient_error_since = elapsed
                if elapsed - self.transient_error_since < 1:
                    self.status = "Memory sample unavailable; previous sample retained"
                    self.next_sample = elapsed + 0.05
                    return
            raise
        self.transient_error_since = None
        self.recorder.add(sample, raw)
        self.status = f"{sample.mapping_count} mappings | {self.recorder.count} samples | RAM only"
        self.next_sample = elapsed + (min(0.1, self.interval) if elapsed < 2 else self.interval)

    def rows(self):
        sample = self.recorder.latest
        return sorted(sample.categories.items(), key=lambda row: (-row[1][self.metric], row[0])) if sample else []

    def plain(self):
        print(f"Monitoring {self.owned.command[0]} | Ctrl+C stops this launch | RAM only", flush=True)
        if self.owned.heaptrack:
            print("DEEP MODE: RSS includes Heaptrack overhead; heap attribution runs after stopping.", flush=True)
        while self.stop_reason is None:
            self.tick()
            elapsed = time.monotonic() - self.started
            sample = self.recorder.latest
            if sample and elapsed - self.last_plain >= 1:
                self.last_plain = elapsed
                amounts = "  ".join(f"{m.upper()} {format_bytes(sample.totals[m])}" for m in ("rss", "pss", "uss"))
                top = "; ".join(f"{CATEGORIES[k]} {format_bytes(v['rss'])}" for k, v in self.rows()[:3])
                print(f"[{sample.elapsed:7.1f}s] PID {sample.pid}  {amounts} | {top}", flush=True)
            time.sleep(0.025)


def clip_cells(text: str, width: int) -> str:
    result = []
    used = 0
    for char in text:
        if unicodedata.category(char).startswith("C"):
            char = "?"
        size = 0 if unicodedata.combining(char) else 2 if unicodedata.east_asian_width(char) in ("W", "F") else 1
        if used + size > width:
            break
        result.append(char)
        used += size
    return "".join(result)


class TerminalView:
    """Curses only lives in this view; sampling also works without a terminal."""
    def __init__(self, screen, monitor: Monitor):
        import curses
        self.curses, self.screen, self.monitor = curses, screen, monitor
        screen.nodelay(True)
        screen.keypad(True)
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        self.color = 0
        if curses.has_colors():
            curses.start_color()
            try:
                curses.use_default_colors()
                background = -1
            except curses.error:
                background = curses.COLOR_BLACK
            curses.init_pair(1, curses.COLOR_CYAN, background)
            curses.init_pair(2, curses.COLOR_GREEN, background)
            curses.init_pair(3, curses.COLOR_YELLOW, background)
            self.color = curses.color_pair(1)
        self.block = "█" if "UTF" in (sys.stdout.encoding or "").upper() else "#"

    def write(self, y, x, text, attr=0, width=None):
        height, columns = self.screen.getmaxyx()
        if y < 0 or y >= height or x < 0 or x >= columns - 1:
            return
        available = min(columns - x - 1, width if width is not None else columns)
        try:
            self.screen.addstr(y, x, clip_cells(text, available), attr)
        except self.curses.error:
            # A resize can occur between getmaxyx and addstr.
            pass

    def box(self, y, x, height, width, title):
        if height < 3 or width < 4:
            return
        self.write(y, x, "+" + "-" * (width - 2) + "+", self.color)
        self.write(y + height - 1, x, "+" + "-" * (width - 2) + "+", self.color)
        for row in range(y + 1, y + height - 1):
            self.write(row, x, "|", self.color)
            self.write(row, x + width - 1, "|", self.color)
        self.write(y, x + 2, f" {title} ", self.color | self.curses.A_BOLD, width - 4)

    def handle(self, key):
        m = self.monitor
        if key in (ord("q"), ord("Q"), 3):
            m.request_stop()
        elif key in (ord("r"), ord("p"), ord("u")):
            m.metric = {ord("r"): "rss", ord("p"): "pss", ord("u"): "uss"}[key]
            m.selection = 0
        elif key == self.curses.KEY_UP:
            m.selection = max(0, m.selection - 1)
        elif key == self.curses.KEY_DOWN:
            m.selection = min(max(0, len(m.rows()) - 1), m.selection + 1)
        elif key in (10, 13, self.curses.KEY_ENTER):
            m.expanded = not m.expanded

    def draw(self):
        s, m = self.screen, self.monitor
        s.erase()
        height, width = s.getmaxyx()
        sample = m.recorder.latest
        if width < 60 or height < 18:
            self.write(0, 0, "TIDE ISLAND / RAM  |  q or Ctrl+C: stop")
            self.write(1, 0, "Compact view (resize for chart and mappings)")
            if sample:
                for row, metric in enumerate(("rss", "pss", "uss"), 3):
                    self.write(row, 0, f"{metric.upper()}: {format_bytes(sample.totals[metric])}  peak: {format_bytes(m.recorder.peaks[metric])}")
            self.write(height - 2, 0, m.status)
            s.refresh()
            return
        self.box(0, 0, 6, width - 1, "TIDE ISLAND / RAM")
        pid = m.owned.target_pid or "starting"
        self.write(1, 2, f"PID {pid}  |  uptime {time.monotonic() - m.started:.1f}s  |  {'DEEP: instrumented' if m.owned.heaptrack else 'NORMAL'}")
        self.write(2, 2, "Command: " + " ".join(m.owned.command))
        if sample:
            self.write(3, 2, "  ".join(f"{k.upper()} {format_bytes(sample.totals[k])}" for k in ("rss", "pss", "uss")))
            self.write(4, 2, "Sampled peaks: " + "  ".join(f"{k.upper()} {format_bytes(m.recorder.peaks[k])}" for k in ("rss", "pss", "uss")))
        else:
            self.write(3, 2, "Waiting for first readable smaps sample...")
        chart_height = 7 if height >= 28 else 5
        self.box(6, 0, chart_height, width - 1, f"{m.metric.upper()} history / recent samples")
        plot_width, plot_height = width - 18, chart_height - 2
        values = [totals[m.metric] for _, totals in list(m.recorder.history)[-plot_width:]]
        maximum = max(max(values, default=0), 4096)
        for row in range(plot_height):
            label = format_bytes(int(maximum * (plot_height - row) / plot_height))
            self.write(7 + row, 2, f"{label:>11}")
            graph = " " * (plot_width - len(values)) + "".join(
                self.block if value / maximum * plot_height > plot_height - row - 1 else " "
                for value in values
            )
            self.write(7 + row, 15, graph, self.curses.color_pair(2) if self.color else 0)
        top = 6 + chart_height
        table_height = height - top - 3
        rows = m.rows()
        m.selection = min(m.selection, max(0, len(rows) - 1))
        if m.expanded and width >= 105:
            table_width = max(62, width * 3 // 5)
            self.ranking(top, 0, table_height, table_width, rows)
            self.details(top, table_width, table_height, width - table_width - 1, rows)
        elif m.expanded and table_height >= 10:
            first_height = table_height // 2
            self.ranking(top, 0, first_height, width - 1, rows)
            self.details(top + first_height, 0, table_height - first_height, width - 1, rows)
        elif m.expanded:
            self.details(top, 0, table_height, width - 1, rows)
        else:
            self.ranking(top, 0, table_height, width - 1, rows)
        self.write(height - 3, 1, "Heap + anonymous = app AND libraries; device rows = RAM, not VRAM.")
        self.write(height - 2, 1, "r RSS | p PSS | u USS | Up/Down select | Enter details | q/Ctrl+C stop")
        self.write(height - 1, 1, m.status + (" | heap attribution after stop" if m.owned.heaptrack else ""))
        s.refresh()

    def ranking(self, y, x, height, width, rows):
        m = self.monitor
        self.box(y, x, height, width, f"Sources / {m.metric.upper()}")
        self.write(y + 1, x + 2, f"{'Source':<{max(16, width - 33)}} {'RAM':>11} {'Share':>6}", width=width - 4)
        count = max(0, height - 3)
        start = max(0, m.selection - count + 1)
        total = m.recorder.latest.totals[m.metric] if m.recorder.latest else 0
        for line, (key, metrics) in enumerate(rows[start:start + count]):
            value = metrics[m.metric]
            share = value / total * 100 if total else 0
            name_width = max(16, width - 33)
            name = clip_cells(CATEGORIES[key], name_width)
            text = f"{name:<{name_width}} {format_bytes(value):>11} {share:5.1f}%"
            attr = self.curses.A_REVERSE if start + line == m.selection else 0
            self.write(y + 2 + line, x + 2, text, attr, width - 4)

    def details(self, y, x, height, width, rows):
        self.box(y, x, height, width, "Selected source / mappings")
        if not rows:
            return
        m = self.monitor
        key = rows[m.selection][0]
        self.write(y + 1, x + 2, CATEGORIES[key], self.curses.A_BOLD, width - 4)
        paths = sorted(m.recorder.latest.paths[key].items(), key=lambda row: -row[1][m.metric])
        available = max(0, (height - 3) // 2)
        for index, (path, metrics) in enumerate(paths[:available]):
            self.write(y + 2 + index * 2, x + 2, format_bytes(metrics[m.metric]) + "  " + Path(path).name, width=width - 4)
            self.write(y + 3 + index * 2, x + 2, path, width=width - 4)
        if len(paths) > available and height > 3:
            self.write(y + height - 2, x + 2, f"{len(paths) - available} more mappings in report.txt", width=width - 4)

    def run(self):
        while self.monitor.stop_reason is None:
            self.handle(self.screen.getch())
            self.monitor.tick()
            self.draw()
            time.sleep(0.05)


@dataclass(frozen=True)
class FrameInfo:
    function: str
    module: str = ""
    source: str = ""


def profile_frames(report: str) -> dict[str, list[FrameInfo]]:
    """Read frame/module pairs from this trace, including stripped addresses."""
    result: dict[str, list[FrameInfo]] = {}
    function, module, source = "", "", ""

    def save():
        if function and (module or source):
            info = FrameInfo(function, module, source)
            bucket = result.setdefault(function, [])
            if info not in bucket:
                bucket.append(info)

    for line in report.splitlines():
        stripped = line.strip()
        if line.startswith("  ") and stripped:
            if stripped.startswith("in "):
                module = stripped[3:]
            elif stripped.startswith("at "):
                source = stripped[3:]
            else:
                save()
                function, module, source = stripped, "", ""
        else:
            save()
            function, module, source = "", "", ""
    save()
    return result


def resolve_frame(frame: str, index: dict[str, list[FrameInfo]]) -> FrameInfo:
    filename = ""
    function = frame
    if match := re.fullmatch(r"(.*) \(([^()]*)\)", frame):
        if "." in match[2] or "/" in match[2]:
            function, filename = match[1], match[2]
    candidates = index.get(function, [])
    if filename:
        candidates = [info for info in candidates if Path(info.source.rsplit(":", 1)[0]).name == filename]
    if len(candidates) == 1:
        return candidates[0]
    modules = {info.module for info in candidates if info.module}
    if len(modules) == 1:
        return FrameInfo(function, next(iter(modules)), filename)
    return FrameInfo(function, source=filename)


def allocator_frame(frame: FrameInfo) -> bool:
    return frame.function.startswith(("std::", "__gnu_cxx::", "operator new", "operator delete")) or bool(
        re.fullmatch(r"(?:__GI_)?(?:__libc_)?(?:malloc|calloc|realloc|free|strdup|strndup|__strdup|memalign|aligned_alloc|posix_memalign)(?:\(.*\))?", frame.function)
    )


def heap_owner(frames: list[FrameInfo], executable: str) -> str:
    for frame in reversed(frames):
        if allocator_frame(frame):
            continue
        source = Path(frame.source.rsplit(":", 1)[0]).name
        if frame.module == "??":
            return "Unknown"
        if frame.module and without_deleted(frame.module) != executable:
            key = category(frame.module, executable)
            return CATEGORIES.get(key, "Unknown")
        if source == "sokol_gfx.h" or frame.function.startswith(("_sg_", "sg_")):
            return "Sokol CPU allocations"
        if frame.function.startswith("stbi_") or source == "stb_image.h":
            return "Image decoding (stb)"
        for namespace, filename in (("Text", "text.cpp"), ("Renderer", "renderer.cpp"), ("Object", "object.cpp"), ("Wayland", "wayland.cpp")):
            if frame.function.startswith(namespace + "::") or source == filename:
                return "Project / " + namespace
        if frame.module == executable or source in {"main.cpp", "API.cpp", "island.cpp", "timer.cpp", "clock_view.cpp"}:
            return "Project / Other"
        # An opaque allocating frame is not evidence that an outer app caller owns it.
        return "Unknown"
    return "Unknown"


def attribute_heap(stacks: Path, report: Path, executable: str) -> dict:
    index = profile_frames(report.read_text(errors="replace"))
    totals = collections.Counter()
    top = []
    count = 0
    with stacks.open(errors="replace") as stream:
        for line in stream:
            stack, separator, value = line.rstrip().rpartition(" ")
            if not separator or not value.isdecimal() or ";" not in stack:
                raise ValueError("unrecognized Heaptrack folded-stack record")
            amount = int(value)
            if not amount:
                continue
            frames = [resolve_frame(part, index) for part in stack.split(";") if part]
            owner = heap_owner(frames, executable)
            totals[owner] += amount
            count += 1
            item = (amount, count, owner, frames)
            if len(top) < 20:
                heapq.heappush(top, item)
            elif amount > top[0][0]:
                heapq.heapreplace(top, item)
    if not count:
        raise ValueError("Heaptrack did not export any positive peak allocations")
    return {
        "global_peak_live_bytes": sum(totals.values()),
        "categories_bytes": dict(totals.most_common()),
        "stack_count": count,
        "top_stacks": [
            {"live_bytes": amount, "category": owner, "frames": [vars(frame) for frame in frames]}
            for amount, _, owner, frames in sorted(top, reverse=True)
        ],
    }


def analyze_heap(printer: Path, out: Path, monitor: Monitor) -> dict:
    traces = sorted([*out.glob("heaptrack*.zst"), *out.glob("heaptrack*.gz")])
    if len(traces) != 1:
        return {"status": "error", "error": "Heaptrack did not produce one trace; see application.log"}
    report, stacks = out / "heaptrack-stacks.txt", out / "heap-peak.stacks"
    command = [
        str(printer), str(traces[0]), "--print-allocators=0", "--print-temporary=0",
        "--print-leaks=0", "--merge-backtraces=0", "--peak-limit=2147483647",
        "--flamegraph-cost-type=peak", f"--print-flamegraph={stacks}",
    ]
    print("Analyzing heap allocations at the global heap peak... (Ctrl+C cancels analysis)", flush=True)
    with report.open("w") as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 120
        try:
            while process.poll() is None:
                if monitor.cancel_analysis or time.monotonic() >= deadline:
                    raise RuntimeError("heap analysis cancelled or exceeded 120 seconds; the trace is preserved")
                time.sleep(0.1)
            if process.returncode:
                raise RuntimeError(f"heaptrack_print exited with {process.returncode}; see {report.name}")
            result = attribute_heap(stacks, report, monitor.owned.command[0])
            result.update({
                "status": "ok" if monitor.stop_reason == "target_exited" and monitor.owned.process.returncode == 0 else "partial",
                "trace": traces[0].name,
                "profiler_drained": monitor.owned.profiler_drained,
                "note": "Tracked allocation bytes, not RSS. Interrupted runs may lose final events. Live allocations are not a leak verdict.",
            })
            return result
        except (OSError, ValueError, RuntimeError) as exc:
            return {"status": "error", "error": str(exc), "trace": traces[0].name}
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait(timeout=2)


def report_text(summary: dict) -> str:
    lines = ["Tide Island / RAM report", "=" * 72, f"Command: {' '.join(summary['command'])}",
             f"PID: {summary['target_pid']} | Mode: {summary['mode']} | End: {summary['end_reason']}",
             f"Samples: {summary['sample_count']} | Runtime: {summary['elapsed_seconds']:.2f}s", ""]
    if summary.get("error"):
        lines.extend(["ERROR: " + summary["error"], ""])
    sample = summary["latest"]
    if sample:
        for metric in ("rss", "pss", "uss"):
            lines.append(f"{metric.upper():3}: {format_bytes(sample['totals_bytes'][metric]):>13}   sampled peak: {format_bytes(summary['sampled_peaks_bytes'][metric])}")
        lines.extend(["", f"Latest mapping breakdown (sample at {sample['elapsed_seconds']:.2f}s)",
                      f"{'Source':<36} {'RSS':>12} {'PSS':>12} {'USS':>12}"])
        for key, metrics in sorted(sample["categories_bytes"].items(), key=lambda item: -item[1]["rss"]):
            lines.append(f"{CATEGORIES[key]:<36} " + " ".join(f"{format_bytes(metrics[m]):>12}" for m in ("rss", "pss", "uss")))
        lines.extend(["", "Mapped paths in the latest sample (ordered by RSS)"])
        for key, paths in sample["paths_bytes"].items():
            lines.append("\n" + CATEGORIES[key])
            for path, metrics in sorted(paths.items(), key=lambda item: -item[1]["rss"]):
                lines.append("  " + " / ".join(f"{m.upper()} {format_bytes(metrics[m])}" for m in ("rss", "pss", "uss")) + "  " + path)
    else:
        lines.append("No memory sample was captured; unavailable data is not zero usage.")
    if heap := summary.get("heap_analysis"):
        lines.extend(["", "Heap allocation profile (separate from RSS)", f"Status: {heap['status']}"])
        if heap.get("error"):
            lines.append(heap["error"])
        else:
            lines.append("Live allocation bytes at the GLOBAL heap peak: " + format_bytes(heap["global_peak_live_bytes"]))
            for name, amount in heap["categories_bytes"].items():
                lines.append(f"  {name:<36} {format_bytes(amount):>13}")
            lines.extend([heap["note"], "", "Largest allocation stacks at that peak (caller -> allocator)"])
            for stack in heap["top_stacks"]:
                lines.append(f"\n{format_bytes(stack['live_bytes'])} | {stack['category']}")
                for frame in stack["frames"]:
                    lines.append("  " + frame["function"] + (" at " + frame["source"] if frame["source"] else "") + (" in " + frame["module"] if frame["module"] else ""))
    lines.extend(["", "Accounting notes", *["- " + note for note in summary["notes"]]])
    lines.extend("- " + warning for warning in summary["warnings"])
    return "\n".join(lines) + "\n"


def save_report(monitor: Monitor, heap_analysis: dict | None, command: list[str]) -> dict:
    recorder, owned = monitor.recorder, monitor.owned
    summary = {
        "schema_version": 1, "units": "bytes", "command": command,
        "started_at": monitor.started_at, "target_pid": owned.target_pid,
        "launcher_pid": owned.process.pid if owned.process else None,
        "mode": "deep (instrumented)" if owned.heaptrack else "normal",
        "end_reason": monitor.stop_reason, "error": monitor.error,
        "launcher_returncode": owned.process.returncode if owned.process else None,
        "forced_cleanup": owned.forced_cleanup,
        "elapsed_seconds": round((monitor.ended or time.monotonic()) - monitor.started, 4),
        "sample_count": recorder.count, "sampled_peaks_bytes": recorder.peaks,
        "category_sampled_peaks_bytes": recorder.category_peaks,
        "latest": recorder.latest.as_dict() if recorder.latest else None,
        "rss_peak_sample": recorder.rss_peak.as_dict() if recorder.rss_peak else None,
        "heap_analysis": heap_analysis, "notes": list(NOTES), "warnings": monitor.warnings,
    }
    if owned.heaptrack:
        summary["notes"].append("DEEP MODE: measured RAM includes Heaptrack overhead; use a normal run for the baseline.")
    (recorder.out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    (recorder.out / "report.txt").write_text(report_text(summary))
    return summary


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, epilog="Example: python3 tools/memory_monitor.py -- ./build/Tide-island")
    parser.add_argument("--interval", type=float, default=1.0, help="regular sampling interval in seconds (default: 1)")
    parser.add_argument("--output", type=Path, help="new or empty output directory")
    parser.add_argument("--plain", action="store_true", help="use periodic text output instead of curses")
    parser.add_argument("--deep", action="store_true", help="record heap allocations and analyze them after stopping")
    parser.add_argument("--heaptrack-dir", type=Path, help="Heaptrack installation/bin directory (only with --deep)")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- executable [arguments...]")
    args = parser.parse_args(argv)
    if sys.platform != "linux":
        parser.error("this monitor needs Linux /proc/<pid>/smaps")
    if not math.isfinite(args.interval) or args.interval <= 0:
        parser.error("--interval must be a finite positive number")
    if args.heaptrack_dir and not args.deep:
        parser.error("--heaptrack-dir requires --deep")
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        command = resolve_command(command)
        heaptrack = printer = None
        if args.deep:
            with Path(command[0]).open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    raise ValueError("--deep requires an ELF executable; pass the binary directly, not an env/shell wrapper")
            heaptrack, printer = find_heaptrack(args.heaptrack_dir)
        stamp = datetime.now().astimezone().strftime("%Y%m%d-%H%M%S-%f")
        out = (args.output or ROOT / "build/memory-monitor" / f"{stamp}-{os.getpid()}").resolve()
        if out.exists() and (not out.is_dir() or any(out.iterdir())):
            raise ValueError(f"output directory must be new or empty: {out}")
        out.mkdir(parents=True, exist_ok=True)
    except (ValueError, OSError) as exc:
        parser.error(str(exc))

    owned = OwnedProcess(command, out, heaptrack)
    recorder = Recorder(out)
    monitor = Monitor(owned, recorder, args.interval)
    old_handlers = {}
    for sig in (signal.SIGINT, signal.SIGTERM):
        old_handlers[sig] = signal.signal(sig, lambda signum, frame: monitor.request_stop("user_stop"))
    result = None
    try:
        (out / "metadata.json").write_text(json.dumps({"command": command, "started_at": monitor.started_at, "mode": "deep" if heaptrack else "normal", "interval_seconds": args.interval}, indent=2) + "\n")
        owned.start()
        plain = args.plain or not (sys.stdin.isatty() and sys.stdout.isatty()) or os.environ.get("TERM", "dumb") == "dumb"
        if not plain:
            try:
                import curses
            except ImportError as exc:
                monitor.warnings.append(f"Terminal UI unavailable; using plain output: {exc}")
                monitor.plain()
            else:
                try:
                    curses.wrapper(lambda screen: TerminalView(screen, monitor).run())
                except curses.error as exc:
                    monitor.warnings.append(f"Terminal UI unavailable; using plain output: {exc}")
                    monitor.plain()
        else:
            monitor.plain()
    except (OSError, ValueError, RuntimeError) as exc:
        monitor.error, monitor.stop_reason = str(exc), "error"
    finally:
        monitor.ended = time.monotonic()
        try:
            owned.close()
        except (OSError, subprocess.TimeoutExpired) as exc:
            monitor.error = monitor.error or f"cleanup failed: {exc}"
            monitor.stop_reason = "error"
        recorder.close()
    try:
        if args.deep and owned.process is not None:
            try:
                result = analyze_heap(printer, out, monitor)
            except OSError as exc:
                result = {"status": "error", "error": str(exc)}
        save_report(monitor, result, command)
        print("\nStopped. " + (monitor.error or f"{recorder.count} samples captured."))
        if recorder.latest:
            print("Last sample: " + " | ".join(f"{m.upper()} {format_bytes(recorder.latest.totals[m])}" for m in ("rss", "pss", "uss")))
            print("Largest RAM sources (RSS):")
            for key, values in sorted(recorder.latest.categories.items(), key=lambda row: -row[1]["rss"])[:8]:
                print(f"  {CATEGORIES[key]:<36} {format_bytes(values['rss']):>12}  PSS {format_bytes(values['pss'])}")
        if result:
            print(f"Heap analysis: {result['status']}" + (" | " + result["error"] if result.get("error") else ""))
            for name, amount in list(result.get("categories_bytes", {}).items())[:8]:
                print(f"  {name:<36} {format_bytes(amount):>12}")
        print(f"Report: {out / 'report.txt'}\nData:   {out / 'summary.json'}")
        if not recorder.count and owned.process is not None and owned.process.returncode:
            print("Target exited before sampling. See " + str(out / "application.log"), file=sys.stderr)
        return 1 if monitor.error or (result and result["status"] == "error") or (monitor.stop_reason == "target_exited" and owned.process and owned.process.returncode) else 0
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)


if __name__ == "__main__":
    sys.exit(main())
