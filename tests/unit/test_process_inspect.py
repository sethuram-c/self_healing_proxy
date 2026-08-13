import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build" / "process_inspect"


class ProcessInspectTests(unittest.TestCase):

    def test_process_inspect_runs(self):
        result = subprocess.run(
            [str(BINARY)],
            input="\n",
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("PID:", result.stdout)
        self.assertIn("Parent PID:", result.stdout)
        self.assertIn("Opened file descriptor:", result.stdout)
        self.assertIn("Wrote 8 bytes", result.stdout)


if __name__ == "__main__":
    unittest.main()
