#!/usr/bin/env python3
import http.server
import json
import os
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"
LAB = Path(os.environ.get("TRACE_SPAN_LAB_BIN", ROOT / "build" / "trace_span_telemetry_lab"))
COLLECTOR_SCRIPT = ROOT / "scripts" / "otel_collector_lab.sh"


class Recorder:
    def __init__(self, trace_status=200):
        self.trace_status = trace_status
        self.requests = []
        self.lock = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    recorder = None

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        with self.recorder.lock:
            self.recorder.requests.append((self.path, json.loads(body.decode())))
        status = self.recorder.trace_status if self.path == "/v1/traces" else 200
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b"{}")

    def log_message(self, *_args):
        pass


def start_http(recorder):
    Handler.recorder = recorder
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def wait_socket(path):
    deadline = time.time() + 3
    while time.time() < deadline:
        if Path(path).exists():
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                s.close()
                return
            except OSError:
                pass
        time.sleep(0.02)
    raise AssertionError("bridge socket not ready")


def run_case(trace_status=200, require=False, traceparent=None):
    recorder = Recorder(trace_status)
    server, thread = start_http(recorder)
    with tempfile.TemporaryDirectory(prefix="shp36-") as td:
        sock = str(Path(td) / "bridge.sock")
        endpoint = f"http://127.0.0.1:{server.server_address[1]}/v1/logs"
        cmd = ["python3", str(BRIDGE), "--socket", sock, "--otlp-endpoint", endpoint, "--max-events", "1"]
        if require:
            cmd.append("--require-trace-export")
        bridge = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            wait_socket(sock)
            lab_cmd = [str(LAB), "--socket", sock, "--attempts", "3", "--attempt-ms", "1", "--backend", "backend-a"]
            if traceparent:
                lab_cmd += ["--traceparent", traceparent]
            lab = subprocess.run(lab_cmd, cwd=ROOT, text=True, capture_output=True, timeout=5)
            out, err = bridge.communicate(timeout=5)
        finally:
            if bridge.poll() is None:
                bridge.terminate()
                bridge.wait(timeout=2)
            server.shutdown(); server.server_close(); thread.join(timeout=2)
        return lab, bridge.returncode, out, err, recorder.requests, sock


class OTelTraceIntegrationTests(unittest.TestCase):
    def test_trace_batch_becomes_parented_otlp_spans(self):
        lab, rc, out, err, reqs, sock = run_case()
        self.assertEqual(lab.returncode, 0, lab.stderr)
        self.assertEqual(rc, 0, err)
        traces = [doc for path, doc in reqs if path == "/v1/traces"]
        self.assertEqual(len(traces), 1)
        spans = traces[0]["resourceSpans"][0]["scopeSpans"][0]["spans"]
        self.assertEqual(len(spans), 4)
        root = spans[0]
        children = spans[1:]
        self.assertEqual(root["name"], "proxy.request")
        self.assertEqual(root["kind"], 2)
        self.assertNotIn("parentSpanId", root)
        self.assertEqual(len({sp["traceId"] for sp in spans}), 1)
        self.assertEqual(len({sp["spanId"] for sp in spans}), 4)
        self.assertTrue(all(sp["parentSpanId"] == root["spanId"] for sp in children))
        self.assertEqual([sp["status"]["code"] for sp in children], [2, 2, 1])
        self.assertTrue(all(int(sp["endTimeUnixNano"]) > int(sp["startTimeUnixNano"]) for sp in spans))
        self.assertIn("PHASE36_TRACE_EXPORT_OK", out)
        self.assertFalse(Path(sock).exists())

    def test_remote_parent_is_preserved(self):
        tp = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
        lab, rc, _out, err, reqs, _sock = run_case(traceparent=tp)
        self.assertEqual(lab.returncode, 0, lab.stderr)
        self.assertEqual(rc, 0, err)
        spans = [doc for path, doc in reqs if path == "/v1/traces"][0]["resourceSpans"][0]["scopeSpans"][0]["spans"]
        self.assertEqual({sp["traceId"] for sp in spans}, {"4bf92f3577b34da6a3ce929d0e0e4736"})
        self.assertEqual(spans[0]["parentSpanId"], "00f067aa0ba902b7")

    def test_trace_export_failure_is_isolated_unless_required(self):
        lab, rc, out, _err, _reqs, _sock = run_case(trace_status=503, require=False)
        self.assertEqual(lab.returncode, 0, lab.stderr)
        self.assertEqual(rc, 0)
        self.assertIn("TRACES_EXPORT_FAILED=1", out)
        lab, rc, _out, _err, _reqs, _sock = run_case(trace_status=503, require=True)
        self.assertEqual(lab.returncode, 0, lab.stderr)
        self.assertNotEqual(rc, 0)

    @unittest.skipUnless(os.environ.get("PHASE36_REQUIRE_REAL") == "1", "real OpenTelemetry Collector trace test requires PHASE36_REQUIRE_REAL=1")
    def test_real_collector_accepts_traces(self):
        subprocess.run([str(COLLECTOR_SCRIPT), "stop"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        start = subprocess.run([str(COLLECTOR_SCRIPT), "start"], cwd=ROOT, text=True, capture_output=True, timeout=30)
        self.assertEqual(start.returncode, 0, start.stderr)
        try:
            with tempfile.TemporaryDirectory(prefix="shp36-real-") as td:
                sock = str(Path(td) / "bridge.sock")
                bridge = subprocess.Popen([
                    "python3", str(BRIDGE), "--socket", sock,
                    "--otlp-endpoint", "http://127.0.0.1:14318/v1/logs",
                    "--max-events", "1", "--require-trace-export"], cwd=ROOT,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                wait_socket(sock)
                lab = subprocess.run([str(LAB), "--socket", sock, "--attempts", "3", "--attempt-ms", "1"], cwd=ROOT, text=True, capture_output=True, timeout=5)
                out, err = bridge.communicate(timeout=8)
                self.assertEqual(lab.returncode, 0, lab.stderr)
                self.assertEqual(bridge.returncode, 0, err)
                self.assertIn("PHASE36_TRACE_EXPORT_OK", out)
        finally:
            subprocess.run([str(COLLECTOR_SCRIPT), "stop"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main(verbosity=2)
