#!/usr/bin/env python3
import os
import re
import subprocess
import unittest

BIN = os.environ.get("ADAPTIVE_STABILITY_LAB_BIN", "build/adaptive_stability_lab")

class AdaptiveStabilityTests(unittest.TestCase):
    def run_scenario(self, name):
        p = subprocess.run([BIN, "--scenario", name], text=True, capture_output=True, timeout=20)
        self.assertEqual(p.returncode, 0, msg=p.stdout + p.stderr)
        self.assertIn("PHASE52_ADAPTIVE_STABILITY_OK", p.stdout)
        return p.stdout

    def test_help_and_invalid_cli(self):
        p = subprocess.run([BIN, "--help"], text=True, capture_output=True)
        self.assertEqual(p.returncode, 0)
        self.assertIn("oscillation|overload|stale|failover", p.stderr)
        p = subprocess.run([BIN, "--scenario", "nope"], text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_oscillating_latency_and_load_remain_bounded(self):
        out = self.run_scenario("oscillation")
        m = re.search(r"NO_SELECTION=(\d+).*MAX_STREAK=(\d+).*SPREAD=(\d+)", out)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 0)
        self.assertLessEqual(int(m.group(2)), 25)
        self.assertLessEqual(int(m.group(3)), 600)

    def test_overload_shifts_work_away_from_hot_peers(self):
        out = self.run_scenario("overload")
        m = re.search(r"FIRST_A=(\d+) FIRST_B=(\d+) FIRST_C=(\d+) SECOND_A=(\d+) SECOND_B=(\d+) SECOND_C=(\d+)", out)
        self.assertIsNotNone(m)
        first_a, first_b, first_c, second_a, second_b, second_c = map(int, m.groups())
        self.assertGreater(first_b, first_a)
        self.assertGreater(first_b, first_c)
        self.assertGreater(second_a, second_c)

    def test_stale_fast_signal_loses_control_under_sustained_traffic(self):
        out = self.run_scenario("stale")
        m = re.search(r"PICKS_STALE_FAST=(\d+) PICKS_FRESH_B=(\d+) PICKS_FRESH_C=(\d+)", out)
        self.assertIsNotNone(m)
        stale, b, c = map(int, m.groups())
        self.assertLess(stale, b)
        self.assertLess(stale, c)

    def test_local_outage_fails_over_to_remote_without_no_selection_gap(self):
        out = self.run_scenario("failover")
        self.assertIn("SITE_PICKS=100 REMOTE_PICKS=200", out)

if __name__ == "__main__":
    unittest.main(verbosity=2)
