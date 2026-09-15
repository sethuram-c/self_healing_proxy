#!/usr/bin/env python3
import importlib.util
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "control-plane" / "otel_bridge.py"

spec = importlib.util.spec_from_file_location("otel_bridge_phase35", BRIDGE)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
assert spec.loader is not None
spec.loader.exec_module(module)


def event(attempt, backend="backend-a", result="retry"):
    return {
        "schema": "shp.telemetry.v1",
        "timestamp_unix_ns": 1792259896804725007 + attempt,
        "event": "backend_attempt",
        "request_id": "0123456789abcdef0123456789abcdef",
        "trace_id": "4bf92f3577b34da6a3ce929d0e0e4736",
        "span_id": f"{attempt:016x}",
        "trace_source": "generated",
        "attempt": attempt,
        "backend": backend,
        "result": result,
        "message": "phase35",
    }


class MetricsTests(unittest.TestCase):
    def test_counts_request_retry_and_completion(self):
        state = module.MetricsState(start_unix_ns=100)
        counters = module.Counters()
        state.observe(event(1, result="retry"))
        state.observe(event(2, result="retry"))
        state.observe(event(3, result="success"))
        self.assertEqual(state.requests_total, 1)
        self.assertEqual(state.retries_total, 2)
        self.assertEqual(state.completions["success"], 1)
        self.assertEqual(state.backend_attempts[("backend-a", "retry")], 2)
        self.assertEqual(state.backend_attempts[("backend-a", "success")], 1)

        doc = module.build_otlp_metrics_request(state, counters, now_unix_ns=200)
        scope = doc["resourceMetrics"][0]["scopeMetrics"][0]
        metrics = {m["name"]: m for m in scope["metrics"]}
        req = metrics["shp.requests"]["sum"]
        self.assertTrue(req["isMonotonic"])
        self.assertEqual(req["aggregationTemporality"], 2)
        self.assertEqual(req["dataPoints"][0]["asInt"], "1")
        self.assertEqual(req["dataPoints"][0]["startTimeUnixNano"], "100")
        self.assertEqual(req["dataPoints"][0]["timeUnixNano"], "200")

    def test_backend_cardinality_is_bounded(self):
        state = module.MetricsState(start_unix_ns=100, max_backends=2)
        for i, backend in enumerate(["a", "b", "c", "d"], start=1):
            state.observe(event(i, backend=backend, result="retry"))
        self.assertEqual(state.tracked_backends, 2)
        labels = {backend for backend, _result in state.backend_attempts}
        self.assertEqual(labels, {"a", "b", "__other__"})
        self.assertEqual(state.backend_attempts[("__other__", "retry")], 2)

    def test_result_labels_are_normalized(self):
        state = module.MetricsState(start_unix_ns=100)
        state.observe(event(1, result="failed"))
        state.observe(event(2, result="totally-new-result"))
        labels = {result for _backend, result in state.backend_attempts}
        self.assertEqual(labels, {"error", "other"})

    def test_metrics_endpoint_is_derived(self):
        self.assertEqual(
            module._metrics_endpoint("http://127.0.0.1:4318/v1/logs"),
            "http://127.0.0.1:4318/v1/metrics",
        )
        self.assertEqual(
            module._metrics_endpoint("http://127.0.0.1:4318", "http://127.0.0.1:9999/v1/metrics"),
            "http://127.0.0.1:9999/v1/metrics",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
