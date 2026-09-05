#!/usr/bin/env python3
import os
import pathlib
import subprocess
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BINARY = pathlib.Path(os.environ.get("MEMORY_PRESSURE_BIN", REPO_ROOT / "build" / "memory_pressure"))


class MemoryPressureUnitTests(unittest.TestCase):
    def run_binary(self, *args: str, timeout: float = 5.0) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(BINARY), *args],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )

    def test_help(self) -> None:
        result = self.run_binary("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--mib", result.stdout)
        self.assertIn("--wait-for-sigcont", result.stdout)

    def test_missing_mib_is_rejected(self) -> None:
        result = self.run_binary()
        self.assertEqual(result.returncode, 2)
        self.assertIn("--mib is required", result.stderr)

    def test_invalid_mib_is_rejected(self) -> None:
        result = self.run_binary("--mib", "0")
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid --mib value", result.stderr)

    def test_small_allocation_is_really_touched_and_completes(self) -> None:
        result = self.run_binary("--mib", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MEMORY_PRESSURE_READY", result.stdout)
        self.assertIn("MEMORY_PRESSURE_START", result.stdout)
        self.assertIn("MEMORY_PRESSURE_COMPLETE mib=1", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
