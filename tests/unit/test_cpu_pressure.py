#!/usr/bin/env python3
import os
import subprocess
import unittest

CPU_PRESSURE_BIN = os.environ.get("CPU_PRESSURE_BIN", "build/cpu_pressure")


class CpuPressureUnitTests(unittest.TestCase):
    def run_worker(self, *args, timeout=5):
        return subprocess.run(
            [CPU_PRESSURE_BIN, *args],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )

    def test_help(self):
        result = self.run_worker("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--duration-ms", result.stdout)
        self.assertIn("--wait-for-sigcont", result.stdout)

    def test_missing_duration_is_rejected(self):
        result = self.run_worker()
        self.assertEqual(result.returncode, 2)
        self.assertIn("--duration-ms is required", result.stderr)

    def test_invalid_duration_is_rejected(self):
        for value in ("0", "banana", "600001"):
            with self.subTest(value=value):
                result = self.run_worker("--duration-ms", value)
                self.assertEqual(result.returncode, 2)

    def test_short_cpu_burn_completes(self):
        result = self.run_worker("--duration-ms", "100", timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CPU_PRESSURE_READY", result.stdout)
        self.assertIn("CPU_PRESSURE_START", result.stdout)
        self.assertIn("CPU_PRESSURE_COMPLETE", result.stdout)


if __name__ == "__main__":
    unittest.main()
