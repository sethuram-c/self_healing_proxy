#!/usr/bin/env python3
import os
import subprocess
import unittest

BIN = os.environ.get("LATENCY_AWARE_SELECTION_LAB_BIN", "build/latency_aware_selection_lab")


def run(*args):
    return subprocess.run([BIN, *args], text=True, capture_output=True, timeout=5)


class LatencyAwareSelectionTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        result = run("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("faster|stale|locality|no-samples", result.stderr)
        result = run("--scenario", "bogus")
        self.assertNotEqual(result.returncode, 0)

    def test_prefers_low_latency_cohort_with_slack_and_fairness(self):
        result = run("--scenario", "faster")
        self.assertEqual(result.returncode, 0, result.stderr)
        picks = [line.split()[0].split("=", 1)[1] for line in result.stdout.splitlines() if line.startswith("PICK=")]
        self.assertEqual(picks, ["edge-b", "edge-c", "edge-b", "edge-c"])
        self.assertNotIn("PICK=edge-a", result.stdout)
        self.assertIn("PHASE50_LATENCY_AWARE_SELECTION_OK", result.stdout)

    def test_stale_fast_measurement_loses_authority(self):
        result = run("--scenario", "stale")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SELECTED=edge-b", result.stdout)
        self.assertNotIn("SELECTED=edge-a", result.stdout)

    def test_locality_remains_stronger_than_latency(self):
        result = run("--scenario", "locality")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("RANK=SITE", result.stdout)
        self.assertNotIn("SELECTED=remote-d", result.stdout)

    def test_no_measurements_fall_back_to_round_robin(self):
        result = run("--scenario", "no-samples")
        self.assertEqual(result.returncode, 0, result.stderr)
        picks = [line.split()[0].split("=", 1)[1] for line in result.stdout.splitlines() if line.startswith("PICK=")]
        self.assertEqual(picks, ["edge-a", "edge-b", "edge-c"])
        self.assertIn("LATENCY_SIGNAL_NS=0", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
