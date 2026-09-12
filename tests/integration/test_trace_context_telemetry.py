#!/usr/bin/env python3
import os
import re
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = os.environ.get("TRACE_CONTEXT_LAB_BIN", str(ROOT / "build" / "trace_context_telemetry_lab"))
HEADER = struct.Struct("!4sBBHI")


def recv_exact(conn, count):
    data = bytearray()
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            raise AssertionError(f"EOF after {len(data)} of {count} bytes")
        data.extend(chunk)
    return bytes(data)


def collect_frames(path, attempts, output):
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    try:
        conn, _ = srv.accept()
        with conn:
            for _ in range(attempts):
                raw = recv_exact(conn, HEADER.size)
                magic, version, typ, flags, length = HEADER.unpack(raw)
                payload = recv_exact(conn, length).decode("ascii")
                output.append((magic, version, typ, flags, payload))
    finally:
        srv.close()


def parse_payload(payload):
    fields = {}
    for token in payload.split():
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


class TraceContextTelemetryTests(unittest.TestCase):
    def run_case(self, incoming, attempts=3):
        with tempfile.TemporaryDirectory(prefix="shp32-") as td:
            path = str(Path(td) / "collector.sock")
            frames = []
            thread = threading.Thread(target=collect_frames, args=(path, attempts, frames), daemon=True)
            thread.start()
            while not os.path.exists(path):
                pass
            cmd = [LAB, "--socket", path, "--attempts", str(attempts)]
            if incoming is not None:
                cmd += ["--traceparent", incoming]
            proc = subprocess.run(cmd, text=True, capture_output=True, timeout=5)
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(len(frames), attempts)
            return proc, [parse_payload(item[4]) for item in frames], frames

    def assert_common_correlation(self, payloads, attempts):
        request_ids = {p["request_id"] for p in payloads}
        trace_ids = {p["trace_id"] for p in payloads}
        span_ids = {p["span_id"] for p in payloads}
        self.assertEqual(len(request_ids), 1)
        self.assertEqual(len(trace_ids), 1)
        self.assertEqual(len(span_ids), attempts)
        self.assertEqual([int(p["attempt"]) for p in payloads], list(range(1, attempts + 1)))
        for p in payloads:
            self.assertRegex(p["request_id"], r"^[0-9a-f]{32}$")
            self.assertRegex(p["trace_id"], r"^[0-9a-f]{32}$")
            self.assertRegex(p["span_id"], r"^[0-9a-f]{16}$")
            self.assertRegex(p["traceparent"], r"^00-[0-9a-f]{32}-[0-9a-f]{16}-[0-9a-f]{2}$")

    def test_generated_identity_stays_stable_across_attempts(self):
        proc, payloads, frames = self.run_case(None, 3)
        self.assertIn("PHASE32_CORRELATION_OK", proc.stdout)
        self.assertIn("TRACE_SOURCE=generated", proc.stdout)
        self.assert_common_correlation(payloads, 3)
        for magic, version, typ, flags, _ in frames:
            self.assertEqual((magic, version, typ, flags), (b"SHP1", 1, 1, 0))
        self.assertTrue(all(p["source"] == "generated" for p in payloads))

    def test_valid_remote_traceparent_is_preserved(self):
        incoming = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
        proc, payloads, _ = self.run_case(incoming, 3)
        self.assertIn("TRACE_SOURCE=remote", proc.stdout)
        self.assert_common_correlation(payloads, 3)
        self.assertEqual({p["trace_id"] for p in payloads}, {"4bf92f3577b34da6a3ce929d0e0e4736"})
        self.assertTrue(all(p["traceparent"].endswith("-01") for p in payloads))
        self.assertTrue(all(p["source"] == "remote" for p in payloads))

    def test_invalid_remote_traceparent_falls_back_safely(self):
        incoming = "00-00000000000000000000000000000000-00f067aa0ba902b7-01"
        proc, payloads, _ = self.run_case(incoming, 2)
        self.assertIn("TRACE_SOURCE=rejected", proc.stdout)
        self.assert_common_correlation(payloads, 2)
        self.assertNotEqual(payloads[0]["trace_id"], "0" * 32)
        self.assertTrue(all(p["source"] == "rejected" for p in payloads))

    def test_help_and_invalid_cli(self):
        help_proc = subprocess.run([LAB, "--help"], text=True, capture_output=True, timeout=2)
        self.assertEqual(help_proc.returncode, 0)
        bad = subprocess.run([LAB, "--attempts", "0"], text=True, capture_output=True, timeout=2)
        self.assertNotEqual(bad.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
