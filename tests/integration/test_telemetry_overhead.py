#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("TELEMETRY_OVERHEAD_LAB_BIN", str(ROOT / "build/telemetry_overhead_lab")))

# These are intentionally generous failure budgets, not performance trophies.
MAX_MISSING_WALL_NS_PER_EVENT = 50_000
MAX_DRAINING_WALL_NS_PER_EVENT = 100_000
MAX_STALLED_WALL_NS_PER_EVENT = 100_000
MAX_RSS_DELTA_BYTES = 16 * 1024 * 1024
QUEUE_BYTES = 64 * 1024


def parse_output(text: str) -> dict[str, str]:
    values = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def run_lab(*args: str, timeout: float = 8.0) -> dict[str, str]:
    result = subprocess.run(
        [str(LAB), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=True,
    )
    if "PHASE38_OVERHEAD_SAMPLE_OK" not in result.stdout:
        raise AssertionError(result.stdout + result.stderr)
    return parse_output(result.stdout)


class UnixCollector:
    def __init__(self, path: Path, drain: bool):
        self.path = path
        self.drain = drain
        self.ready = threading.Event()
        self.stop = threading.Event()
        self.accepted = threading.Event()
        self.bytes_read = 0
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()
        if not self.ready.wait(3):
            raise RuntimeError("collector did not become ready")

    def close(self):
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

    def _run(self):
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
                self.accepted.set()
                conn.settimeout(0.1)
                try:
                    if self.drain:
                        while not self.stop.is_set():
                            try:
                                data = conn.recv(65536)
                            except socket.timeout:
                                continue
                            if not data:
                                break
                            self.bytes_read += len(data)
                    else:
                        while not self.stop.wait(0.05):
                            pass
                finally:
                    conn.close()
        finally:
            srv.close()


class TelemetryOverheadTests(unittest.TestCase):
    def test_cli_contract(self):
        help_result = subprocess.run([str(LAB), "--help"], cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("--mode disabled|telemetry", help_result.stdout)

        bad = subprocess.run([str(LAB), "--mode", "telemetry"], cwd=ROOT, text=True, capture_output=True)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("--socket is required", bad.stderr)

    def test_disabled_baseline_is_measurable(self):
        values = run_lab("--mode", "disabled", "--events", "100000", "--payload-bytes", "256")
        self.assertEqual(values["MODE"], "disabled")
        self.assertEqual(int(values["EVENTS"]), 100000)
        self.assertEqual(int(values["DROPPED"]), 0)
        self.assertGreater(int(values["PRODUCER_WALL_NS"]), 0)
        self.assertGreater(int(values["PRODUCER_CPU_NS"]), 0)

    def test_missing_collector_has_bounded_fast_failure(self):
        with tempfile.TemporaryDirectory(prefix="shp38-missing-") as td:
            missing = Path(td) / "missing.sock"
            values = run_lab(
                "--mode", "telemetry",
                "--socket", str(missing),
                "--events", "100000",
                "--payload-bytes", "256",
                "--queue-bytes", str(QUEUE_BYTES),
                "--runtime-ms", "10",
            )
        self.assertEqual(int(values["CONNECT_FAILURES"]), 1)
        self.assertEqual(int(values["DROPPED"]), 100000)
        self.assertEqual(int(values["PEAK_QUEUE_BYTES"]), 0)
        self.assertLessEqual(int(values["WALL_NS_PER_EVENT"]), MAX_MISSING_WALL_NS_PER_EVENT)
        self.assertLessEqual(int(values["RSS_DELTA_BYTES"]), MAX_RSS_DELTA_BYTES)

    def test_draining_collector_delivers_without_unbounded_cost(self):
        with tempfile.TemporaryDirectory(prefix="shp38-drain-") as td:
            sock = Path(td) / "collector.sock"
            collector = UnixCollector(sock, drain=True)
            collector.start()
            try:
                values = run_lab(
                    "--mode", "telemetry",
                    "--socket", str(sock),
                    "--events", "5000",
                    "--payload-bytes", "256",
                    "--queue-bytes", str(4 * 1024 * 1024),
                    "--runtime-ms", "1500",
                    "--send-buffer", "65536",
                )
            finally:
                collector.close()
        self.assertEqual(int(values["DROPPED"]), 0)
        self.assertEqual(int(values["SENT"]), 5000)
        self.assertEqual(int(values["QUEUED_BYTES"]), 0)
        self.assertLessEqual(int(values["WALL_NS_PER_EVENT"]), MAX_DRAINING_WALL_NS_PER_EVENT)
        self.assertLessEqual(int(values["RSS_DELTA_BYTES"]), MAX_RSS_DELTA_BYTES)

    def test_stalled_collector_drops_instead_of_blocking_or_growing(self):
        with tempfile.TemporaryDirectory(prefix="shp38-stall-") as td:
            sock = Path(td) / "collector.sock"
            collector = UnixCollector(sock, drain=False)
            collector.start()
            try:
                started = time.monotonic()
                values = run_lab(
                    "--mode", "telemetry",
                    "--socket", str(sock),
                    "--events", "50000",
                    "--payload-bytes", "1024",
                    "--queue-bytes", str(QUEUE_BYTES),
                    "--runtime-ms", "100",
                    "--send-buffer", "1024",
                )
                elapsed = time.monotonic() - started
            finally:
                collector.close()
        self.assertLess(elapsed, 8.0)
        self.assertGreater(int(values["DROPPED"]), 0)
        self.assertGreater(int(values["WOULD_BLOCK"]), 0)
        self.assertLessEqual(int(values["PEAK_QUEUE_BYTES"]), QUEUE_BYTES)
        self.assertLessEqual(int(values["QUEUED_BYTES"]), QUEUE_BYTES)
        self.assertLessEqual(int(values["WALL_NS_PER_EVENT"]), MAX_STALLED_WALL_NS_PER_EVENT)
        self.assertLessEqual(int(values["RSS_DELTA_BYTES"]), MAX_RSS_DELTA_BYTES)


if __name__ == "__main__":
    unittest.main(verbosity=2)
