#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "cgroup_v2_lab.sh"
REQUIRE_REAL = os.environ.get("PHASE24_REQUIRE_REAL") == "1"


def run_script(command: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(
        [str(SCRIPT), command],
        cwd=REPO_ROOT,
        env=merged,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class CgroupV2IntegrationTests(unittest.TestCase):
    def test_help_contract(self) -> None:
        result = run_script("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("setup", result.stdout)
        self.assertIn("verify", result.stdout)
        self.assertIn("status", result.stdout)
        self.assertIn("cleanup", result.stdout)
        self.assertIn("cgroup v2", result.stdout.lower())
        self.assertIn("77", result.stdout)
        self.assertIn("memory.max", result.stdout)
        self.assertIn("cpu.max", result.stdout)

    def test_unknown_command_is_rejected(self) -> None:
        result = run_script("not-a-command")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown command", result.stderr)

    def test_real_cgroup_v2_membership_and_accounting(self) -> None:
        if os.geteuid() != 0:
            if REQUIRE_REAL:
                self.fail("PHASE24_REQUIRE_REAL=1 but test is not running as root")
            self.skipTest("real Phase 24 cgroup v2 topology requires root")

        suffix = str(os.getpid())
        name = f"shp24-test-{suffix}"
        state_root = f"/run/self-healing-proxy-phase24-test-{suffix}"
        env = {
            "PHASE24_CGROUP_NAME": name,
            "PHASE24_STATE_ROOT": state_root,
            "PHASE24_WORKER_SECONDS": "120",
        }

        run_script("cleanup", env)
        setup = run_script("setup", env)
        if setup.returncode == 77:
            if REQUIRE_REAL:
                self.fail(f"required cgroup v2 setup skipped:\n{setup.stderr}")
            self.skipTest(setup.stderr.strip() or "cgroup v2 runtime unavailable")
        self.assertEqual(setup.returncode, 0, setup.stderr)
        self.assertIn("PHASE24_SETUP_OK", setup.stdout)
        self.assertIn(f"CGROUP_PATH=/sys/fs/cgroup/{name}", setup.stdout)

        try:
            duplicate = run_script("setup", env)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("already exists", duplicate.stderr)

            verify = run_script("verify", env)
            if verify.returncode == 77:
                if REQUIRE_REAL:
                    self.fail(f"required cgroup accounting unavailable:\n{verify.stderr}")
                self.skipTest(verify.stderr.strip() or "required controller files unavailable")
            self.assertEqual(verify.returncode, 0, verify.stderr)
            self.assertIn("WORKER_MEMBERSHIP=PASS", verify.stdout)
            self.assertIn("PIDS_CURRENT=", verify.stdout)
            self.assertIn("MEMORY_CURRENT=", verify.stdout)
            self.assertIn("CPU_USAGE_USEC=", verify.stdout)
            self.assertIn("PHASE24_CGROUP_VERIFIED", verify.stdout)

            status = run_script("status", env)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("CGROUP_STATE=present", status.stdout)
            self.assertIn("--- cgroup.procs ---", status.stdout)
            self.assertIn("--- cpu.stat ---", status.stdout)
            self.assertIn("--- memory.current ---", status.stdout)
            self.assertIn("--- pids.current ---", status.stdout)
        finally:
            cleanup = run_script("cleanup", env)
            self.assertEqual(cleanup.returncode, 0, cleanup.stderr)
            self.assertIn("PHASE24_CLEANUP_OK", cleanup.stdout)

        cleanup_again = run_script("cleanup", env)
        self.assertEqual(cleanup_again.returncode, 0, cleanup_again.stderr)
        self.assertIn("PHASE24_CLEANUP_OK", cleanup_again.stdout)
        self.assertFalse(pathlib.Path(f"/sys/fs/cgroup/{name}").exists())
        self.assertFalse(pathlib.Path(state_root).exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
