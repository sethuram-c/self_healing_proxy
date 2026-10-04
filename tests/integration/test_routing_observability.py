#!/usr/bin/env python3
import json
import os
import subprocess
import unittest

BIN = os.environ.get("ROUTING_OBSERVABILITY_LAB_BIN", "build/routing_observability_lab")
PERF_BIN = os.environ.get("ROUTING_OBSERVABILITY_PERF_BIN", "build/routing_observability_lab-perf")

class RoutingObservabilityTests(unittest.TestCase):
    def run_case(self, scenario, perf=False):
        binary = PERF_BIN if perf else BIN
        cp = subprocess.run([binary, "--scenario", scenario], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertEqual(cp.returncode, 0, cp.stderr + cp.stdout)
        self.assertIn("PHASE54_ROUTING_OBSERVABILITY_OK", cp.stdout)
        return cp.stdout

    def first_event(self, out):
        line = next(x for x in out.splitlines() if x.startswith("{"))
        return json.loads(line)

    def test_selected_decision_exposes_score_and_freshness(self):
        e = self.first_event(self.run_case("selected"))
        self.assertEqual(e["schema"], "shp.routing.v1")
        self.assertEqual(e["locality_rank"], "SITE")
        self.assertEqual(e["fallback_reason"], "NONE")
        self.assertTrue(e["used_latency"])
        self.assertGreaterEqual(e["fresh_latency_candidates"], 2)
        self.assertLessEqual(e["best_score"], e["selected_score"])
        self.assertLessEqual(e["selected_score"], e["score_threshold"])

    def test_locality_fallback_reason_is_explicit(self):
        e = self.first_event(self.run_case("fallback"))
        self.assertEqual(e["backend"], "zone-c")
        self.assertEqual(e["locality_rank"], "ZONE")
        self.assertEqual(e["fallback_reason"], "SITE_UNAVAILABLE")

    def test_stale_latency_reports_load_only_selection(self):
        e = self.first_event(self.run_case("load-only"))
        self.assertFalse(e["used_latency"])
        self.assertEqual(e["fresh_latency_candidates"], 0)
        self.assertEqual(e["fastest_fresh_latency_ns"], 0)

    @unittest.skipIf(os.environ.get("SKIP_PERF") == "1", "performance budget runs only on optimized binary")
    def test_trace_capture_overhead_is_bounded(self):
        out = self.run_case("overhead", perf=True)
        fields = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
        self.assertLessEqual(int(fields["TRACE_OVERHEAD_X1000"]), int(fields["TRACE_BUDGET_X1000"]))
        self.assertLessEqual(int(fields["TRACED_NS_PER_OP_X1000"]),
                             int(fields["TRACE_ABSOLUTE_BUDGET_NS"]) * 1000)

    def test_help_and_invalid_cli(self):
        cp = subprocess.run([BIN, "--help"], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(cp.returncode, 0)
        self.assertIn("selected|fallback|load-only|overhead", cp.stderr)
        cp = subprocess.run([BIN, "--scenario", "bogus"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(cp.returncode, 2)

if __name__ == "__main__":
    unittest.main(verbosity=2)
