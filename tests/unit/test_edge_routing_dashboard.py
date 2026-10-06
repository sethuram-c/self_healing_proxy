#!/usr/bin/env python3
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DASHBOARD = ROOT / "observability/grafana/dashboards/edge-routing-operations.json"

class EdgeRoutingDashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(DASHBOARD.read_text())
        cls.by_title = {p["title"]: p for p in cls.doc["panels"]}

    def test_identity_and_operator_panels(self):
        self.assertEqual(self.doc["uid"], "shp-edge-routing")
        self.assertIn("Edge Routing Operations", self.doc["title"])
        expected = {
            "Routing Decision Rate", "Remote Routing Ratio", "Fallback Ratio",
            "Fresh Latency Coverage", "Locality Distribution",
            "Fallback Rate by Reason", "Routing Decision Logs",
            "Operational Interpretation",
        }
        self.assertTrue(expected.issubset(self.by_title))

    def test_prometheus_queries_use_only_bounded_routing_dimensions(self):
        prom = [p for p in self.doc["panels"] if p.get("datasource", {}).get("uid") == "prometheus"]
        self.assertGreaterEqual(len(prom), 6)
        joined = "\n".join(t.get("expr", "") for p in prom for t in p.get("targets", []))
        for metric in ("routing[._]decisions", "routing[._]locality", "routing[._]fallbacks", "routing[._]latency[._]signal"):
            self.assertIn(metric, joined)
        for forbidden in ("backend=", "request_id", "trace_id", "span_id", "selected_index"):
            self.assertNotIn(forbidden, joined)
        self.assertIn('tier="REMOTE"', joined)
        self.assertIn('reason!="NONE"', joined)
        self.assertIn('used="true"', joined)

    def test_ratio_panels_are_derived_from_decision_counter(self):
        for title in ("Remote Routing Ratio", "Fallback Ratio", "Fresh Latency Coverage"):
            expr = self.by_title[title]["targets"][0]["expr"]
            self.assertIn("routing[._]decisions", expr)
            self.assertIn("clamp_min", expr)
            self.assertEqual(self.by_title[title]["fieldConfig"]["defaults"]["unit"], "percentunit")

    def test_routing_logs_are_service_and_schema_scoped(self):
        q = self.by_title["Routing Decision Logs"]["targets"][0]["expr"]
        self.assertIn('service_name="self-healing-proxy"', q)
        self.assertIn('schema="shp.routing.v1"', q)

    def test_runbook_preserves_metric_cardinality_rule(self):
        text = self.by_title["Operational Interpretation"]["options"]["content"]
        self.assertIn("do **not** label by backend", text)
        self.assertIn("health/circuit/capacity safety remains authoritative", text)

if __name__ == "__main__":
    unittest.main(verbosity=2)
