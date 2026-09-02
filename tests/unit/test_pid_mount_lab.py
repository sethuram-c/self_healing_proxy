#!/usr/bin/env python3
"""Phase 22: deterministic CLI checks and gated kernel-isolation exercise."""
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "pid_mount_lab"


class PidMountLabTests(unittest.TestCase):
    def invoke(self, *args):
        return subprocess.run([str(BIN), *args], text=True,
                              capture_output=True, timeout=10, check=False)

    def test_help(self):
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PID+mount namespace", result.stdout)

    def test_invalid_option(self):
        self.assertEqual(self.invoke("--bad").returncode, 2)

    def test_kernel_isolation(self):
        result = self.invoke()
        if result.returncode == 77:
            self.skipTest(result.stderr.strip())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PHASE22_ISOLATION_VERIFIED", result.stdout)
        self.assertRegex(result.stdout, r"ISOLATED_INIT_PID=1\b")
        self.assertRegex(result.stdout, r"ISOLATED_INIT_PPID=0\b")
        self.assertRegex(result.stdout, r"ISOLATED_WORKER_PID=2 PPID=1\b")


if __name__ == "__main__":
    unittest.main(verbosity=2)
