#!/usr/bin/env python3
import http.server
import json
import os
import pathlib
import socket
import stat
import struct
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"
COLLECTOR_LAB = ROOT / "scripts" / "otel_collector_lab.sh"
HEADER = struct.Struct("!4sBBHI")


def make_event(attempt, result):
    return {
        "schema": "shp.telemetry.v1",
        "timestamp_unix_ns": time.time_ns(),
        "event": "backend_attempt",
        "request_id": "0123456789abcdef0123456789abcdef",
        "trace_id": "4bf92f3577b34da6a3ce929d0e0e4736",
        "span_id": f"{attempt:016x}",
        "trace_source": "generated",
        "attempt": attempt,
        "backend": "backend-a",
        "result": result,
        "message": "phase35 metrics",
    }


def send_events(path, events):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(str(path))
    for event in events:
        payload = json.dumps(event, separators=(",", ":")).encode()
        sock.sendall(HEADER.pack(b"SHP1", 1, 1, 0, len(payload)) + payload)
    sock.close()


def wait_for_socket(path, proc, timeout=4.0):
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
    raise AssertionError("bridge socket did not appear")


class CaptureState:
    def __init__(self, metrics_status=200):
        self.metrics_status = metrics_status
        self.requests = []
        self.lock = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    state = None

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        doc = json.loads(raw.decode())
        with self.state.lock:
            self.state.requests.append((self.path, doc))
        status = self.state.metrics_status if self.path == "/v1/metrics" else 200
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b"{}")

    def log_message(self, *_args):
        pass


def start_server(metrics_status=200):
    state = CaptureState(metrics_status)
    cls = type("CaptureHandler", (Handler,), {})
    cls.state = state
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), cls)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return state, server, thread


class OTelMetricsIntegrationTests(unittest.TestCase):
    def test_three_events_export_cumulative_metrics(self):
        state, server, thread = start_server()
        try:
            with tempfile.TemporaryDirectory() as td:
                uds = pathlib.Path(td) / "bridge.sock"
                base = f"http://127.0.0.1:{server.server_port}"
                proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(uds),
                        "--otlp-endpoint", base + "/v1/logs",
                        "--metrics-export-every", "3", "--max-events", "3",
                        "--require-export", "--require-metrics-export",
                    ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                wait_for_socket(uds, proc)
                send_events(uds, [make_event(1, "retry"), make_event(2, "retry"), make_event(3, "success")])
                out, err = proc.communicate(timeout=10)
                self.assertEqual(proc.returncode, 0, err)
                self.assertIn("METRIC_REQUESTS_TOTAL=1", out)
                self.assertIn("METRIC_RETRIES_TOTAL=2", out)
                self.assertIn("METRICS_EXPORTED=1", out)

                with state.lock:
                    reqs = list(state.requests)
                self.assertEqual(sum(path == "/v1/logs" for path, _ in reqs), 3)
                metrics_docs = [doc for path, doc in reqs if path == "/v1/metrics"]
                self.assertEqual(len(metrics_docs), 1)
                metrics = metrics_docs[0]["resourceMetrics"][0]["scopeMetrics"][0]["metrics"]
                by_name = {m["name"]: m for m in metrics}
                self.assertEqual(by_name["shp.requests"]["sum"]["dataPoints"][0]["asInt"], "1")
                self.assertEqual(by_name["shp.retries"]["sum"]["dataPoints"][0]["asInt"], "2")
                attempts = by_name["shp.backend.attempts"]["sum"]["dataPoints"]
                values = {}
                for point in attempts:
                    attrs = {a["key"]: a["value"]["stringValue"] for a in point.get("attributes", [])}
                    values[(attrs["backend"], attrs["result"])] = point["asInt"]
                self.assertEqual(values[("backend-a", "retry")], "2")
                self.assertEqual(values[("backend-a", "success")], "1")
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=2)

    def test_metrics_failure_does_not_break_log_ingestion(self):
        state, server, thread = start_server(metrics_status=503)
        try:
            with tempfile.TemporaryDirectory() as td:
                uds = pathlib.Path(td) / "bridge.sock"
                base = f"http://127.0.0.1:{server.server_port}"
                proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(uds),
                        "--otlp-endpoint", base + "/v1/logs",
                        "--metrics-export-every", "2", "--max-events", "2",
                    ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                wait_for_socket(uds, proc)
                send_events(uds, [make_event(1, "retry"), make_event(2, "success")])
                out, err = proc.communicate(timeout=10)
                self.assertEqual(proc.returncode, 0, err)
                self.assertIn("EXPORTED=2", out)
                self.assertIn("METRICS_EXPORT_FAILED=1", out)
                with state.lock:
                    self.assertEqual(sum(path == "/v1/logs" for path, _ in state.requests), 2)
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=2)

    def test_real_collector_accepts_metrics(self):
        if os.environ.get("PHASE35_REQUIRE_REAL") != "1":
            self.skipTest("real OpenTelemetry Collector metrics test requires PHASE35_REQUIRE_REAL=1")
        port = os.environ.get("PHASE34_OTLP_HTTP_PORT", "14318")
        env = os.environ.copy(); env["PHASE34_OTLP_HTTP_PORT"] = port
        subprocess.run([str(COLLECTOR_LAB), "stop"], cwd=ROOT, env=env, capture_output=True, text=True)
        try:
            start = subprocess.run([str(COLLECTOR_LAB), "start"], cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(start.returncode, 0, start.stderr)
            with tempfile.TemporaryDirectory() as td:
                uds = pathlib.Path(td) / "bridge.sock"
                proc = subprocess.Popen(
                    [
                        "python3", str(BRIDGE), "--socket", str(uds),
                        "--otlp-endpoint", f"http://127.0.0.1:{port}/v1/logs",
                        "--metrics-export-every", "3", "--max-events", "3",
                        "--require-export", "--require-metrics-export", "--request-timeout-ms", "1000",
                    ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                wait_for_socket(uds, proc, timeout=5)
                send_events(uds, [make_event(1, "retry"), make_event(2, "retry"), make_event(3, "success")])
                out, err = proc.communicate(timeout=10)
                self.assertEqual(proc.returncode, 0, err)
                self.assertIn("METRICS_EXPORTED=1", out)
                self.assertIn("METRICS_EXPORT_FAILED=0", out)
        finally:
            subprocess.run([str(COLLECTOR_LAB), "stop"], cwd=ROOT, env=env, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
