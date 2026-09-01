#!/usr/bin/env python3
"""Phase 21: non-privileged namespace inspection contract tests."""

import os
import re
import subprocess
import sys
import unittest

BIN = os.environ.get("NAMESPACE_INSPECT_BIN", "build/namespace_inspect")
NAMES = ("user", "pid", "mnt", "net", "ipc", "uts", "cgroup", "time")


class NamespaceInspectTests(unittest.TestCase):
    def invoke(self, *args):
        return subprocess.run([BIN, *args], capture_output=True, text=True,
                              timeout=5, check=False)

    def test_self_inspection(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"Observer PID=\d+ PPID=\d+; target PID=\d+")
        for name in NAMES:
            line = next((x for x in result.stdout.splitlines()
                         if x.startswith(name + " ") or x.startswith(name + "\t")), None)
            self.assertIsNotNone(line, name)
            if "unavailable" not in line:
                match = re.fullmatch(
                    rf"{name}\s+self=({name}:\[\d+\]) "
                    rf"target=({name}:\[\d+\]) shared=yes", line)
                self.assertIsNotNone(match, line)
                self.assertEqual(match.group(1), match.group(2))

    def test_target_process_namespace_identity(self):
        child = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(20)"])
        try:
            result = self.invoke(str(child.pid))
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in NAMES:
                path = f"/proc/{child.pid}/ns/{name}"
                if os.path.exists(path):
                    link = os.readlink(path)
                    self.assertIn(f"target={link}", result.stdout)
        finally:
            child.terminate()
            child.wait(timeout=5)

    def test_invalid_arguments(self):
        for args in [("0",), ("-1",), ("abc",), ("1x",), ("1", "2")]:
            with self.subTest(args=args):
                self.assertNotEqual(self.invoke(*args).returncode, 0)

    def test_nonexistent_pid(self):
        self.assertNotEqual(self.invoke("2147483647").returncode, 0)


if __name__ == "__main__":
    unittest.main()
