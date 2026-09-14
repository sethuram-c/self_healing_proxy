#!/usr/bin/env python3
import http.server
import json
import os
import pathlib
import socket
import stat
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"
PRODUCER = pathlib.Path(os.environ.get("JSON_LOG_LAB_BIN", ROOT / "build" / "json_log_telemetry_lab"))
COLLECTOR_LAB = ROOT / "scripts" / "otel_collector_lab.sh"


class CaptureState:
    def __init__(self, status=200):
        self.status = status
        self.requests = []
        self.lock = threading.Lock()


class CaptureHandler(http.server.BaseHTTPRequestHandler):
    state = None

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            doc = json.loads(raw.decode("utf-8"))
        except Exception:
            doc = None
        with self.state.lock:
            self.state.requests.append(
                {
                    "path": self.path,
                    "content_type": self.headers.get("Content-Type"),
                    "doc": doc,
                }
            )
        self.send_response(self.state.status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b"{}")

    def log_message(self, *_args):
        pass


def start_http_capture(status=200):
    state = CaptureState(status)
    handler = type("Handler", (CaptureHandler,), {})
    handler.state = state
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return state, server, thread


def wait_for_socket(path, proc, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            out, err = proc.communicate(timeout=1)
            raise AssertionError(f"bridge exited early rc={proc.returncode}\nstdout={out}\nstderr={err}")
        try:
            if stat.S_ISSOCK(os.stat(path).st_mode):
                return
        except FileNotFoundError:
            pass
        time.sleep(0.01)
    raise AssertionError(f"bridge socket never appeared: {path}")


def run_producer(socket_path, attempts=3):
    return subprocess.run(
        [str(PRODUCER), "--socket", str(socket_path), "--attempts", str(attempts)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=10,
    )


class OTelBridgeIntegrationTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        help_run = subprocess.run(
            ["python3", str(BRIDGE), "--help"], cwd=ROOT, text=True, capture_output=True, timeout=5
        )
        self.assertEqual(help_run.returncode, 0, help_run.stderr)
        self.assertIn("OTLP/HTTP", help_run.stdout)

        bad = subprocess.run(
            ["python3", str(BRIDGE), "--socket", "/tmp/x", "--otlp-endpoint", "ftp://bad"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=5,
        )
        self.assertNotEqual(bad.returncode, 0)

    def test_phase33_frames_become_valid_otlp_http_json(self):
        state, server, thread = start_http_capture(200)
        try:
            with tempfile.TemporaryDirectory() as td:
                sock = pathlib.Path(td) / "bridge.sock"
                endpoint = f"http://127.0.0.1:{server.server_port}/v1/logs"
                bridge_proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(sock), "--otlp-endpoint", endpoint,
                        "--max-events", "3", "--require-export",
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                wait_for_socket(sock, bridge_proc)
                producer = run_producer(sock, attempts=3)
                self.assertEqual(producer.returncode, 0, producer.stderr)
                self.assertIn("PHASE33_JSON_LOGGING_OK", producer.stdout)
                out, err = bridge_proc.communicate(timeout=10)
                self.assertEqual(bridge_proc.returncode, 0, err)
                self.assertIn("EXPORTED=3", out)
                self.assertIn("EXPORT_FAILED=0", out)

                with state.lock:
                    requests = list(state.requests)
                self.assertEqual(len(requests), 3)
                request_ids = set()
                trace_ids = set()
                span_ids = set()
                for request in requests:
                    self.assertEqual(request["path"], "/v1/logs")
                    self.assertEqual(request["content_type"], "application/json")
                    doc = request["doc"]
                    record = doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]
                    body_event = json.loads(record["body"]["stringValue"])
                    self.assertEqual(record["traceId"], body_event["trace_id"])
                    self.assertEqual(record["spanId"], body_event["span_id"])
                    self.assertIsInstance(record["timeUnixNano"], str)
                    request_ids.add(body_event["request_id"])
                    trace_ids.add(body_event["trace_id"])
                    span_ids.add(body_event["span_id"])
                self.assertEqual(len(request_ids), 1)
                self.assertEqual(len(trace_ids), 1)
                self.assertEqual(len(span_ids), 3)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

    def test_otlp_failure_isolated_from_phase31_producer(self):
        state, server, thread = start_http_capture(503)
        try:
            with tempfile.TemporaryDirectory() as td:
                sock = pathlib.Path(td) / "bridge.sock"
                endpoint = f"http://127.0.0.1:{server.server_port}/v1/logs"
                bridge_proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(sock), "--otlp-endpoint", endpoint,
                        "--max-events", "2", "--request-timeout-ms", "200",
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                wait_for_socket(sock, bridge_proc)
                producer = run_producer(sock, attempts=2)
                self.assertEqual(producer.returncode, 0, producer.stderr)
                self.assertIn("SENT=2", producer.stdout)
                out, err = bridge_proc.communicate(timeout=10)
                self.assertEqual(bridge_proc.returncode, 0, err)
                self.assertIn("EXPORTED=0", out)
                self.assertIn("EXPORT_FAILED=2", out)
                with state.lock:
                    self.assertEqual(len(state.requests), 2)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

    def test_real_opentelemetry_collector(self):
        if os.environ.get("PHASE34_REQUIRE_REAL") != "1":
            self.skipTest("real OpenTelemetry Collector test requires PHASE34_REQUIRE_REAL=1")

        port = os.environ.get("PHASE34_OTLP_HTTP_PORT", "14318")
        env = os.environ.copy()
        env["PHASE34_OTLP_HTTP_PORT"] = port
        subprocess.run([str(COLLECTOR_LAB), "stop"], cwd=ROOT, env=env, capture_output=True, text=True)
        try:
            start = subprocess.run(
                [str(COLLECTOR_LAB), "start"], cwd=ROOT, env=env, text=True, capture_output=True, timeout=30
            )
            self.assertEqual(start.returncode, 0, start.stderr)
            self.assertIn("PHASE34_COLLECTOR_READY", start.stdout)

            with tempfile.TemporaryDirectory() as td:
                sock = pathlib.Path(td) / "bridge.sock"
                bridge_proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(sock),
                        "--otlp-endpoint", f"http://127.0.0.1:{port}/v1/logs",
                        "--max-events", "3", "--require-export", "--request-timeout-ms", "1000",
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                wait_for_socket(sock, bridge_proc, timeout=5)
                producer = run_producer(sock, attempts=3)
                self.assertEqual(producer.returncode, 0, producer.stderr)
                out, err = bridge_proc.communicate(timeout=10)
                self.assertEqual(bridge_proc.returncode, 0, err)
                self.assertIn("EXPORTED=3", out)
                self.assertIn("EXPORT_FAILED=0", out)
        finally:
            subprocess.run([str(COLLECTOR_LAB), "stop"], cwd=ROOT, env=env, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
