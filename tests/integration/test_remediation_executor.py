#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("REMEDIATION_EXECUTOR_LAB_BIN", ROOT / "build/remediation_executor_lab"))


class RemediationExecutorIntegrationTests(unittest.TestCase):
    def run_lab(self, scenario):
        proc = subprocess.run(
            [str(LAB), "--scenario", scenario],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("PHASE45_REMEDIATION_EXECUTOR_OK", proc.stdout)
        return proc.stdout

    def test_help_and_invalid_cli(self):
        help_run = subprocess.run([str(LAB), "--help"], cwd=ROOT, text=True, capture_output=True, timeout=2)
        self.assertEqual(help_run.returncode, 0)
        self.assertIn("graceful|escalate|stale|spawn-fail", help_run.stdout)
        bad = subprocess.run([str(LAB), "--scenario", "bogus"], cwd=ROOT, text=True, capture_output=True, timeout=2)
        self.assertNotEqual(bad.returncode, 0)

    def test_graceful_restart_uses_sigterm_without_escalation(self):
        out = self.run_lab("graceful")
        self.assertIn("status=RESTARTED", out)
        self.assertIn("escalated=0", out)

    def test_stubborn_backend_escalates_to_sigkill(self):
        out = self.run_lab("escalate")
        self.assertIn("status=RESTARTED", out)
        self.assertIn("escalated=1", out)

    def test_stale_identity_is_refused_without_killing_live_process(self):
        out = self.run_lab("stale")
        self.assertIn("status=STALE_IDENTITY", out)

    def test_exec_failure_is_reported(self):
        out = self.run_lab("spawn-fail")
        self.assertIn("status=SPAWN_FAILED", out)
        self.assertIn("spawn_errno=2", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
