#!/usr/bin/env python3
import os
import pathlib
import subprocess
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "cgroup_memory_lab.sh"
WORKER = REPO_ROOT / "build" / "memory_pressure"
REQUIRE_REAL = os.environ.get("PHASE25_REQUIRE_REAL") == "1"


def run_script(command: str, env: dict[str, str] | None = None, timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
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
        timeout=timeout,
        check=False,
    )


class CgroupMemoryOomIntegrationTests(unittest.TestCase):
    def test_help_contract(self) -> None:
        result = run_script("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("memory.max", result.stdout)
        self.assertIn("memory.events", result.stdout)
        self.assertIn("OOM", result.stdout)
        self.assertIn("setup", result.stdout)
        self.assertIn("verify", result.stdout)
        self.assertIn("cleanup", result.stdout)
        self.assertIn("77", result.stdout)

    def test_unknown_command_is_rejected(self) -> None:
        result = run_script("not-a-command")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown command", result.stderr)

    def test_invalid_limit_relationship_is_rejected(self) -> None:
        env = {
            "PHASE25_MEMORY_MAX_MIB": "64",
            "PHASE25_TARGET_MIB": "32",
        }
        result = run_script("status", env)
        self.assertEqual(result.returncode, 2)
        self.assertIn("must be > memory max", result.stderr)

    def test_real_memory_limit_and_oom_kill(self) -> None:
        if os.geteuid() != 0:
            if REQUIRE_REAL:
                self.fail("PHASE25_REQUIRE_REAL=1 but test is not running as root")
            self.skipTest("real Phase 25 memory/OOM test requires root")

        if not WORKER.exists():
            self.fail(f"missing memory-pressure worker: {WORKER}")

        suffix = str(os.getpid())
        name = f"shp25-test-{suffix}"
        state_root = f"/run/self-healing-proxy-phase25-test-{suffix}"
        env = {
            "PHASE25_CGROUP_NAME": name,
            "PHASE25_STATE_ROOT": state_root,
            "PHASE25_MEMORY_MAX_MIB": "32",
            "PHASE25_TARGET_MIB": "128",
            "PHASE25_VERIFY_TIMEOUT_SECONDS": "20",
            "PHASE25_WORKER_BIN": str(WORKER),
        }

        run_script("cleanup", env)
        setup = run_script("setup", env)
        if setup.returncode == 77:
            if REQUIRE_REAL:
                self.fail(f"required Phase 25 setup skipped:\n{setup.stderr}")
            self.skipTest(setup.stderr.strip() or "memory controller unavailable")
        self.assertEqual(setup.returncode, 0, setup.stderr)
        self.assertIn("PHASE25_SETUP_OK", setup.stdout)
        self.assertIn("MEMORY_MAX_BYTES=33554432", setup.stdout)

        try:
            duplicate = run_script("setup", env)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("already exists", duplicate.stderr)

            verify = run_script("verify", env, timeout=30.0)
            if verify.returncode == 77:
                if REQUIRE_REAL:
                    self.fail(f"required Phase 25 verification skipped:\n{verify.stderr}")
                self.skipTest(verify.stderr.strip() or "memory controller unavailable")
            self.assertEqual(verify.returncode, 0, verify.stderr)
            self.assertIn("WORKER_OOM_TERMINATION=PASS", verify.stdout)
            self.assertIn("MEMORY_EVENTS_MAX=", verify.stdout)
            self.assertIn("MEMORY_EVENTS_OOM=", verify.stdout)
            self.assertIn("MEMORY_EVENTS_OOM_KILL=", verify.stdout)
            self.assertIn("CGROUP_POPULATED=0", verify.stdout)
            self.assertIn("PHASE25_MEMORY_OOM_VERIFIED", verify.stdout)

            status = run_script("status", env)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("CGROUP_STATE=present", status.stdout)
            self.assertIn("--- memory.max ---", status.stdout)
            self.assertIn("--- memory.events ---", status.stdout)
            self.assertIn("MEMORY_PRESSURE_READY", status.stdout)
            self.assertNotIn("MEMORY_PRESSURE_COMPLETE", status.stdout)
        finally:
            cleanup = run_script("cleanup", env)
            self.assertEqual(cleanup.returncode, 0, cleanup.stderr)
            self.assertIn("PHASE25_CLEANUP_OK", cleanup.stdout)

        cleanup_again = run_script("cleanup", env)
        self.assertEqual(cleanup_again.returncode, 0, cleanup_again.stderr)
        self.assertIn("PHASE25_CLEANUP_OK", cleanup_again.stdout)
        self.assertFalse(pathlib.Path(f"/sys/fs/cgroup/{name}").exists())
        self.assertFalse(pathlib.Path(state_root).exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
