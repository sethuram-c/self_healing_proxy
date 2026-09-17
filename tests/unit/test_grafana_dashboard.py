#!/usr/bin/env python3
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DASHBOARD = ROOT / "observability/grafana/dashboards/self-healing-proxy-overview.json"
PROVIDER = ROOT / "observability/grafana/provisioning/dashboards/shp.yaml"
LAB = ROOT / "scripts/grafana_lgtm_lab.sh"


class GrafanaDashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(DASHBOARD.read_text())

    def test_dashboard_identity_and_core_panels(self):
        self.assertEqual(self.doc["uid"], "shp-overview")
        self.assertIn("Self-Healing Proxy", self.doc["title"])
        panels = self.doc["panels"]
        self.assertGreaterEqual(len(panels), 6)
        titles = {p["title"] for p in panels}
        self.assertTrue({"Logical Requests", "Retries", "Backend Attempts by Result", "Correlated Logs", "Recent Traces"}.issubset(titles))

    def test_dashboard_uses_built_in_lgtm_datasource_uids(self):
        used = {
            panel.get("datasource", {}).get("uid")
            for panel in self.doc["panels"]
            if isinstance(panel.get("datasource"), dict)
        }
        self.assertTrue({"prometheus", "loki", "tempo"}.issubset(used))

    def test_metric_queries_do_not_use_unbounded_request_or_trace_labels(self):
        prom_panels = [p for p in self.doc["panels"] if p.get("datasource", {}).get("uid") == "prometheus"]
        self.assertGreaterEqual(len(prom_panels), 3)
        for panel in prom_panels:
            for target in panel.get("targets", []):
                expr = target.get("expr", "")
                self.assertNotIn("request_id", expr)
                self.assertNotIn("trace_id", expr)
                self.assertNotIn("span_id", expr)

    def test_logs_and_traces_are_service_scoped(self):
        by_title = {p["title"]: p for p in self.doc["panels"]}
        loki_query = by_title["Correlated Logs"]["targets"][0]["expr"]
        trace_query = by_title["Recent Traces"]["targets"][0]["query"]
        self.assertIn('service_name="self-healing-proxy"', loki_query)
        self.assertIn('resource.service.name = "self-healing-proxy"', trace_query)

    def test_provisioning_and_lab_pin_the_demo_stack(self):
        provider = PROVIDER.read_text()
        lab = LAB.read_text()
        self.assertIn("Self-Healing Proxy", provider)
        self.assertIn("/otel-lgtm/grafana/conf/provisioning/dashboards/custom", provider)
        self.assertIn("docker.io/grafana/otel-lgtm:0.36.0", lab)
        self.assertNotIn("grafana/otel-lgtm:latest", lab)


if __name__ == "__main__":
    unittest.main(verbosity=2)
