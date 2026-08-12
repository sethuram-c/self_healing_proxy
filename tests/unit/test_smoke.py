import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build" / "microproxy"


class SmokeTests(unittest.TestCase):

    def test_program_starts_successfully(self):
        result = subprocess.run(
            [str(BINARY)],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn(
            "Self-Healing Micro-Proxy",
            result.stdout,
        )
        self.assertIn(
            "Phase 01: Environment Ready",
            result.stdout,
        )
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
