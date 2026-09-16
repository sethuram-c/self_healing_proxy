#!/usr/bin/env python3
import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"
spec = importlib.util.spec_from_file_location("phase36_otel_bridge", BRIDGE)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
assert spec.loader is not None
spec.loader.exec_module(module)


def batch():
    return {
        "schema": "shp.trace.v1",
        "request_id": "11" * 16,
        "trace_id": "22" * 16,
        "trace_source": "generated",
        "trace_flags": 1,
        "spans": [
            {
                "name": "proxy.request",
                "span_id": "33" * 8,
                "parent_span_id": "",
                "kind": "server",
                "start_unix_ns": 1_000_000_000,
                "end_unix_ns": 1_030_000_000,
                "duration_ns": 30_000_000,
                "attempt": 0,
                "backend": "",
                "result": "success",
            },
            {
                "name": "proxy.backend_attempt",
                "span_id": "44" * 8,
                "parent_span_id": "33" * 8,
                "kind": "client",
                "start_unix_ns": 1_005_000_000,
                "end_unix_ns": 1_015_000_000,
                "duration_ns": 10_000_000,
                "attempt": 1,
                "backend": "backend-a",
                "result": "retry",
            },
            {
                "name": "proxy.backend_attempt",
                "span_id": "55" * 8,
                "parent_span_id": "33" * 8,
                "kind": "client",
                "start_unix_ns": 1_016_000_000,
                "end_unix_ns": 1_026_000_000,
                "duration_ns": 10_000_000,
                "attempt": 2,
                "backend": "backend-a",
                "result": "success",
            },
        ],
    }


class TraceMappingTests(unittest.TestCase):
    def test_trace_endpoint_is_derived(self):
        self.assertEqual(
            module._traces_endpoint("http://127.0.0.1:4318/v1/logs"),
            "http://127.0.0.1:4318/v1/traces",
        )

    def test_request_and_attempt_parentage_maps_to_otlp(self):
        doc = module.build_otlp_traces_request(batch())
        spans = doc["resourceSpans"][0]["scopeSpans"][0]["spans"]
        self.assertEqual(len(spans), 3)
        self.assertEqual(spans[0]["kind"], 2)
        self.assertNotIn("parentSpanId", spans[0])
        self.assertEqual(spans[1]["kind"], 3)
        self.assertEqual(spans[1]["parentSpanId"], "33" * 8)
        self.assertEqual(spans[2]["parentSpanId"], "33" * 8)
        self.assertEqual(spans[0]["status"]["code"], 1)
        self.assertEqual(spans[1]["status"]["code"], 2)
        self.assertEqual(spans[2]["status"]["code"], 1)
        self.assertEqual({sp["traceId"] for sp in spans}, {"22" * 16})

    def test_remote_parent_is_preserved_on_request_span(self):
        doc_in = batch()
        doc_in["spans"][0]["parent_span_id"] = "aa" * 8
        spans = module.build_otlp_traces_request(doc_in)["resourceSpans"][0]["scopeSpans"][0]["spans"]
        self.assertEqual(spans[0]["parentSpanId"], "aa" * 8)

    def test_invalid_parentage_and_timing_are_rejected(self):
        bad = batch()
        bad["spans"][1]["parent_span_id"] = "99" * 8
        with self.assertRaises(ValueError):
            module.validate_trace_batch(bad)
        bad = batch()
        bad["spans"][1]["duration_ns"] = 999_000_000
        with self.assertRaises(ValueError):
            module.validate_trace_batch(bad)


if __name__ == "__main__":
    unittest.main(verbosity=2)
