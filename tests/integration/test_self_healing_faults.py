#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LAB = pathlib.Path(os.environ.get("SELF_HEALING_FAULT_LAB_BIN", ROOT / "build/self_healing_fault_lab"))


def run_lab(*args):
    return subprocess.run([str(LAB), *args], cwd=ROOT, text=True, capture_output=True, check=False)


def events(stdout):
    result = []
    for line in stdout.splitlines():
        if line.startswith("{"):
            result.append(json.loads(line))
    return result


class SelfHealingFaultTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        help_run = run_lab("--help")
        self.assertEqual(help_run.returncode, 0, help_run.stderr)
        self.assertIn("recover|storm", help_run.stdout)
        bad = run_lab("--scenario", "bogus")
        self.assertNotEqual(bad.returncode, 0)

    def test_fault_to_recovery_is_observable_end_to_end(self):
        proc = run_lab("--scenario", "recover")
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        ev = events(proc.stdout)
        self.assertGreaterEqual(len(ev), 8, proc.stdout)
        self.assertTrue(all(e["schema"] == "shp.healing.v1" for e in ev))
        self.assertEqual([e["sequence"] for e in ev], list(range(1, len(ev) + 1)))
        names = [e["event"] for e in ev]
        for required in ["baseline_healthy", "fault_injected", "remediation_approved",
                         "fleet_admitted", "replacement_quarantined", "readiness_success",
                         "recovery_complete"]:
            self.assertIn(required, names)
        final = ev[-1]
        self.assertEqual(final["health_state"], "HEALTHY")
        self.assertEqual(final["circuit_state"], "CLOSED")
        self.assertEqual(final["recovery_state"], "READY")
        self.assertEqual(final["action"], "ROUTE_ENABLED")
        self.assertIn("PHASE48_SELF_HEALING_RECOVERY_OK", proc.stdout)

    def test_restart_storm_suppression_is_observable(self):
        proc = run_lab("--scenario", "storm")
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        ev = events(proc.stdout)
        reasons = [e["reason"] for e in ev if e["event"] == "restart_suppressed"]
        self.assertIn("CONCURRENCY_LIMIT", reasons)
        self.assertIn("WINDOW_BUDGET", reasons)
        self.assertIn("PHASE48_RESTART_STORM_PROTECTION_OK", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
