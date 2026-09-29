#!/usr/bin/env python3
import os, subprocess, unittest
BIN=os.environ.get("EDGE_AWARE_SELECTION_LAB_BIN","build/edge_aware_selection_lab")
class T(unittest.TestCase):
 def run_s(self,s): return subprocess.run([BIN,"--scenario",s],text=True,capture_output=True,check=True).stdout
 def test_help_and_invalid_cli(self):
  self.assertIn("prefer-local",subprocess.run([BIN,"--help"],text=True,capture_output=True,check=True).stdout)
  self.assertNotEqual(subprocess.run([BIN,"--bad"],text=True,capture_output=True).returncode,0)
 def test_prefers_site_and_preserves_fairness(self):
  o=self.run_s("prefer-local"); self.assertEqual(o.count("RANK=SITE"),4); self.assertIn("PICK=edge-a",o); self.assertIn("PICK=edge-b",o)
 def test_falls_back_zone_then_remote(self):
  o=self.run_s("fallback"); self.assertIn("FIRST=zone-c RANK=ZONE",o); self.assertIn("SECOND=remote-d RANK=REMOTE",o)
 def test_no_eligible_backend_fails_closed(self): self.assertIn("SELECTED=NONE",self.run_s("fail-closed"))
if __name__=="__main__": unittest.main(verbosity=2)
