#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("RECOVERY_ORCHESTRATOR_LAB_BIN", ROOT / "build/recovery_orchestrator_lab"))


class RecoveryOrchestratorIntegrationTests(unittest.TestCase):
    def run_lab(self, scenario, timeout=6):
        proc = subprocess.run(
            [str(LAB), "--scenario", scenario],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("PHASE46_SUPERVISED_RECOVERY_OK", proc.stdout)
        return proc.stdout

    def test_help_and_invalid_cli(self):
        help_run = subprocess.run([str(LAB), "--help"], cwd=ROOT, text=True, capture_output=True, timeout=2)
        self.assertEqual(help_run.returncode, 0)
        self.assertIn("ready|timeout|restart-fail", help_run.stdout)
        bad = subprocess.run([str(LAB), "--scenario", "bogus"], cwd=ROOT, text=True, capture_output=True, timeout=2)
        self.assertNotEqual(bad.returncode, 0)

    def test_replacement_is_quarantined_until_real_readiness_and_hysteresis(self):
        out = self.run_lab("ready")
        self.assertIn("RESTART status=RESTARTED", out)
        self.assertIn("state=READY", out)
        self.assertIn("reason=NONE", out)
        self.assertIn("can_route=1", out)
        self.assertIn("health=HEALTHY", out)
        self.assertIn("circuit=CLOSED", out)
        self.assertIn("PROBE result=REFUSED", out)
        self.assertIn("PROBE result=SUCCESS", out)

    def test_slow_replacement_hits_startup_deadline_and_stays_unroutable(self):
        out = self.run_lab("timeout")
        self.assertIn("RESTART status=RESTARTED", out)
        self.assertIn("state=FAILED", out)
        self.assertIn("reason=STARTUP_TIMEOUT", out)
        self.assertIn("can_route=0", out)

    def test_failed_restart_never_enters_recovery_routing(self):
        out = self.run_lab("restart-fail")
        self.assertIn("state=FAILED", out)
        self.assertIn("reason=RESTART_NOT_COMPLETED", out)
        self.assertIn("can_route=0", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
