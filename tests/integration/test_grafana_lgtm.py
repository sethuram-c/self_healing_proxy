#!/usr/bin/env python3
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import re

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "scripts/grafana_lgtm_lab.sh"
BRIDGE = ROOT / "control-plane/otel_bridge.py"
JSON_LAB = Path(os.environ.get("JSON_LOG_LAB_BIN", str(ROOT / "build/json_log_telemetry_lab")))
TRACE_LAB = Path(os.environ.get("TRACE_SPAN_LAB_BIN", str(ROOT / "build/trace_span_telemetry_lab")))
GRAFANA = os.environ.get("PHASE37_GRAFANA_URL", "http://127.0.0.1:13000")
OTLP = os.environ.get("PHASE37_OTLP_URL", "http://127.0.0.1:14319/v1/logs")
REQUIRE_REAL = os.environ.get("PHASE37_REQUIRE_REAL") == "1"


def auth_request(url: str) -> urllib.request.Request:
    token = base64.b64encode(b"admin:admin").decode("ascii")
    return urllib.request.Request(url, headers={"Authorization": f"Basic {token}"})


def fetch_json(url: str, auth: bool = True, timeout: float = 2.0):
    req = auth_request(url) if auth else urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def post_json(url: str, payload: dict, timeout: float = 3.0):
    token = base64.b64encode(b"admin:admin").decode("ascii")
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Authorization": f"Basic {token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def wait_for(predicate, timeout=20.0, interval=0.25):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except Exception as exc:  # readiness paths are eventually consistent
            last = exc
        time.sleep(interval)
    if last:
        raise AssertionError(f"timed out waiting for condition; last error: {last}")
    raise AssertionError("timed out waiting for condition")


def wait_for_unix_socket(path: Path, timeout=5.0):
    def ready():
        if not path.exists():
            return False
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.settimeout(0.1)
            s.connect(str(path))
            return True
        except OSError:
            return False
        finally:
            s.close()
    wait_for(ready, timeout=timeout, interval=0.05)


class GrafanaLGTMIntegrationTests(unittest.TestCase):
    def test_lab_help_or_invalid_command_is_rejected(self):
        result = subprocess.run([str(LAB)], cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage:", result.stderr)

    @unittest.skipUnless(REQUIRE_REAL, "real Grafana LGTM test requires PHASE37_REQUIRE_REAL=1")
    def test_real_grafana_queries_logs_metrics_and_traces(self):
        subprocess.run([str(LAB), "stop"], cwd=ROOT, check=True, text=True, capture_output=True)
        try:
            subprocess.run([str(LAB), "start"], cwd=ROOT, check=True)

            health = wait_for(lambda: fetch_json(f"{GRAFANA}/api/health", auth=False), timeout=30)
            self.assertEqual(health.get("database"), "ok")

            for uid in ("prometheus", "loki", "tempo"):
                ds = wait_for(lambda uid=uid: fetch_json(f"{GRAFANA}/api/datasources/uid/{uid}"), timeout=10)
                self.assertEqual(ds.get("uid"), uid)

            dash = wait_for(lambda: fetch_json(f"{GRAFANA}/api/dashboards/uid/shp-overview"), timeout=15)
            self.assertEqual(dash["dashboard"]["uid"], "shp-overview")

            with tempfile.TemporaryDirectory(prefix="shp37-") as td:
                sock = Path(td) / "bridge.sock"
                bridge_cmd = [
                    "python3", str(BRIDGE),
                    "--socket", str(sock),
                    "--otlp-endpoint", OTLP,
                    "--metrics-export-every", "3",
                    "--max-events", "4",
                    "--require-export",
                    "--require-metrics-export",
                    "--require-trace-export",
                ]
                bridge = subprocess.Popen(bridge_cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                try:
                    wait_for_unix_socket(sock)
                    logs = subprocess.run([
                        str(JSON_LAB), "--socket", str(sock), "--attempts", "3",
                        "--backend", "backend-a", "--message", "phase37 grafana",
                    ], cwd=ROOT, text=True, capture_output=True, check=True)
                    self.assertIn("SENT=3", logs.stdout)

                    traces = subprocess.run([
                        str(TRACE_LAB), "--socket", str(sock), "--attempts", "3",
                        "--attempt-ms", "1", "--backend", "backend-a",
                    ], cwd=ROOT, text=True, capture_output=True, check=True)
                    self.assertIn("SPANS=4", traces.stdout)
                    trace_id_match = re.search(r"^TRACE_ID=([0-9a-f]{32})$", traces.stdout, re.MULTILINE)
                    self.assertIsNotNone(trace_id_match, traces.stdout)
                    emitted_trace_id = trace_id_match.group(1)

                    out, _ = bridge.communicate(timeout=15)
                    self.assertEqual(bridge.returncode, 0, out)
                    self.assertIn("EXPORTED=3", out)
                    match = re.search(r"^METRICS_EXPORTED=(\d+)$", out, re.MULTILINE)
                    self.assertIsNotNone(match, out)
                    self.assertGreaterEqual(int(match.group(1)), 1, out)
                    self.assertIn("METRICS_EXPORT_FAILED=0", out)
                    self.assertIn("TRACES_EXPORTED=1", out)
                    self.assertIn("TRACES_EXPORT_FAILED=0", out)
                finally:
                    if bridge.poll() is None:
                        bridge.terminate()
                        bridge.wait(timeout=5)

            # Prometheus through Grafana's provisioned datasource proxy.
            names_url = f"{GRAFANA}/api/datasources/proxy/uid/prometheus/api/v1/label/__name__/values"
            def prometheus_names():
                doc = fetch_json(names_url)
                vals = doc.get("data", [])
                return vals if any("shp" in str(v).lower() for v in vals) else None
            names = wait_for(prometheus_names, timeout=30, interval=0.5)
            normalized = {str(n).lower().replace('.', '_'): n for n in names}
            self.assertTrue(any("shp_requests" in key for key in normalized), names)
            self.assertTrue(any("shp_retries" in key for key in normalized), names)

            # Loki through Grafana's datasource proxy.
            now_ns = time.time_ns()
            start_ns = now_ns - 15 * 60 * 1_000_000_000
            query = urllib.parse.quote('{service_name="self-healing-proxy"} |= "phase37 grafana"', safe="")
            loki_url = (
                f"{GRAFANA}/api/datasources/proxy/uid/loki/loki/api/v1/query_range"
                f"?query={query}&start={start_ns}&end={now_ns}&limit=20"
            )
            def loki_found():
                doc = fetch_json(loki_url)
                result = doc.get("data", {}).get("result", [])
                return result if result else None
            loki_result = wait_for(loki_found, timeout=30, interval=0.5)
            self.assertGreaterEqual(len(loki_result), 1)

            # Tempo through Grafana's backend datasource query API. Grafana's
            # Tempo datasource supports exact trace-ID lookups via queryType
            # "traceId" on /api/ds/query; this is the same request shape used
            # by Grafana Explore for a direct trace lookup.
            tempo_query_url = f"{GRAFANA}/api/ds/query"
            tempo_payload = {
                "queries": [
                    {
                        "query": emitted_trace_id,
                        "queryType": "traceId",
                        "refId": "A",
                        "tableType": "traces",
                        "filters": [],
                        "serviceMapQuery": "",
                        "datasource": {"type": "tempo", "uid": "tempo"},
                        "intervalMs": 0,
                    }
                ],
                "from": "0",
                "to": "0",
            }

            def tempo_found():
                doc = post_json(tempo_query_url, tempo_payload)
                result = doc.get("results", {}).get("A", {})
                frames = result.get("frames", [])
                if not frames:
                    return None
                encoded = json.dumps(doc).lower()
                return doc if emitted_trace_id in encoded else None

            tempo_result = wait_for(tempo_found, timeout=30, interval=0.5)
            self.assertIn(emitted_trace_id, json.dumps(tempo_result).lower())

            print("PHASE37_GRAFANA_QUERY_OK")
            print(f"DASHBOARD_UID={dash['dashboard']['uid']}")
            print(f"PROMETHEUS_SHP_METRICS={sum(1 for n in names if 'shp' in str(n).lower())}")
            print(f"LOKI_STREAMS={len(loki_result)}")
            print("TEMPO_TRACES=1")
            print(f"TEMPO_TRACE_ID={emitted_trace_id}")
        finally:
            subprocess.run([str(LAB), "stop"], cwd=ROOT, check=False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
