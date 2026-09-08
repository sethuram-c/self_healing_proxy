#!/usr/bin/env python3
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "control-plane" / "isolation_launcher.py"
REQUIRE_REAL = os.environ.get("PHASE28_REQUIRE_REAL") == "1"


class Stage3HardeningTests(unittest.TestCase):
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

    def test_help_exposes_closure_operations(self) -> None:
        cp = self.run_launcher("--help")
        self.assertEqual(cp.returncode, 0, cp.stderr)
        for token in (
            "PID/mount",
            "privilege drop",
            "doctor",
            "recover",
            "cleanup",
        ):
            self.assertIn(token, cp.stdout)

    def test_root_workload_identity_is_rejected(self) -> None:
        cp = self.run_launcher(
            "doctor",
            env={"PHASE28_RUN_UID": "0", "PHASE28_RUN_GID": "0"},
        )
        self.assertEqual(cp.returncode, 1)
        self.assertIn("refuses a host-root workload", cp.stderr)

    def real_env(self) -> dict[str, str]:
        suffix = str(os.getpid() % 100000)
        third_octet = 40 + (os.getpid() % 150)
        uid = os.environ.get("SUDO_UID", "65534")
        gid = os.environ.get("SUDO_GID", "65534")
        if uid == "0":
            uid = "65534"
        return {
            "PHASE27_NS_NAME": f"shp27-h-{suffix}",
            "PHASE27_CGROUP_NAME": f"shp27-hc-{suffix}",
            "PHASE27_HOST_IF": f"p28h{suffix}"[:15],
            "PHASE27_NS_IF": f"p28n{suffix}"[:15],
            "PHASE27_HOST_ADDR": f"10.228.{third_octet}.1/30",
            "PHASE27_NS_ADDR": f"10.228.{third_octet}.2/30",
            "PHASE27_CPU_MAX": "50000 100000",
            "PHASE27_MEMORY_MAX_MIB": "64",
            "PHASE27_PIDS_MAX": "32",
            "PHASE27_STATE_DIR": f"/run/self-healing-proxy-phase28-test-{suffix}",
            "PHASE28_RUN_UID": uid,
            "PHASE28_RUN_GID": gid,
        }

    def require_real_runtime(self) -> None:
        if os.geteuid() != 0:
            if REQUIRE_REAL:
                self.fail("PHASE28_REQUIRE_REAL=1 requires running this test as root")
            self.skipTest("real Phase 28 Stage 3 closure requires root")
        for binary in ("ip", "ping", "nsenter"):
            if shutil.which(binary) is None:
                if REQUIRE_REAL:
                    self.fail(f"{binary} is required for Phase 28")
                self.skipTest(f"{binary} unavailable")
        fs = subprocess.run(
            ["stat", "-fc", "%T", "/sys/fs/cgroup"],
            text=True,
            stdout=subprocess.PIPE,
        ).stdout.strip()
        if fs != "cgroup2fs" or not os.access("/sys/fs/cgroup", os.W_OK):
            if REQUIRE_REAL:
                self.fail("writable cgroup v2 hierarchy is required")
            self.skipTest("writable cgroup v2 hierarchy unavailable")

    def test_real_stage3_hardening_and_stale_recovery(self) -> None:
        self.require_real_runtime()
        env = self.real_env()
        state_dir = Path(env["PHASE27_STATE_DIR"])
        state_file = state_dir / f"{env['PHASE27_CGROUP_NAME']}.json"
        cgroup_path = Path("/sys/fs/cgroup") / env["PHASE27_CGROUP_NAME"]
        victim: subprocess.Popen[str] | None = None

        self.run_launcher("cleanup", env=env)
        try:
            start = self.run_launcher("start", "--", "/usr/bin/sleep", "30", env=env)
            if start.returncode == 77 and not REQUIRE_REAL:
                self.skipTest(start.stderr.strip())
            self.assertEqual(start.returncode, 0, start.stderr)
            self.assertIn("PHASE28_START_OK", start.stdout)

            verify = self.run_launcher("verify", env=env)
            self.assertEqual(verify.returncode, 0, verify.stderr)
            for marker in (
                "PHASE28_HARDENED_ISOLATION_VERIFIED",
                "WORKLOAD_NAMESPACE_PID=1",
                "FRESH_PROC=PASS",
                "NO_NEW_PRIVS=1",
                "EFFECTIVE_CAPABILITIES=0",
            ):
                self.assertIn(marker, verify.stdout)

            doctor = self.run_launcher("doctor", env=env)
            self.assertEqual(doctor.returncode, 0, doctor.stderr)
            self.assertIn("INSTANCE_STATE=LIVE", doctor.stdout)

            state = json.loads(state_file.read_text(encoding="utf-8"))
            workload_pid = int(state["workload_pid"])
            status_text = Path(f"/proc/{workload_pid}/status").read_text(encoding="utf-8")
            self.assertRegex(status_text, r"(?m)^NSpid:\s+\d+\s+1$")
            self.assertNotEqual(os.readlink("/proc/self/ns/pid"), os.readlink(f"/proc/{workload_pid}/ns/pid"))
            self.assertNotEqual(os.readlink("/proc/self/ns/mnt"), os.readlink(f"/proc/{workload_pid}/ns/mnt"))

            # PID-reuse safety proof: replace the recorded workload PID with an
            # unrelated host process. Cleanup must kill only tasks currently in
            # the owned cgroup, never the recorded numeric PID by itself.
            victim = subprocess.Popen(["/usr/bin/sleep", "30"], text=True)
            state["workload_pid"] = victim.pid
            state["pid"] = victim.pid
            state["workload_starttime"] = 1
            state_file.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")

            cleanup = self.run_launcher("cleanup", env=env)
            self.assertEqual(cleanup.returncode, 0, cleanup.stderr)
            self.assertIsNone(victim.poll(), "cleanup signaled an unrelated host PID from stale JSON")
            self.assertFalse(cgroup_path.exists())
            self.assertFalse(state_dir.exists())

            # Explicit stale-state recovery: allow the isolated workload to
            # finish naturally, classify it STALE, then recover its leftovers.
            start2 = self.run_launcher("start", "--", "/usr/bin/sleep", "1", env=env)
            self.assertEqual(start2.returncode, 0, start2.stderr)
            time.sleep(1.5)
            doctor2 = self.run_launcher("doctor", env=env)
            self.assertEqual(doctor2.returncode, 0, doctor2.stderr)
            self.assertIn("INSTANCE_STATE=STALE", doctor2.stdout)
            recover = self.run_launcher("recover", env=env)
            self.assertEqual(recover.returncode, 0, recover.stderr)
            self.assertIn("PHASE28_STALE_RECOVERY_OK", recover.stdout)
            final_doctor = self.run_launcher("doctor", env=env)
            self.assertEqual(final_doctor.returncode, 0, final_doctor.stderr)
            self.assertIn("INSTANCE_STATE=CLEAN", final_doctor.stdout)
        finally:
            self.run_launcher("cleanup", env=env)
            if victim is not None and victim.poll() is None:
                victim.terminate()
                try:
                    victim.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    victim.kill()
                    victim.wait(timeout=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
