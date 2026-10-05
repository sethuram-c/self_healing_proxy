#!/usr/bin/env python3
import importlib.util
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"
spec = importlib.util.spec_from_file_location("otel_bridge_phase55", BRIDGE)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
assert spec.loader is not None
spec.loader.exec_module(module)


def routing_event(seq=1, locality="SITE", fallback="NONE", used=True, backend="edge-a"):
    return {
        "schema": "shp.routing.v1", "timestamp_ns": 1000 + seq, "sequence": seq,
        "selected_index": 0, "backend": backend, "locality_rank": locality,
        "fallback_reason": fallback, "fastest_fresh_latency_ns": 2000000 if used else 0,
        "fresh_latency_candidates": 2 if used else 0, "safe_locality_candidates": 2,
        "best_score": 400, "score_threshold": 500, "selected_score": 450,
        "latency_component": 1000 if used else 0, "load_component": 200,
        "used_latency": used,
    }


class RoutingMetricsTests(unittest.TestCase):
    def test_validation_and_otlp_log_mapping(self):
        event = routing_event()
        self.assertEqual(module.validate_routing_event(event), event)
        doc = module.build_otlp_routing_logs_request(event, observed_unix_ns=9999)
        record = doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]
        self.assertEqual(record["timeUnixNano"], "9999")
        self.assertNotIn("traceId", record)
        self.assertIn('"schema":"shp.routing.v1"', record["body"]["stringValue"])

    def test_metrics_are_bounded_labels_not_backend_names(self):
        routing = module.RoutingMetricsState(start_unix_ns=100)
        for i in range(100):
            routing.observe(routing_event(i + 1, backend=f"backend-{i}"))
        self.assertEqual(routing.decisions_total, 100)
        self.assertEqual(set(routing.by_locality), set(module.ROUTING_LOCALITIES))
        self.assertEqual(set(routing.by_fallback), set(module.ROUTING_FALLBACKS))
        base = module.MetricsState(start_unix_ns=100)
        counters = module.Counters()
        doc = module.build_otlp_metrics_request(base, counters, now_unix_ns=200, routing=routing)
        metrics = {m["name"]: m for m in doc["resourceMetrics"][0]["scopeMetrics"][0]["metrics"]}
        self.assertIn("shp.routing.decisions", metrics)
        self.assertIn("shp.routing.locality", metrics)
        self.assertIn("shp.routing.fallbacks", metrics)
        self.assertIn("shp.routing.latency_signal", metrics)
        text = str(metrics)
        self.assertNotIn("backend-99", text)

    def test_observes_fixed_locality_fallback_and_latency_dimensions(self):
        routing = module.RoutingMetricsState(start_unix_ns=100)
        routing.observe(routing_event(1, "SITE", "NONE", True))
        routing.observe(routing_event(2, "ZONE", "SITE_UNAVAILABLE", True))
        routing.observe(routing_event(3, "REMOTE", "LOCAL_UNAVAILABLE", False))
        self.assertEqual(routing.decisions_total, 3)
        self.assertEqual(routing.by_locality["SITE"], 1)
        self.assertEqual(routing.by_locality["ZONE"], 1)
        self.assertEqual(routing.by_locality["REMOTE"], 1)
        self.assertEqual(routing.by_fallback["LOCAL_UNAVAILABLE"], 1)
        self.assertEqual(routing.latency_used[True], 2)
        self.assertEqual(routing.latency_used[False], 1)

    def test_invalid_enum_and_score_order_rejected(self):
        event = routing_event()
        event["locality_rank"] = "PLANET"
        with self.assertRaises(ValueError): module.validate_routing_event(event)
        event = routing_event()
        event["selected_score"] = 600
        with self.assertRaises(ValueError): module.validate_routing_event(event)


if __name__ == "__main__":
    unittest.main(verbosity=2)
