#!/usr/bin/env python3
import os
import pathlib
import subprocess
import unittest

SCRIPT = pathlib.Path("scripts/cgroup_cpu_lab.sh")
REQUIRE_REAL = os.environ.get("PHASE26_REQUIRE_REAL") == "1"


class CgroupCpuThrottleIntegrationTests(unittest.TestCase):
    def run_script(self, command, *, env=None, sudo=False, timeout=20):
        cmd = [str(SCRIPT), command]
        if sudo:
            cmd = ["sudo", "env", *[f"{k}={v}" for k, v in env.items()], *cmd]
            run_env = None
        else:
            run_env = os.environ.copy()
            if env:
                run_env.update(env)
        return subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
            env=run_env,
        )

    def test_help_contract(self):
        result = self.run_script("help")
        self.assertEqual(result.returncode, 0)
        for word in ("setup", "verify", "status", "cleanup", "cpu.max", "77"):
            self.assertIn(word, result.stdout)

    def test_unknown_command_is_rejected(self):
        result = self.run_script("not-a-command")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown command", result.stderr)

    def test_invalid_quota_relationship_is_rejected(self):
        env = {
            "PHASE26_CGROUP_NAME": f"shp26-invalid-{os.getpid()}",
            "PHASE26_CPU_QUOTA_US": "100000",
            "PHASE26_CPU_PERIOD_US": "100000",
        }
        result = self.run_script("setup", env=env)
        self.assertEqual(result.returncode, 2)
        self.assertIn("quota must be smaller", result.stderr)

    def test_real_cpu_quota_and_throttling(self):
        if os.geteuid() != 0 and not REQUIRE_REAL:
            self.skipTest("real Phase 26 cgroup CPU test requires root; milestone gate reruns with sudo")

        suffix = os.getpid()
        name = f"shp26-test-{suffix}"
        state_root = f"/run/self-healing-proxy-phase26-test-{suffix}"
        env = {
            "PHASE26_CGROUP_NAME": name,
            "PHASE26_STATE_ROOT": state_root,
            "PHASE26_CPU_QUOTA_US": "20000",
            "PHASE26_CPU_PERIOD_US": "100000",
            "PHASE26_WORKER_DURATION_MS": "15000",
            "PHASE26_VERIFY_WINDOW_SEC": "2",
            "CPU_PRESSURE_BIN": os.environ.get("CPU_PRESSURE_BIN", "build/cpu_pressure"),
        }

        use_sudo = os.geteuid() != 0
        cleanup = self.run_script("cleanup", env=env, sudo=use_sudo)
        if cleanup.returncode == 77:
            if REQUIRE_REAL:
                self.fail(cleanup.stderr)
            self.skipTest(cleanup.stderr.strip())

        try:
            setup = self.run_script("setup", env=env, sudo=use_sudo)
            if setup.returncode == 77:
                if REQUIRE_REAL:
                    self.fail(setup.stderr)
                self.skipTest(setup.stderr.strip())
            self.assertEqual(setup.returncode, 0, setup.stderr)
            self.assertIn("PHASE26_SETUP_OK", setup.stdout)
            self.assertIn("CPU_MAX=20000 100000", setup.stdout)

            duplicate = self.run_script("setup", env=env, sudo=use_sudo)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("already exists", duplicate.stderr)

            verify = self.run_script("verify", env=env, sudo=use_sudo, timeout=15)
            self.assertEqual(verify.returncode, 0, verify.stderr)
            self.assertIn("WORKER_MEMBERSHIP=PASS", verify.stdout)
            self.assertIn("WORKER_ALIVE_AFTER_THROTTLE=PASS", verify.stdout)
            self.assertIn("PHASE26_CPU_THROTTLING_VERIFIED", verify.stdout)

            deltas = {}
            for line in verify.stdout.splitlines():
                if line.startswith("CPU_STAT_DELTA_"):
                    key, value = line.split("=", 1)
                    deltas[key] = int(value)
            self.assertGreater(deltas["CPU_STAT_DELTA_USAGE_USEC"], 0)
            self.assertGreater(deltas["CPU_STAT_DELTA_PERIODS"], 0)
            self.assertGreater(deltas["CPU_STAT_DELTA_NR_THROTTLED"], 0)
            self.assertGreater(deltas["CPU_STAT_DELTA_THROTTLED_USEC"], 0)

            status = self.run_script("status", env=env, sudo=use_sudo)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("CPU_MAX=20000 100000", status.stdout)
            self.assertIn("nr_throttled", status.stdout)
        finally:
            first = self.run_script("cleanup", env=env, sudo=use_sudo)
            self.assertIn(first.returncode, (0, 77), first.stderr)
            second = self.run_script("cleanup", env=env, sudo=use_sudo)
            self.assertIn(second.returncode, (0, 77), second.stderr)


if __name__ == "__main__":
    unittest.main()
