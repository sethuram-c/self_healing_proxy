#!/usr/bin/env python3
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "control-plane" / "isolation_launcher.py"
REQUIRE_REAL = os.environ.get("PHASE27_REQUIRE_REAL") == "1"


class IsolationLauncherTests(unittest.TestCase):
    def run_launcher(self, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        merged = os.environ.copy()
        if env:
            merged.update(env)
        return subprocess.run(
            [sys.executable, str(LAUNCHER), *args],
            cwd=ROOT,
            env=merged,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_help_contract(self) -> None:
        cp = self.run_launcher("--help")
        self.assertEqual(cp.returncode, 0, cp.stderr)
        for token in ("start", "verify", "status", "cleanup", "network namespace", "cgroup v2"):
            self.assertIn(token, cp.stdout)

    def test_unsafe_name_is_rejected_before_privileged_work(self) -> None:
        cp = self.run_launcher("status", env={"PHASE27_NS_NAME": "unsafe-name"})
        self.assertEqual(cp.returncode, 1)
        self.assertIn("must start with", cp.stderr)

    def test_start_requires_command(self) -> None:
        cp = self.run_launcher("start")
        if cp.returncode == 77:
            self.skipTest("privilege check happened before command validation in this runtime")
        self.assertNotEqual(cp.returncode, 0)
        self.assertIn("requires a command", cp.stderr)

    def test_real_composed_isolation(self) -> None:
        if os.geteuid() != 0:
            if REQUIRE_REAL:
                self.fail("PHASE27_REQUIRE_REAL=1 requires running this test as root")
            self.skipTest("real Phase 27 composed isolation requires root")
        if shutil.which("ip") is None or shutil.which("ping") is None:
            if REQUIRE_REAL:
                self.fail("iproute2/ping are required for Phase 27")
            self.skipTest("iproute2/ping unavailable")
        if subprocess.run(["stat", "-fc", "%T", "/sys/fs/cgroup"], text=True, stdout=subprocess.PIPE).stdout.strip() != "cgroup2fs":
            if REQUIRE_REAL:
                self.fail("cgroup v2 is required")
            self.skipTest("cgroup v2 unavailable")
        if not os.access("/sys/fs/cgroup", os.W_OK):
            if REQUIRE_REAL:
                self.fail("cgroup v2 root is not writable")
            self.skipTest("cgroup v2 root is not writable")

        suffix = str(os.getpid() % 100000)
        ns_name = f"shp27-n-{suffix}"
        cg_name = f"shp27-c-{suffix}"
        host_if = f"p27h{suffix}"[:15]
        ns_if = f"p27n{suffix}"[:15]
        third_octet = 40 + (os.getpid() % 150)
        host_addr = f"10.227.{third_octet}.1/30"
        ns_addr = f"10.227.{third_octet}.2/30"
        state_dir = f"/run/self-healing-proxy-phase27-test-{suffix}"
        env = {
            "PHASE27_NS_NAME": ns_name,
            "PHASE27_CGROUP_NAME": cg_name,
            "PHASE27_HOST_IF": host_if,
            "PHASE27_NS_IF": ns_if,
            "PHASE27_HOST_ADDR": host_addr,
            "PHASE27_NS_ADDR": ns_addr,
            "PHASE27_CPU_MAX": "50000 100000",
            "PHASE27_MEMORY_MAX_MIB": "64",
            "PHASE27_PIDS_MAX": "32",
            "PHASE27_STATE_DIR": state_dir,
        }

        self.run_launcher("cleanup", env=env)
        try:
            start = self.run_launcher("start", "--", "/usr/bin/sleep", "30", env=env)
            if start.returncode == 77 and not REQUIRE_REAL:
                self.skipTest(start.stderr.strip())
            self.assertEqual(start.returncode, 0, start.stderr)
            self.assertIn("PHASE27_START_OK", start.stdout)

            duplicate = self.run_launcher("start", "--", "/usr/bin/sleep", "30", env=env)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("already exists", duplicate.stderr)

            verify = self.run_launcher("verify", env=env)
            self.assertEqual(verify.returncode, 0, verify.stderr)
            for marker in (
                "PHASE27_ISOLATION_VERIFIED",
                "HOST_TO_NAMESPACE_PING=PASS",
                "NAMESPACE_TO_HOST_PING=PASS",
                "WORKLOAD_CGROUP=/" + cg_name,
            ):
                self.assertIn(marker, verify.stdout)

            status = self.run_launcher("status", env=env)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("WORKLOAD_ALIVE=1", status.stdout)
            self.assertIn("CPU_MAX_BEGIN", status.stdout)
            self.assertIn("MEMORY_MAX_BEGIN", status.stdout)
            self.assertIn("PIDS_MAX_BEGIN", status.stdout)

            state_file = Path(state_dir) / f"{cg_name}.json"
            state = json.loads(state_file.read_text(encoding="utf-8"))
            pid = int(state["pid"])
            self.assertTrue(Path(f"/proc/{pid}").exists())
            self.assertIn(f"0::/{cg_name}", Path(f"/proc/{pid}/cgroup").read_text(encoding="utf-8"))
            self.assertNotEqual(os.readlink("/proc/self/ns/net"), os.readlink(f"/proc/{pid}/ns/net"))
        finally:
            first = self.run_launcher("cleanup", env=env)
            self.assertEqual(first.returncode, 0, first.stderr)
            second = self.run_launcher("cleanup", env=env)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertFalse(Path("/sys/fs/cgroup", cg_name).exists())
            self.assertFalse(Path(state_dir).exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
