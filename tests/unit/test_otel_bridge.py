#!/usr/bin/env python3
import importlib.util
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE_PATH = ROOT / "control-plane" / "otel_bridge.py"
spec = importlib.util.spec_from_file_location("otel_bridge", BRIDGE_PATH)
bridge = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bridge
assert spec.loader is not None
spec.loader.exec_module(bridge)


class OTelBridgeUnitTests(unittest.TestCase):
    def event(self):
        return {
            "schema": "shp.telemetry.v1",
            "timestamp_unix_ns": 1789234567890000000,
            "event": "backend_attempt",
            "request_id": "0123456789abcdef0123456789abcdef",
            "trace_id": "4bf92f3577b34da6a3ce929d0e0e4736",
            "span_id": "00f067aa0ba902b7",
            "trace_source": "remote",
            "attempt": 2,
            "backend": "backend-a",
            "result": "retry",
            "message": "upstream reset",
        }

    def test_otlp_mapping_preserves_trace_and_structured_attributes(self):
        event = self.event()
        doc = bridge.build_otlp_logs_request(event, observed_unix_ns=1789234567999999999)
        record = doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]
        self.assertEqual(record["traceId"], event["trace_id"])
        self.assertEqual(record["spanId"], event["span_id"])
        self.assertEqual(record["timeUnixNano"], str(event["timestamp_unix_ns"]))
        self.assertEqual(record["observedTimeUnixNano"], "1789234567999999999")
        self.assertEqual(record["severityNumber"], 9)
        self.assertEqual(json.loads(record["body"]["stringValue"]), event)
        attrs = {
            item["key"]: next(iter(item["value"].values()))
            for item in record["attributes"]
        }
        self.assertEqual(attrs["event.name"], "backend_attempt")
        self.assertEqual(attrs["shp.request_id"], event["request_id"])
        self.assertEqual(attrs["shp.attempt"], "2")

    def test_invalid_schema_and_identifiers_are_rejected(self):
        event = self.event()
        event["schema"] = "other"
        with self.assertRaises(ValueError):
            bridge.validate_phase33_event(event)

        event = self.event()
        event["trace_id"] = "0" * 32
        with self.assertRaises(ValueError):
            bridge.validate_phase33_event(event)

        event = self.event()
        event["span_id"] = "ABCDEF0123456789"
        with self.assertRaises(ValueError):
            bridge.validate_phase33_event(event)

    def test_error_result_maps_to_error_severity(self):
        event = self.event()
        event["result"] = "error"
        doc = bridge.build_otlp_logs_request(event, observed_unix_ns=1)
        record = doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]
        self.assertEqual(record["severityNumber"], 17)
        self.assertEqual(record["severityText"], "ERROR")

    def test_endpoint_root_gets_logs_path(self):
        self.assertEqual(
            bridge._validate_endpoint("http://127.0.0.1:4318"),
            "http://127.0.0.1:4318/v1/logs",
        )
        with self.assertRaises(ValueError):
            bridge._validate_endpoint("unix:///tmp/not-otlp")


if __name__ == "__main__":
    unittest.main(verbosity=2)
