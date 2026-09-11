#!/usr/bin/env python3
import os
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

BIN = os.environ.get("TELEMETRY_LAB_BIN", "build/telemetry_nonblocking_lab")
HEADER = struct.Struct("!4sBBHI")


def run_lab(*args, timeout=5):
    return subprocess.run([BIN, *args], capture_output=True, text=True, timeout=timeout, check=False)


def parse_kv(stdout):
    out = {}
    for line in stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


class NonblockingTelemetryTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        help_result = run_lab("--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn("--queue-bytes", help_result.stdout)
        self.assertIn("--runtime-ms", help_result.stdout)

        bad = run_lab("--socket", "/tmp/x", "--events", "0")
        self.assertNotEqual(bad.returncode, 0)

    def test_missing_collector_drops_without_waiting(self):
        path = f"/tmp/shp31-missing-{os.getpid()}.sock"
        start = time.monotonic()
        result = run_lab(
            "--socket", path,
            "--events", "20000",
            "--payload-bytes", "128",
            "--queue-bytes", "8192",
            "--runtime-ms", "50",
            timeout=3,
        )
        elapsed = time.monotonic() - start
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(elapsed, 1.0)
        self.assertIn("TELEMETRY_CONNECT_UNAVAILABLE", result.stdout)
        self.assertIn("PHASE31_NONBLOCKING_OK", result.stdout)
        stats = parse_kv(result.stdout)
        self.assertEqual(int(stats["DROPPED"]), 20000)
        self.assertEqual(int(stats["SENT"]), 0)
        self.assertLess(int(stats["ENQUEUE_ELAPSED_MS"]), 250)

    def test_slow_collector_forces_bounded_drop_not_blocking(self):
        with tempfile.TemporaryDirectory(prefix="shp31-") as tmp:
            path = os.path.join(tmp, "telemetry.sock")
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener.bind(path)
            listener.listen(1)

            accepted = threading.Event()

            def slow_server():
                conn, _ = listener.accept()
                accepted.set()
                time.sleep(0.8)
                conn.close()

            thread = threading.Thread(target=slow_server, daemon=True)
            thread.start()

            start = time.monotonic()
            result = run_lab(
                "--socket", path,
                "--events", "5000",
                "--payload-bytes", "1024",
                "--queue-bytes", "32768",
                "--runtime-ms", "250",
                "--send-buffer", "4096",
                timeout=4,
            )
            elapsed = time.monotonic() - start
            listener.close()
            thread.join(timeout=2)

            self.assertTrue(accepted.is_set())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertLess(elapsed, 1.5)
            self.assertIn("PHASE31_NONBLOCKING_OK", result.stdout)
            stats = parse_kv(result.stdout)
            self.assertGreater(int(stats["DROPPED"]), 0)
            self.assertGreater(int(stats["WOULD_BLOCK"]), 0)
            self.assertLessEqual(int(stats["QUEUED_BYTES"]), 32768)
            self.assertLess(int(stats["ENQUEUE_ELAPSED_MS"]), 300)

    def test_draining_collector_receives_all_frames(self):
        with tempfile.TemporaryDirectory(prefix="shp31-") as tmp:
            path = os.path.join(tmp, "telemetry.sock")
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener.bind(path)
            listener.listen(1)
            received = []
            server_error = []

            def exact_recv(conn, n):
                data = bytearray()
                while len(data) < n:
                    chunk = conn.recv(n - len(data))
                    if not chunk:
                        raise EOFError(f"wanted {n}, got {len(data)}")
                    data.extend(chunk)
                return bytes(data)

            def drain_server():
                try:
                    conn, _ = listener.accept()
                    with conn:
                        for _ in range(200):
                            raw = exact_recv(conn, HEADER.size)
                            magic, version, frame_type, flags, length = HEADER.unpack(raw)
                            self.assertEqual(magic, b"SHP1")
                            self.assertEqual(version, 1)
                            self.assertEqual(frame_type, 1)
                            self.assertEqual(flags, 0)
                            payload = exact_recv(conn, length)
                            received.append(payload)
                except Exception as exc:  # surfaced in main thread
                    server_error.append(exc)

            thread = threading.Thread(target=drain_server, daemon=True)
            thread.start()

            result = run_lab(
                "--socket", path,
                "--events", "200",
                "--payload-bytes", "256",
                "--queue-bytes", "262144",
                "--runtime-ms", "1500",
                timeout=5,
            )
            thread.join(timeout=3)
            listener.close()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(server_error, server_error)
            self.assertEqual(len(received), 200)
            stats = parse_kv(result.stdout)
            self.assertEqual(int(stats["SENT"]), 200)
            self.assertEqual(int(stats["DROPPED"]), 0)
            self.assertEqual(int(stats["QUEUED_FRAMES"]), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
