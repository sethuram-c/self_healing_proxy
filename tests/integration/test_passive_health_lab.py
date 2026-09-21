#!/usr/bin/env python3
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("PASSIVE_HEALTH_LAB_BIN", ROOT / "build/passive_health_lab"))


def run(*args):
    return subprocess.run(
        [str(LAB), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


class PassiveHealthLabTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        proc = run("--help")
        self.assertEqual(proc.returncode, 0)
        self.assertIn("mixed|trip-recover|neutral", proc.stderr)

        proc = run("--scenario", "bogus")
        self.assertEqual(proc.returncode, 2)

    def test_mixed_backend_and_client_outcomes(self):
        proc = run("--scenario", "mixed")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("OUTCOME=CLIENT_DISCONNECT EVIDENCE=NEUTRAL STATE=HEALTHY", proc.stdout)
        self.assertIn("OUTCOME=UPSTREAM_RESPONSE_TIMEOUT EVIDENCE=FAILURE STATE=SUSPECT", proc.stdout)
        self.assertIn("OUTCOME=UPSTREAM_RESPONSE_COMPLETE EVIDENCE=SUCCESS STATE=HEALTHY", proc.stdout)
        self.assertIn("PHASE41_PASSIVE_HEALTH_OK", proc.stdout)

    def test_neutral_failures_do_not_poison_backend(self):
        proc = run("--scenario", "neutral")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("OUTCOME=LOCAL_OVERLOAD EVIDENCE=NEUTRAL STATE=HEALTHY", proc.stdout)
        self.assertIn("OUTCOME=UPSTREAM_HTTP_STATUS EVIDENCE=NEUTRAL STATE=HEALTHY", proc.stdout)
        self.assertIn("SUCCESSES=1 FAILURES=0", proc.stdout)

    def test_passive_failures_trip_and_recovery_requires_two_successes(self):
        proc = run("--scenario", "trip-recover")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("OUTCOME=UPSTREAM_RESET_BEFORE_RESPONSE EVIDENCE=FAILURE STATE=UNHEALTHY", proc.stdout)
        self.assertIn("OUTCOME=COOLDOWN EVIDENCE=NEUTRAL STATE=RECOVERING", proc.stdout)
        self.assertIn("STEP=6 OUTCOME=UPSTREAM_RESPONSE_COMPLETE EVIDENCE=SUCCESS STATE=RECOVERING", proc.stdout)
        self.assertIn("STEP=7 OUTCOME=UPSTREAM_RESPONSE_COMPLETE EVIDENCE=SUCCESS STATE=HEALTHY", proc.stdout)
        self.assertIn("PHASE41_PASSIVE_HEALTH_OK", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
