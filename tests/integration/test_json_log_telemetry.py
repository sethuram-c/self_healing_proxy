#!/usr/bin/env python3
import json
import os
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = os.environ.get("JSON_LOG_LAB_BIN", str(ROOT / "build" / "json_log_telemetry_lab"))
HEADER = struct.Struct("!4sBBHI")


def recv_exact(conn, count):
    data = bytearray()
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            raise AssertionError(f"EOF after {len(data)} of {count} bytes")
        data.extend(chunk)
    return bytes(data)


def collector(path, expected, output):
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    try:
        conn, _ = srv.accept()
        with conn:
            for _ in range(expected):
                header = recv_exact(conn, HEADER.size)
                magic, version, typ, flags, length = HEADER.unpack(header)
                payload = recv_exact(conn, length)
                output.append((magic, version, typ, flags, payload))
    finally:
        srv.close()


class JsonLogTelemetryTests(unittest.TestCase):
    def run_case(self, *, attempts=3, traceparent=None, backend="backend-a", message="attempt completed"):
        with tempfile.TemporaryDirectory(prefix="shp33-") as td:
            path = str(Path(td) / "collector.sock")
            frames = []
            thread = threading.Thread(target=collector, args=(path, attempts, frames), daemon=True)
            thread.start()
            deadline = time.time() + 2
            while not os.path.exists(path):
                self.assertLess(time.time(), deadline, "collector socket was not created")
                time.sleep(0.001)

            cmd = [LAB, "--socket", path, "--attempts", str(attempts), "--backend", backend, "--message", message]
            if traceparent is not None:
                cmd += ["--traceparent", traceparent]
            proc = subprocess.run(cmd, text=True, capture_output=True, timeout=5)
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(len(frames), attempts)
            docs = []
            for magic, version, typ, flags, raw in frames:
                self.assertEqual((magic, version, typ, flags), (b"SHP1", 1, 1, 0))
                text = raw.decode("utf-8")
                self.assertNotIn("\n", text)
                docs.append(json.loads(text))
            return proc, docs

    def assert_schema(self, docs, attempts):
        self.assertEqual(len(docs), attempts)
        request_ids = {d["request_id"] for d in docs}
        trace_ids = {d["trace_id"] for d in docs}
        span_ids = {d["span_id"] for d in docs}
        self.assertEqual(len(request_ids), 1)
        self.assertEqual(len(trace_ids), 1)
        self.assertEqual(len(span_ids), attempts)
        for index, doc in enumerate(docs, 1):
            self.assertEqual(doc["schema"], "shp.telemetry.v1")
            self.assertEqual(doc["event"], "backend_attempt")
            self.assertEqual(doc["attempt"], index)
            self.assertIsInstance(doc["timestamp_unix_ns"], int)
            self.assertGreater(doc["timestamp_unix_ns"], 0)
            self.assertRegex(doc["request_id"], r"^[0-9a-f]{32}$")
            self.assertRegex(doc["trace_id"], r"^[0-9a-f]{32}$")
            self.assertRegex(doc["span_id"], r"^[0-9a-f]{16}$")

    def test_generated_json_events_have_stable_correlation(self):
        proc, docs = self.run_case(attempts=3)
        self.assertIn("PHASE33_JSON_LOGGING_OK", proc.stdout)
        self.assertIn("SENT=3", proc.stdout)
        self.assertIn("DROPPED=0", proc.stdout)
        self.assert_schema(docs, 3)
        self.assertTrue(all(d["trace_source"] == "generated" for d in docs))
        self.assertEqual([d["result"] for d in docs], ["retry", "retry", "success"])

    def test_remote_trace_id_is_preserved(self):
        incoming = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
        _, docs = self.run_case(attempts=2, traceparent=incoming)
        self.assert_schema(docs, 2)
        self.assertEqual({d["trace_id"] for d in docs}, {"4bf92f3577b34da6a3ce929d0e0e4736"})
        self.assertTrue(all(d["trace_source"] == "remote" for d in docs))

    def test_hostile_strings_remain_one_valid_json_event(self):
        backend = 'be"\\nd\n\t\x01'
        message = 'line1\nline2 "quoted" \\ slash\tend'
        _, docs = self.run_case(attempts=1, backend=backend, message=message)
        self.assert_schema(docs, 1)
        self.assertEqual(docs[0]["backend"], backend)
        self.assertEqual(docs[0]["message"], message)

    def test_oversized_field_is_rejected_without_emitting_partial_json(self):
        with tempfile.TemporaryDirectory(prefix="shp33-over-") as td:
            path = str(Path(td) / "collector.sock")
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(path)
            srv.listen(1)
            try:
                proc = subprocess.Popen(
                    [LAB, "--socket", path, "--message", "x" * 300],
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                conn, _ = srv.accept()
                with conn:
                    conn.settimeout(1)
                    data = conn.recv(4096)
                stdout, stderr = proc.communicate(timeout=2)
                self.assertNotEqual(proc.returncode, 0, stdout)
                self.assertIn("json_log_serialize", stderr)
                self.assertEqual(data, b"")
            finally:
                srv.close()

    def test_help_and_invalid_cli(self):
        help_proc = subprocess.run([LAB, "--help"], text=True, capture_output=True, timeout=2)
        self.assertEqual(help_proc.returncode, 0)
        bad = subprocess.run([LAB, "--attempts", "0"], text=True, capture_output=True, timeout=2)
        self.assertNotEqual(bad.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
