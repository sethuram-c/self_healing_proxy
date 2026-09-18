#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import statistics
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
LAB = Path(os.environ.get("TELEMETRY_OVERHEAD_LAB_BIN", str(ROOT / "build/telemetry_overhead_lab")))
QUEUE_BYTES = 64 * 1024
SAMPLES = int(os.environ.get("PHASE38_SAMPLES", "5"))

BUDGETS = {
    "missing_wall_ns_per_event": 50_000,
    "draining_wall_ns_per_event": 100_000,
    "stalled_wall_ns_per_event": 100_000,
    "rss_delta_bytes": 16 * 1024 * 1024,
}


def parse(text):
    result = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            result[k] = v
    return result


def run(*args):
    proc = subprocess.run([str(LAB), *args], cwd=ROOT, text=True, capture_output=True, check=True, timeout=10)
    if "PHASE38_OVERHEAD_SAMPLE_OK" not in proc.stdout:
        raise RuntimeError(proc.stdout + proc.stderr)
    return parse(proc.stdout)


class Collector:
    def __init__(self, path, drain):
        self.path = Path(path)
        self.drain = drain
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        if not self.ready.wait(3):
            raise RuntimeError("collector not ready")
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop.set()
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.settimeout(0.1)
                s.connect(str(self.path))
        except OSError:
            pass
        self.thread.join(timeout=3)
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass

    def _serve(self):
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(str(self.path))
        srv.listen(4)
        srv.settimeout(0.2)
        self.ready.set()
        try:
            while not self.stop.is_set():
                try:
                    conn, _ = srv.accept()
                except socket.timeout:
                    continue
                conn.settimeout(0.1)
                try:
                    if self.drain:
                        while not self.stop.is_set():
                            try:
                                if not conn.recv(65536):
                                    break
                            except socket.timeout:
                                continue
                    else:
                        self.stop.wait(1.0)
                finally:
                    conn.close()
        finally:
            srv.close()


def median_int(samples, key):
    return int(statistics.median(int(s[key]) for s in samples))


def main():
    disabled = [run("--mode", "disabled", "--events", "100000", "--payload-bytes", "256") for _ in range(SAMPLES)]

    with tempfile.TemporaryDirectory(prefix="shp38-bench-") as td:
        td = Path(td)
        missing_path = td / "missing.sock"
        missing = [run("--mode", "telemetry", "--socket", str(missing_path), "--events", "100000", "--payload-bytes", "256", "--queue-bytes", str(QUEUE_BYTES), "--runtime-ms", "10") for _ in range(SAMPLES)]

        drain_samples = []
        for i in range(SAMPLES):
            path = td / f"drain-{i}.sock"
            with Collector(path, True):
                drain_samples.append(run("--mode", "telemetry", "--socket", str(path), "--events", "5000", "--payload-bytes", "256", "--queue-bytes", str(4 * 1024 * 1024), "--runtime-ms", "1500", "--send-buffer", "65536"))

        stall_samples = []
        for i in range(SAMPLES):
            path = td / f"stall-{i}.sock"
            with Collector(path, False):
                stall_samples.append(run("--mode", "telemetry", "--socket", str(path), "--events", "50000", "--payload-bytes", "1024", "--queue-bytes", str(QUEUE_BYTES), "--runtime-ms", "100", "--send-buffer", "1024"))

    disabled_wall = median_int(disabled, "WALL_NS_PER_EVENT")
    missing_wall = median_int(missing, "WALL_NS_PER_EVENT")
    missing_cpu = median_int(missing, "CPU_NS_PER_EVENT")
    draining_wall = median_int(drain_samples, "WALL_NS_PER_EVENT")
    draining_cpu = median_int(drain_samples, "CPU_NS_PER_EVENT")
    stalled_wall = median_int(stall_samples, "WALL_NS_PER_EVENT")
    stalled_cpu = median_int(stall_samples, "CPU_NS_PER_EVENT")
    stalled_peak = max(int(s["PEAK_QUEUE_BYTES"]) for s in stall_samples)
    stalled_drop_min = min(int(s["DROPPED"]) for s in stall_samples)
    rss_max = max(int(s["RSS_DELTA_BYTES"]) for s in missing + drain_samples + stall_samples)

    failures = []
    if missing_wall > BUDGETS["missing_wall_ns_per_event"]:
        failures.append("missing collector wall budget")
    if draining_wall > BUDGETS["draining_wall_ns_per_event"]:
        failures.append("draining collector wall budget")
    if stalled_wall > BUDGETS["stalled_wall_ns_per_event"]:
        failures.append("stalled collector wall budget")
    if stalled_peak > QUEUE_BYTES:
        failures.append("queue memory bound")
    if stalled_drop_min <= 0:
        failures.append("stalled collector did not trigger drop policy")
    if rss_max > BUDGETS["rss_delta_bytes"]:
        failures.append("RSS growth budget")

    print("PHASE38_BENCHMARK_REPORT")
    print(f"SAMPLES={SAMPLES}")
    print(f"DISABLED_MEDIAN_WALL_NS_PER_EVENT={disabled_wall}")
    print(f"MISSING_MEDIAN_WALL_NS_PER_EVENT={missing_wall}")
    print(f"MISSING_MEDIAN_CPU_NS_PER_EVENT={missing_cpu}")
    print(f"MISSING_INCREMENTAL_WALL_NS_PER_EVENT={max(0, missing_wall - disabled_wall)}")
    print(f"DRAINING_MEDIAN_WALL_NS_PER_EVENT={draining_wall}")
    print(f"DRAINING_MEDIAN_CPU_NS_PER_EVENT={draining_cpu}")
    print(f"STALLED_MEDIAN_WALL_NS_PER_EVENT={stalled_wall}")
    print(f"STALLED_MEDIAN_CPU_NS_PER_EVENT={stalled_cpu}")
    print(f"STALLED_MAX_QUEUE_BYTES={stalled_peak}")
    print(f"STALLED_MIN_DROPPED={stalled_drop_min}")
    print(f"MAX_RSS_DELTA_BYTES={rss_max}")
    print(f"BUDGET_MISSING_WALL_NS_PER_EVENT={BUDGETS['missing_wall_ns_per_event']}")
    print(f"BUDGET_DRAINING_WALL_NS_PER_EVENT={BUDGETS['draining_wall_ns_per_event']}")
    print(f"BUDGET_STALLED_WALL_NS_PER_EVENT={BUDGETS['stalled_wall_ns_per_event']}")
    print(f"BUDGET_QUEUE_BYTES={QUEUE_BYTES}")
    print(f"BUDGET_RSS_DELTA_BYTES={BUDGETS['rss_delta_bytes']}")

    if failures:
        print("PHASE38_BENCHMARK_FAILED=" + ",".join(failures))
        raise SystemExit(1)
    print("PHASE38_BENCHMARK_OK")


if __name__ == "__main__":
    main()
