#!/usr/bin/env python3
import os, subprocess, unittest
BIN=os.environ.get("CIRCUIT_BREAKER_LAB_BIN","build/circuit_breaker_lab")
class Tests(unittest.TestCase):
    def run_s(self,s):
        return subprocess.run([BIN,"--scenario",s],text=True,capture_output=True,check=True).stdout
    def test_help_and_invalid_cli(self):
        self.assertEqual(subprocess.run([BIN,"--help"]).returncode,0)
        self.assertNotEqual(subprocess.run([BIN,"--scenario","bad"],capture_output=True).returncode,0)
    def test_open_backend_is_skipped(self):
        out=self.run_s("open-skip")
        self.assertIn("B_STATE=OPEN",out); self.assertNotIn("PICK=backend-b",out)
        self.assertIn("PICK=backend-a",out); self.assertIn("PICK=backend-c",out)
    def test_failures_outside_window_do_not_trip(self):
        out=self.run_s("window")
        self.assertIn("RECENT_FAILURES=2",out); self.assertIn("B_STATE=CLOSED",out)
    def test_half_open_allows_one_trial_and_success_closes(self):
        out=self.run_s("half-open")
        self.assertIn("B_STATE=HALF_OPEN",out); self.assertIn("TRIAL=backend-b",out)
        self.assertNotIn("SECOND=backend-b",out); self.assertIn("B_FINAL=CLOSED",out)
if __name__=="__main__": unittest.main(verbosity=2)
