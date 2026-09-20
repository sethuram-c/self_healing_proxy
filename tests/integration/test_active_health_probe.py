#!/usr/bin/env python3
import os
import socket
import subprocess
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(os.environ.get("ACTIVE_HEALTH_PROBE_LAB_BIN", ROOT / "build" / "active_health_probe_lab"))


class AcceptServer:
    def __init__(self, port=0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.port = self.sock.getsockname()[1]
        self.sock.listen(16)
        self.sock.settimeout(0.1)
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def _run(self):
        while not self.stop_event.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            conn.close()

    def close(self):
        self.stop_event.set()
        try:
            self.sock.close()
        except OSError:
            pass
        self.thread.join(timeout=1)


class ActiveHealthProbeTests(unittest.TestCase):
    def run_lab(self, *args, timeout=5):
        return subprocess.run(
            [str(LAB), *map(str, args)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=timeout,
        )

    def test_help_and_invalid_cli(self):
        result = self.run_lab("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--timeout-ms", result.stdout)

        result = self.run_lab("--host", "127.0.0.1", "--port", "0")
        self.assertNotEqual(result.returncode, 0)

    def test_healthy_backend_probes_succeed(self):
        server = AcceptServer().start()
        try:
            result = self.run_lab(
                "--host", "127.0.0.1",
                "--port", server.port,
                "--cycles", 3,
                "--interval-ms", 10,
                "--timeout-ms", 100,
            )
        finally:
            server.close()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PROBE_SUCCESSES=3", result.stdout)
        self.assertIn("PROBE_FAILURES=0", result.stdout)
        self.assertIn("FINAL_HEALTH=HEALTHY", result.stdout)
        self.assertNotIn("result=TIMEOUT", result.stdout)

    def test_refused_backend_becomes_unhealthy(self):
        holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        holder.bind(("127.0.0.1", 0))
        port = holder.getsockname()[1]
        holder.close()

        result = self.run_lab(
            "--host", "127.0.0.1",
            "--port", port,
            "--cycles", 2,
            "--interval-ms", 10,
            "--timeout-ms", 100,
            "--failure-threshold", 2,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("result=REFUSED", result.stdout)
        self.assertIn("PROBE_FAILURES=2", result.stdout)
        self.assertIn("FINAL_HEALTH=UNHEALTHY", result.stdout)

    def test_backend_recovers_after_cooldown_and_two_successes(self):
        reserve = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
        reserve.close()

        proc = subprocess.Popen(
            [
                str(LAB), "--host", "127.0.0.1", "--port", str(port),
                "--cycles", "5", "--interval-ms", "60", "--timeout-ms", "50",
                "--failure-threshold", "2", "--recovery-successes", "2",
                "--cooldown-ms", "30",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        server = None
        try:
            time.sleep(0.085)
            server = AcceptServer(port).start()
            stdout, stderr = proc.communicate(timeout=5)
        finally:
            if server is not None:
                server.close()
            if proc.poll() is None:
                proc.kill()
                proc.communicate()

        self.assertEqual(proc.returncode, 0, stderr)
        lines = [line for line in stdout.splitlines() if line.startswith("PROBE ")]
        self.assertGreaterEqual(len(lines), 5, stdout)
        self.assertIn("result=REFUSED", lines[0])
        self.assertIn("result=REFUSED", lines[1])
        self.assertIn("health=UNHEALTHY", lines[1])
        self.assertTrue(any("health=RECOVERING" in line for line in lines[2:]), stdout)
        self.assertIn("FINAL_HEALTH=HEALTHY", stdout)
        self.assertIn("PROBE_SUCCESSES=3", stdout)
        self.assertIn("PROBE_FAILURES=2", stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
