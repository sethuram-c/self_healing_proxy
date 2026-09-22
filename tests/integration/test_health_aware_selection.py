#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("HEALTH_AWARE_SELECTION_LAB_BIN", ROOT / "build/health_aware_selection_lab"))


def run_lab(*args):
    return subprocess.run([str(LAB), *args], cwd=ROOT, text=True, capture_output=True)


class HealthAwareSelectionLabTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        help_run = run_lab("--help")
        self.assertEqual(help_run.returncode, 0)
        self.assertIn("skip|all-unavailable|rejoin", help_run.stderr)

        bad = run_lab("--scenario", "bogus")
        self.assertNotEqual(bad.returncode, 0)

    def test_unhealthy_backend_is_skipped_without_losing_round_robin(self):
        result = run_lab("--scenario", "skip")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if line.startswith("STEP=")]
        selected = [line.split("SELECTED=", 1)[1].split()[0] for line in lines]
        self.assertEqual(selected, ["backend-a", "backend-c", "backend-a", "backend-c"])
        self.assertNotIn("SELECTED=backend-b", result.stdout)
        self.assertIn("PHASE42_HEALTH_AWARE_SELECTION_OK", result.stdout)

    def test_all_unavailable_fails_closed_without_cursor_corruption(self):
        result = run_lab("--scenario", "all-unavailable")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SELECTED=NONE", result.stdout)
        self.assertIn("ELIGIBLE=0", result.stdout)
        self.assertIn("CURSOR=0", result.stdout)
        self.assertIn("PHASE42_HEALTH_AWARE_SELECTION_OK", result.stdout)

    def test_recovered_backend_rejoins_rotation(self):
        result = run_lab("--scenario", "rejoin")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("REJOIN_STATE=HEALTHY ELIGIBLE=1", result.stdout)
        after = [line for line in result.stdout.splitlines() if line.startswith("STEP=after_")]
        selected = [line.split("SELECTED=", 1)[1].split()[0] for line in after]
        self.assertEqual(selected, ["backend-a", "backend-b", "backend-c"])
        self.assertIn("PHASE42_HEALTH_AWARE_SELECTION_OK", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
