#!/usr/bin/env python3
import os, subprocess, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("HEALING_COORDINATOR_LAB_BIN", ROOT / "build/healing_coordinator_lab"))

class FleetHealingCoordinatorTests(unittest.TestCase):
    def run_lab(self, scenario):
        return subprocess.run([str(LAB), "--scenario", scenario], cwd=ROOT, text=True, capture_output=True, check=True).stdout

    def test_help_and_invalid_cli(self):
        p = subprocess.run([str(LAB), "--help"], cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(p.returncode, 0)
        self.assertIn("concurrency|fairness|budget", p.stderr)
        p = subprocess.run([str(LAB), "--scenario", "bad"], cwd=ROOT, text=True, capture_output=True)
        self.assertNotEqual(p.returncode, 0)

    def test_global_concurrency_limit_blocks_restart_storm(self):
        out = self.run_lab("concurrency")
        self.assertIn("DENY reason=CONCURRENCY_LIMIT", out)
        self.assertIn("AFTER_COMPLETE backend=2 active=2", out)
        self.assertIn("PHASE47_FLEET_COORDINATION_OK", out)

    def test_round_robin_fairness_prevents_noisy_backend_monopoly(self):
        out = self.run_lab("fairness")
        for line in ["STEP=1 ADMIT backend=0", "STEP=2 ADMIT backend=1", "STEP=3 ADMIT backend=2", "STEP=4 ADMIT backend=0"]:
            self.assertIn(line, out)
        self.assertIn("PHASE47_FLEET_COORDINATION_OK", out)

    def test_global_attempt_budget_ages_out(self):
        out = self.run_lab("budget")
        self.assertIn("AT_40 reason=WINDOW_BUDGET", out)
        self.assertIn("AT_111 backend=3 reason=ALLOWED", out)
        self.assertIn("PHASE47_FLEET_COORDINATION_OK", out)

if __name__ == "__main__": unittest.main(verbosity=2)
