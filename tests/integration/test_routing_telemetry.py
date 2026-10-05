#!/usr/bin/env python3
import json
import http.server
import threading
import time
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LAB = os.environ.get("ROUTING_TELEMETRY_LAB_BIN", str(ROOT / "build" / "routing_telemetry_lab"))

class RoutingTelemetryTests(unittest.TestCase):
    def run_lab(self, scenario):
        cp = subprocess.run([LAB, "--scenario", scenario], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        self.assertEqual(cp.returncode, 0, cp.stderr)
        self.assertIn("PHASE55_ROUTING_TELEMETRY_OK", cp.stdout)
        return cp.stdout

    def test_help_and_invalid_cli(self):
        cp = subprocess.run([LAB, "--help"], cwd=ROOT, text=True, capture_output=True, timeout=5)
        self.assertEqual(cp.returncode, 0)
        self.assertIn("connected|missing|bounded|bridge", cp.stdout)
        cp = subprocess.run([LAB, "--scenario", "nope"], cwd=ROOT, text=True, capture_output=True, timeout=5)
        self.assertNotEqual(cp.returncode, 0)

    def test_connected_frame_contains_routing_schema(self):
        out = self.run_lab("connected")
        first = next(line for line in out.splitlines() if line.startswith("{"))
        event = json.loads(first)
        self.assertEqual(event["schema"], "shp.routing.v1")
        self.assertEqual(event["backend"], "edge-a")
        self.assertIn("EMITTED=1 DROPPED=0 SENT=1", out)

    def test_missing_collector_drops_without_failure(self):
        out = self.run_lab("missing")
        self.assertIn("EMITTED=0 DROPPED=1 CHANNEL_DROPPED=1", out)

    def test_bounded_queue_drops_newest(self):
        out = self.run_lab("bounded")
        self.assertIn("QUEUED=1 EMITTED=1 DROPPED=1", out)

    def test_real_bridge_exports_routing_logs_and_metrics(self):
        received = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length)
                received.append((self.path, json.loads(body.decode("utf-8"))))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b"{}")
            def log_message(self, fmt, *args):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        with tempfile.TemporaryDirectory(prefix="shp55-") as td:
            sock = str(pathlib.Path(td) / "otel.sock")
            port = server.server_address[1]
            bridge = subprocess.Popen([
                "python3", str(ROOT / "control-plane" / "otel_bridge.py"),
                "--socket", sock,
                "--otlp-endpoint", f"http://127.0.0.1:{port}/v1/logs",
                "--metrics-export-every", "3",
                "--max-events", "3",
                "--request-timeout-ms", "1000",
                "--require-export", "--require-metrics-export",
            ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            deadline = time.time() + 3
            while time.time() < deadline and not pathlib.Path(sock).exists():
                if bridge.poll() is not None:
                    break
                time.sleep(0.01)
            self.assertTrue(pathlib.Path(sock).exists(), "bridge socket was not created")
            prod = subprocess.run([LAB, "--scenario", "bridge", "--socket", sock], cwd=ROOT,
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            self.assertEqual(prod.returncode, 0, prod.stderr)
            out, err = bridge.communicate(timeout=5)
            self.assertEqual(bridge.returncode, 0, err)
            self.assertIn("RECEIVED=3", out)
            self.assertIn("EXPORTED=3", out)
            self.assertIn("METRIC_ROUTING_DECISIONS=3", out)
        server.shutdown(); server.server_close(); thread.join(timeout=2)
        log_docs = [doc for path, doc in received if path == "/v1/logs"]
        metric_docs = [doc for path, doc in received if path == "/v1/metrics"]
        self.assertEqual(len(log_docs), 3)
        self.assertGreaterEqual(len(metric_docs), 1)
        bodies = [d["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]["body"]["stringValue"] for d in log_docs]
        self.assertTrue(all(json.loads(body)["schema"] == "shp.routing.v1" for body in bodies))
        names = {m["name"] for m in metric_docs[-1]["resourceMetrics"][0]["scopeMetrics"][0]["metrics"]}
        self.assertIn("shp.routing.decisions", names)
        self.assertIn("shp.routing.locality", names)
        self.assertIn("shp.routing.fallbacks", names)
        self.assertIn("shp.routing.latency_signal", names)


if __name__ == "__main__":
    unittest.main(verbosity=2)
