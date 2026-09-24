#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = Path(os.environ.get("REMEDIATION_POLICY_LAB_BIN", ROOT / "build/remediation_policy_lab"))


def run(*args):
    return subprocess.run([str(BIN), *args], cwd=ROOT, text=True, capture_output=True)


class RemediationPolicyLabTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        p = run("--help")
        self.assertEqual(p.returncode, 0)
        self.assertIn("evidence|backoff|budget|recovery", p.stderr)
        p = run("--scenario", "nope")
        self.assertNotEqual(p.returncode, 0)

    def test_requires_strong_combined_evidence(self):
        p = run("--scenario", "evidence")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("STEP=health_only", p.stdout)
        self.assertIn("ACTION=NONE REASON=INSUFFICIENT_EVIDENCE", p.stdout)
        self.assertIn("STEP=both_signals", p.stdout)
        self.assertIn("ACTION=RESTART_BACKEND REASON=ALLOWED", p.stdout)

    def test_backoff_doubles_and_is_bounded(self):
        p = run("--scenario", "backoff")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("STEP=blocked_1 NOW=19 ACTION=NONE REASON=BACKOFF", p.stdout)
        self.assertIn("STEP=allowed_2 NOW=20 ACTION=RESTART_BACKEND", p.stdout)
        self.assertIn("STEP=blocked_2 NOW=39 ACTION=NONE REASON=BACKOFF", p.stdout)
        self.assertIn("STEP=allowed_3 NOW=40 ACTION=RESTART_BACKEND", p.stdout)

    def test_attempt_budget_ages_out(self):
        p = run("--scenario", "budget")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("STEP=budget_exhausted", p.stdout)
        self.assertIn("REASON=WINDOW_BUDGET_EXHAUSTED", p.stdout)
        self.assertIn("STEP=window_aged", p.stdout)
        self.assertIn("ACTION=RESTART_BACKEND REASON=ALLOWED", p.stdout)

    def test_recovery_resets_backoff_without_erasing_window_safety(self):
        p = run("--scenario", "recovery")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("STEP=healthy_no_action", p.stdout)
        self.assertIn("REASON=INSUFFICIENT_EVIDENCE", p.stdout)
        self.assertIn("STEP=relapse_allowed", p.stdout)
        self.assertIn("ACTION=RESTART_BACKEND REASON=ALLOWED", p.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
