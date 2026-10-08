#!/usr/bin/env python3
import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVER = Path(os.environ.get("EPOLL_SERVER_BIN", ROOT / "build" / "epoll_server"))
BACKEND = ROOT / "control-plane" / "mock_backend.py"
HOST = "127.0.0.1"
PROXY_PORT = 8081
BACKENDS = [("backend-a", 9001), ("backend-b", 9002)]


def wait_for_port(port):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((HOST, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"port {port} did not become ready")


def read_all(sock):
    data = bytearray()
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return bytes(data)
        data.extend(chunk)


def request_once(path, timeout=5):
    with socket.create_connection((HOST, PROXY_PORT), timeout=2) as client:
        client.settimeout(timeout)
        request = (
            f"GET {path} HTTP/1.1\r\n"
            "Host: phase59-test\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")
        client.sendall(request)
        client.shutdown(socket.SHUT_WR)
        raw = read_all(client)

    head, body = raw.split(b"\r\n\r\n", 1)
    if b" 200 " not in head.split(b"\r\n", 1)[0]:
        raise AssertionError(f"unexpected response: {raw!r}")
    return json.loads(body.decode("utf-8"))


def write_policy(path, score_slack):
    Path(path).write_text(
        "latency_alpha_numerator=1\n"
        "latency_alpha_denominator=4\n"
        "latency_stale_after_ms=5000\n"
        "latency_weight=0\n"
        "load_weight=1\n"
        f"score_slack={score_slack}\n",
        encoding="utf-8",
    )


class LiveRoutingPolicyReloadTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        p = subprocess.run([SERVER, "--help"], text=True, capture_output=True)
        self.assertEqual(p.returncode, 0)
        self.assertIn("--routing-policy PATH", p.stderr)

        p = subprocess.run([SERVER, "--unknown"], text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_invalid_startup_policy_fails_closed(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write("load_weight=0\nlatency_weight=0\n")
            path = f.name
        try:
            p = subprocess.run(
                [SERVER, "--routing-policy", path],
                text=True,
                capture_output=True,
            )
            self.assertNotEqual(p.returncode, 0)
            self.assertIn("Invalid routing policy file", p.stderr)
        finally:
            os.unlink(path)

    def test_sighup_reloads_live_policy_without_dropping_connection(self):
        backend_processes = []
        server = None
        slow_result = []
        slow_error = []

        def slow_request():
            try:
                slow_result.append(request_once("/slow?delay_ms=2200", timeout=6))
            except Exception as exc:  # noqa: BLE001
                slow_error.append(exc)

        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            policy_path = f.name
        write_policy(policy_path, 1000)

        try:
            for name, port in BACKENDS:
                process = subprocess.Popen(
                    ["python3", str(BACKEND), "--name", name, "--port", str(port)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                backend_processes.append(process)
                wait_for_port(port)

            env = os.environ.copy()
            env["MICROPROXY_BACKEND_MAX_CONNECTIONS"] = "4"
            env["MICROPROXY_RESPONSE_TIMEOUT_MS"] = "5000"
            server = subprocess.Popen(
                [str(SERVER), "--routing-policy", policy_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
            )
            wait_for_port(PROXY_PORT)

            slow = threading.Thread(target=slow_request)
            slow.start()
            time.sleep(0.25)

            # With wide slack, round-robin fairness still admits the second peer.
            warm = request_once("/phase59/warm")
            self.assertEqual(warm["backend"], "backend-b")

            # Cursor now points at backend-a, which remains loaded by the slow request.
            # Tightening slack to zero must make the next decision prefer idle backend-b.
            write_policy(policy_path, 0)
            server.send_signal(signal.SIGHUP)
            time.sleep(0.10)
            after_reload = request_once("/phase59/after-reload")
            self.assertEqual(after_reload["backend"], "backend-b")

            # Invalid candidate must be rejected, keeping generation 2 active.
            Path(policy_path).write_text(
                "latency_weight=0\nload_weight=0\nscore_slack=0\n",
                encoding="utf-8",
            )
            server.send_signal(signal.SIGHUP)
            time.sleep(0.10)
            still_active = request_once("/phase59/after-invalid")
            self.assertIn(still_active["backend"], {"backend-a", "backend-b"})

            # A second valid reload publishes generation 3.
            write_policy(policy_path, 80)
            server.send_signal(signal.SIGHUP)
            time.sleep(0.10)
            request_once("/phase59/generation-three")

            slow.join(timeout=7)
            self.assertFalse(slow.is_alive())
            if slow_error:
                raise slow_error[0]
            self.assertEqual(slow_result[0]["backend"], "backend-a")

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(timeout=5)
            self.assertEqual(server.returncode, 0, msg=f"stdout:\n{stdout}\nstderr:\n{stderr}")
            self.assertIn("Routing policy: path=", stdout)
            self.assertIn("ROUTING_POLICY_RELOAD_OK old_generation=1 new_generation=2", stdout)
            self.assertIn("ROUTING_POLICY_RELOAD_OK old_generation=2 new_generation=3", stdout)
            self.assertIn("ROUTING_POLICY_RELOAD_REJECTED generation=2", stderr)
            self.assertIn("policy_generation=1", stdout)
            self.assertIn("policy_generation=2", stdout)
            self.assertIn("policy_generation=3", stdout)

        finally:
            try:
                os.unlink(policy_path)
            except FileNotFoundError:
                pass
            if server is not None:
                if server.poll() is None:
                    server.kill()
                server.communicate(timeout=5)
            for process in backend_processes:
                if process.poll() is None:
                    process.terminate()
            for process in backend_processes:
                try:
                    process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
