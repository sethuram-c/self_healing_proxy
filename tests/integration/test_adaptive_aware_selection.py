#!/usr/bin/env python3
import os
import subprocess
import unittest

BIN = os.environ.get("ADAPTIVE_AWARE_SELECTION_LAB_BIN", "build/adaptive_aware_selection_lab")


def run(*args):
    return subprocess.run([BIN, *args], text=True, capture_output=True, check=False)


class AdaptiveAwareSelectionTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        p = run("--help")
        self.assertEqual(p.returncode, 0)
        self.assertIn("load-shift|tradeoff|fairness|locality", p.stderr)
        self.assertNotEqual(run("--scenario", "bogus").returncode, 0)

    def test_equal_latency_prefers_less_loaded_backend(self):
        p = run("--scenario", "load-shift")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("PICK=edge-b", p.stdout)
        self.assertIn("LOAD_COMPONENT=200", p.stdout)
        self.assertIn("PHASE51_ADAPTIVE_SCORING_OK", p.stdout)

    def test_load_can_outweigh_modest_latency_advantage(self):
        p = run("--scenario", "tradeoff")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("PICK=edge-b", p.stdout)
        self.assertIn("PHASE51_ADAPTIVE_SCORING_OK", p.stdout)

    def test_near_equal_scores_preserve_round_robin_fairness(self):
        p = run("--scenario", "fairness")
        self.assertEqual(p.returncode, 0, p.stderr)
        picks = [line.split()[0].split("=", 1)[1] for line in p.stdout.splitlines() if line.startswith("PICK=")]
        self.assertEqual(picks, ["edge-a", "edge-b", "edge-a", "edge-b"])

    def test_locality_remains_stronger_than_adaptive_score(self):
        p = run("--scenario", "locality")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("RANK=SITE", p.stdout)
        self.assertNotIn("PICK=remote-d", p.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
