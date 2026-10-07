#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN = os.environ.get("ROUTING_POLICY_LAB_BIN", str(ROOT / "build" / "routing_policy_lab"))


def run(*args):
    return subprocess.run([BIN, *args], text=True, capture_output=True, cwd=ROOT)


class RoutingPolicyTests(unittest.TestCase):
    def test_defaults_match_production_profile(self):
        proc = run("--scenario", "defaults")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("ALPHA=1/4", proc.stdout)
        self.assertIn("STALE_MS=5000", proc.stdout)
        self.assertIn("LATENCY_WEIGHT=1", proc.stdout)
        self.assertIn("LOAD_WEIGHT=2", proc.stdout)
        self.assertIn("SCORE_SLACK=80", proc.stdout)
        self.assertIn("PHASE57_ROUTING_POLICY_OK", proc.stdout)

    def test_latency_heavy_override_changes_only_soft_preference(self):
        with tempfile.TemporaryDirectory() as td:
            cfg = pathlib.Path(td) / "routing.conf"
            cfg.write_text("latency_weight=10\nload_weight=1\nscore_slack=0\n", encoding="utf-8")
            default = run("--scenario", "decision")
            tuned = run("--scenario", "decision", "--config", str(cfg))
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertEqual(tuned.returncode, 0, tuned.stderr)
        self.assertIn("SELECTED=edge-slower-idle RANK=SITE", default.stdout)
        self.assertIn("SELECTED=edge-fast-hot RANK=SITE", tuned.stdout)
        self.assertIn("PHASE57_ROUTING_POLICY_OK", tuned.stdout)

    def test_unknown_duplicate_and_unsafe_values_fail_closed(self):
        bad_configs = [
            "unknown=1\n",
            "load_weight=2\nload_weight=3\n",
            "latency_alpha_numerator=5\nlatency_alpha_denominator=4\n",
            "latency_stale_after_ms=600001\n",
            "latency_weight=0\nload_weight=0\n",
            "score_slack=4001\n",
        ]
        for text in bad_configs:
            with self.subTest(text=text):
                with tempfile.TemporaryDirectory() as td:
                    cfg = pathlib.Path(td) / "bad.conf"
                    cfg.write_text(text, encoding="utf-8")
                    proc = run("--scenario", "show", "--config", str(cfg))
                self.assertNotEqual(proc.returncode, 0)
                self.assertIn("invalid routing policy", proc.stderr)

    def test_help_and_invalid_cli(self):
        help_proc = run("--help")
        self.assertEqual(help_proc.returncode, 0)
        self.assertIn("Usage:", help_proc.stderr)
        bad = run("--scenario", "nonsense")
        self.assertEqual(bad.returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
