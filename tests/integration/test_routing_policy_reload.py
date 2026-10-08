#!/usr/bin/env python3
import os
import subprocess
import tempfile
import unittest

BIN = os.environ.get("ROUTING_POLICY_RELOAD_LAB_BIN", "build/routing_policy_reload_lab")

class RoutingPolicyReloadTests(unittest.TestCase):
    def run_lab(self, *args, check=True):
        return subprocess.run([BIN, *args], text=True, capture_output=True, check=check)

    def test_help_and_invalid_cli(self):
        p = self.run_lab("--help")
        self.assertIn("reload|rollback|invalid|stale-writer", p.stderr)
        p = self.run_lab("--scenario", "unknown", check=False)
        self.assertEqual(p.returncode, 2)

    def test_reload_publishes_complete_new_generation(self):
        p = self.run_lab("--scenario", "reload")
        self.assertIn("GENERATION=2", p.stdout)
        self.assertIn("ALPHA=3/8", p.stdout)
        self.assertIn("LATENCY_WEIGHT=10 LOAD_WEIGHT=1 SCORE_SLACK=7", p.stdout)
        self.assertIn("PHASE58_POLICY_RELOAD_OK", p.stdout)

    def test_invalid_candidate_never_changes_active_generation(self):
        p = self.run_lab("--scenario", "invalid")
        self.assertIn("GENERATION=1", p.stdout)
        self.assertIn("INVALID_RELOAD_REJECTED=1", p.stdout)

    def test_stale_writer_is_rejected_by_expected_generation(self):
        p = self.run_lab("--scenario", "stale-writer")
        self.assertIn("GENERATION=2", p.stdout)
        self.assertIn("STALE_WRITER_REJECTED=1", p.stdout)
        self.assertIn("LATENCY_WEIGHT=10 LOAD_WEIGHT=1", p.stdout)

    def test_rollback_restores_previous_values_as_new_generation(self):
        p = self.run_lab("--scenario", "rollback")
        self.assertIn("GENERATION=3", p.stdout)
        self.assertIn("ALPHA=1/4", p.stdout)
        self.assertIn("LATENCY_WEIGHT=1 LOAD_WEIGHT=2 SCORE_SLACK=80", p.stdout)

    def test_file_reload_is_validated_before_publish(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write("latency_weight=8\nload_weight=1\nscore_slack=5\n")
            path = f.name
        try:
            p = self.run_lab("--scenario", "reload", "--config", path)
            self.assertIn("GENERATION=2", p.stdout)
            self.assertIn("LATENCY_WEIGHT=8 LOAD_WEIGHT=1 SCORE_SLACK=5", p.stdout)
        finally:
            os.unlink(path)

if __name__ == "__main__":
    unittest.main(verbosity=2)
