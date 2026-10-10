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
        client.sendall(
            (
                f"GET {path} HTTP/1.1\r\n"
                "Host: phase60-test\r\n"
                "Connection: close\r\n\r\n"
            ).encode("ascii")
        )
        client.shutdown(socket.SHUT_WR)
        raw = read_all(client)

    head, body = raw.split(b"\r\n\r\n", 1)
    if b" 200 " not in head.split(b"\r\n", 1)[0]:
        raise AssertionError(f"unexpected response: {raw!r}")
    return json.loads(body.decode("utf-8"))


def policy_text(score_slack):
    return (
        "latency_alpha_numerator=1\n"
        "latency_alpha_denominator=4\n"
        "latency_stale_after_ms=5000\n"
        "latency_weight=0\n"
        "load_weight=1\n"
        f"score_slack={score_slack}\n"
    )


def write_policy(path, score_slack):
    Path(path).write_text(policy_text(score_slack), encoding="utf-8")


def atomic_replace_policy(path, score_slack):
    target = Path(path)
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(policy_text(score_slack), encoding="utf-8")
    os.replace(temporary, target)


class AutoRoutingPolicyReloadTests(unittest.TestCase):
    def test_automatic_reload_debounce_atomic_replace_and_sighup_regression(self):
        backend_processes = []
        server = None
        slow_result = []
        slow_error = []

        def slow_request():
            try:
                slow_result.append(request_once("/slow?delay_ms=1800", timeout=6))
            except Exception as exc:  # noqa: BLE001
                slow_error.append(exc)

        with tempfile.TemporaryDirectory(prefix="phase60-") as directory:
            policy_path = Path(directory) / "routing-policy.conf"
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
                    [str(SERVER), "--routing-policy", str(policy_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    env=env,
                )
                wait_for_port(PROXY_PORT)

                slow = threading.Thread(target=slow_request)
                slow.start()
                time.sleep(0.20)

                warm = request_once("/phase60/warm")
                self.assertEqual(warm["backend"], "backend-b")

                # One atomic replacement must reload without SIGHUP.
                atomic_replace_policy(policy_path, 0)
                time.sleep(0.25)
                after_atomic = request_once("/phase60/after-atomic")
                self.assertEqual(after_atomic["backend"], "backend-b")

                # Several writes inside the debounce window must collapse to one generation.
                write_policy(policy_path, 20)
                write_policy(policy_path, 40)
                write_policy(policy_path, 80)
                time.sleep(0.25)
                request_once("/phase60/after-burst")

                # Invalid automatic candidate is rejected without replacing active state.
                policy_path.write_text(
                    "latency_weight=0\nload_weight=0\nscore_slack=0\n",
                    encoding="utf-8",
                )
                time.sleep(0.25)
                request_once("/phase60/after-invalid")

                # Repeated inode replacement must continue to be observed.
                atomic_replace_policy(policy_path, 10)
                time.sleep(0.25)
                request_once("/phase60/replacement-two")
                atomic_replace_policy(policy_path, 0)
                time.sleep(0.25)
                request_once("/phase60/replacement-three")

                # Manual SIGHUP remains a supported independent control path.
                server.send_signal(signal.SIGHUP)
                time.sleep(0.10)
                request_once("/phase60/sighup")

                slow.join(timeout=7)
                self.assertFalse(slow.is_alive())
                if slow_error:
                    raise slow_error[0]
                self.assertEqual(slow_result[0]["backend"], "backend-a")

                server.send_signal(signal.SIGTERM)
                stdout, stderr = server.communicate(timeout=5)
                self.assertEqual(
                    server.returncode,
                    0,
                    msg=f"stdout:\n{stdout}\nstderr:\n{stderr}",
                )

                self.assertIn("reload=SIGHUP,inotify", stdout)
                self.assertIn("ROUTING_POLICY_CHANGE_DETECTED source=inotify", stdout)
                self.assertIn(
                    "ROUTING_POLICY_RELOAD_OK old_generation=1 new_generation=2 source=inotify",
                    stdout,
                )
                # Burst is one publication: generation 2 -> 3, not multiple publications.
                self.assertIn(
                    "ROUTING_POLICY_RELOAD_OK old_generation=2 new_generation=3 source=inotify",
                    stdout,
                )
                self.assertEqual(
                    sum(
                        1
                        for line in stdout.splitlines()
                        if line.startswith("ROUTING_POLICY_RELOAD_OK")
                        and "source=inotify" in line
                    ),
                    4,
                )
                self.assertIn(
                    "ROUTING_POLICY_RELOAD_OK old_generation=3 new_generation=4 source=inotify",
                    stdout,
                )
                self.assertIn(
                    "ROUTING_POLICY_RELOAD_OK old_generation=4 new_generation=5 source=inotify",
                    stdout,
                )
                self.assertIn("ROUTING_POLICY_RELOAD_REJECTED generation=3 source=inotify", stderr)
                self.assertIn(
                    "ROUTING_POLICY_RELOAD_OK old_generation=5 new_generation=6 source=sighup",
                    stdout,
                )
                self.assertIn("policy_generation=1", stdout)
                self.assertIn("policy_generation=2", stdout)
                self.assertIn("policy_generation=3", stdout)

            finally:
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
