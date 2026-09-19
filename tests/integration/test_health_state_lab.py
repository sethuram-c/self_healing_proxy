#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("HEALTH_STATE_LAB_BIN", ROOT / "build/health_state_lab"))


def run(*args):
    return subprocess.run([str(LAB), *args], cwd=ROOT, text=True, capture_output=True)


class HealthStateLabTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        help_run = run("--help")
        self.assertEqual(help_run.returncode, 0)
        self.assertIn("transient|trip-recover", help_run.stderr)
        bad = run("--scenario", "unknown")
        self.assertEqual(bad.returncode, 2)

    def test_transient_failure_recovers_without_ejection(self):
        result = run("--scenario", "transient")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("STEP=single_failure STATE=SUSPECT", result.stdout)
        self.assertIn("STEP=success_after_failure STATE=HEALTHY", result.stdout)
        self.assertNotIn("STATE=UNHEALTHY", result.stdout)
        self.assertIn("PHASE39_HEALTH_MODEL_OK", result.stdout)

    def test_trip_cooldown_and_hysteretic_recovery(self):
        result = run("--scenario", "trip-recover")
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("STEP=failure_3 STATE=UNHEALTHY", out)
        self.assertIn("STEP=cooldown_elapsed STATE=RECOVERING", out)
        self.assertIn("STEP=recovery_success_1 STATE=RECOVERING", out)
        self.assertIn("STEP=recovery_success_2 STATE=HEALTHY", out)
        self.assertIn("PHASE39_HEALTH_MODEL_OK", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
