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
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "scripts/grafana_lgtm_lab.sh"
BRIDGE = ROOT / "control-plane/otel_bridge.py"
ROUTING_LAB = Path(os.environ.get("ROUTING_TELEMETRY_LAB_BIN", str(ROOT / "build/routing_telemetry_lab")))
GRAFANA = os.environ.get("PHASE56_GRAFANA_URL", "http://127.0.0.1:13000")
OTLP = os.environ.get("PHASE56_OTLP_URL", "http://127.0.0.1:14319/v1/logs")
REQUIRE_REAL = os.environ.get("PHASE56_REQUIRE_REAL") == "1"


def auth_request(url):
    token = base64.b64encode(b"admin:admin").decode("ascii")
    return urllib.request.Request(url, headers={"Authorization": f"Basic {token}"})

def fetch_json(url, auth=True, timeout=2.0):
    req = auth_request(url) if auth else urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))

def wait_for(fn, timeout=30.0, interval=0.25):
    deadline=time.monotonic()+timeout; last=None
    while time.monotonic()<deadline:
        try:
            v=fn()
            if v: return v
        except Exception as exc:
            last=exc
        time.sleep(interval)
    raise AssertionError(f"timed out waiting for condition; last={last}")

def wait_socket(path, timeout=5.0):
    def ready():
        if not path.exists(): return False
        s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.settimeout(0.1); s.connect(str(path)); return True
        except OSError: return False
        finally: s.close()
    return wait_for(ready, timeout=timeout, interval=0.05)

class EdgeRoutingViewsTests(unittest.TestCase):
    def test_lab_contract_and_dashboard_file_exist(self):
        cp=subprocess.run([str(LAB)], cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(cp.returncode, 2)
        self.assertIn("usage:", cp.stderr)
        doc=json.loads((ROOT/"observability/grafana/dashboards/edge-routing-operations.json").read_text())
        self.assertEqual(doc["uid"], "shp-edge-routing")

    @unittest.skipUnless(REQUIRE_REAL, "real Phase 56 Grafana routing-view test requires PHASE56_REQUIRE_REAL=1")
    def test_real_grafana_provisions_dashboard_and_queries_routing_signals(self):
        subprocess.run([str(LAB), "stop"], cwd=ROOT, check=False, text=True, capture_output=True)
        try:
            subprocess.run([str(LAB), "start"], cwd=ROOT, check=True)
            wait_for(lambda: fetch_json(f"{GRAFANA}/api/health", auth=False).get("database") == "ok")
            dash=wait_for(lambda: fetch_json(f"{GRAFANA}/api/dashboards/uid/shp-edge-routing"), timeout=20)
            self.assertEqual(dash["dashboard"]["uid"], "shp-edge-routing")

            with tempfile.TemporaryDirectory(prefix="shp56-") as td:
                sock=Path(td)/"routing.sock"
                bridge=subprocess.Popen([
                    "python3", str(BRIDGE), "--socket", str(sock),
                    "--otlp-endpoint", OTLP, "--metrics-export-every", "3", "--max-events", "3",
                    "--request-timeout-ms", "1000", "--require-export", "--require-metrics-export",
                ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                try:
                    wait_socket(sock)
                    prod=subprocess.run([str(ROUTING_LAB), "--scenario", "bridge", "--socket", str(sock)],
                                        cwd=ROOT, text=True, capture_output=True, timeout=5, check=True)
                    self.assertIn("EMITTED=3", prod.stdout)
                    out,_=bridge.communicate(timeout=15)
                    self.assertEqual(bridge.returncode,0,out)
                    self.assertIn("METRIC_ROUTING_DECISIONS=3",out)
                finally:
                    if bridge.poll() is None:
                        bridge.terminate(); bridge.wait(timeout=5)

            names_url=f"{GRAFANA}/api/datasources/proxy/uid/prometheus/api/v1/label/__name__/values"
            def routing_names():
                vals=fetch_json(names_url).get("data",[])
                norm=[str(v).lower().replace('.', '_') for v in vals]
                return vals if any("shp_routing_decisions" in n for n in norm) else None
            names=wait_for(routing_names, timeout=30, interval=0.5)
            norm={str(v).lower().replace('.', '_') for v in names}
            for wanted in ("shp_routing_decisions", "shp_routing_locality", "shp_routing_fallbacks", "shp_routing_latency_signal"):
                self.assertTrue(any(wanted in n for n in norm), (wanted,names))

            now=time.time_ns(); start=now-15*60*1_000_000_000
            query=urllib.parse.quote('{service_name="self-healing-proxy"} |= "shp.routing.v1"', safe="")
            loki=(f"{GRAFANA}/api/datasources/proxy/uid/loki/loki/api/v1/query_range?query={query}&start={start}&end={now}&limit=20")
            logs=wait_for(lambda: (d:=fetch_json(loki)).get("data",{}).get("result") or None, timeout=30, interval=0.5)
            self.assertGreaterEqual(len(logs),1)
            print("PHASE56_EDGE_ROUTING_VIEWS_OK")
            print("DASHBOARD_UID=shp-edge-routing")
            print(f"ROUTING_METRICS={sum(1 for n in norm if 'shp_routing_' in n)}")
            print(f"ROUTING_LOG_STREAMS={len(logs)}")
        finally:
            subprocess.run([str(LAB), "stop"], cwd=ROOT, check=False, text=True, capture_output=True)

if __name__ == "__main__":
    unittest.main(verbosity=2)
