import json
import os
import signal
import socket
import subprocess
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = Path(
    os.environ.get(
        "EPOLL_SERVER_BIN",
        ROOT / "build" / "epoll_server",
    )
)
BACKEND = ROOT / "control-plane" / "mock_backend.py"
HOST = "127.0.0.1"
PROXY_PORT = 8081


def wait_for_port(port):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((HOST, port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"port {port} did not become ready")


def request(path):
    with socket.create_connection((HOST, PROXY_PORT), timeout=2) as client:
        client.settimeout(5)
        payload = (
            f"GET {path} HTTP/1.1\r\n"
            "Host: retry-test\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")
        client.sendall(payload)
        client.shutdown(socket.SHUT_WR)

        data = bytearray()
        while True:
            try:
                chunk = client.recv(4096)
            except ConnectionResetError:
                break
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)


def start_backend(name, port):
    process = subprocess.Popen(
        [
            "python3",
            str(BACKEND),
            "--name",
            name,
            "--port",
            str(port),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    wait_for_port(port)
    return process


def stop_process(process):
    if process is None:
        return "", ""
    if process.poll() is None:
        process.terminate()
    try:
        return process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        return process.communicate(timeout=5)


class RetrySafetyIntegrationTests(unittest.TestCase):

    def test_connect_failure_retries_alternate_backend_before_send(self):
        backend_b = None
        server = None
        try:
            backend_b = start_backend("backend-b", 9002)

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            wait_for_port(PROXY_PORT)

            raw = request("/phase19/failover")
            head, body = raw.split(b"\r\n\r\n", 1)
            self.assertIn(b" 200 ", head.split(b"\r\n", 1)[0])
            payload = json.loads(body.decode("utf-8"))
            self.assertEqual(payload["backend"], "backend-b")

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(timeout=5)

            self.assertEqual(server.returncode, 0, msg=f"stdout:\n{stdout}\nstderr:\n{stderr}")
            self.assertEqual(stderr, "")
            self.assertIn("selected backend backend-a", stdout)
            self.assertIn("retrying backend after backend connect failure", stdout)
            self.assertIn("selected backend backend-b", stdout)
            self.assertIn("backend_retries=1", stdout)
        finally:
            if server is not None and server.poll() is None:
                server.kill()
                server.communicate(timeout=5)
            stop_process(backend_b)

    def test_request_is_not_replayed_after_any_upstream_bytes_were_sent(self):
        backend_a = None
        backend_b = None
        server = None
        try:
            backend_a = start_backend("backend-a", 9001)
            backend_b = start_backend("backend-b", 9002)

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            wait_for_port(PROXY_PORT)

            raw = request("/drop?delay_ms=200")
            self.assertEqual(raw, b"")

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(timeout=5)
            b_stdout, b_stderr = stop_process(backend_b)
            backend_b = None

            self.assertEqual(server.returncode, 0, msg=f"stdout:\n{stdout}\nstderr:\n{stderr}")
            self.assertEqual(stderr, "")
            self.assertEqual(b_stderr, "")
            self.assertIn("backend retry suppressed after", stdout)
            self.assertIn("request bytes were forwarded", stdout)
            self.assertNotIn("/drop", b_stdout)
            self.assertIn("retry_suppressions=1", stdout)
        finally:
            if server is not None and server.poll() is None:
                server.kill()
                server.communicate(timeout=5)
            stop_process(backend_a)
            stop_process(backend_b)


if __name__ == "__main__":
    unittest.main()
