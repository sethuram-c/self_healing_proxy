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

BACKEND = (
    ROOT
    / "control-plane"
    / "mock_backend.py"
)

HOST = "127.0.0.1"
PROXY_PORT = 8081
BACKEND_PORT = 9001


def wait_for_port(port):
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            with socket.create_connection(
                (HOST, port),
                timeout=0.5,
            ):
                return
        except OSError:
            time.sleep(0.05)

    raise RuntimeError(
        f"port {port} did not become ready"
    )


def start_server(**overrides):
    env = os.environ.copy()
    env.update(
        {
            "MICROPROXY_CONNECT_TIMEOUT_MS": "1000",
            "MICROPROXY_IDLE_TIMEOUT_MS": "5000",
            "MICROPROXY_REQUEST_TIMEOUT_MS": "1000",
            "MICROPROXY_RESPONSE_TIMEOUT_MS": "1000",
        }
    )
    env.update(overrides)

    server = subprocess.Popen(
        [str(SERVER)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )

    wait_for_port(PROXY_PORT)
    return server


def stop_server(server):
    if server.poll() is None:
        server.send_signal(signal.SIGTERM)

    return server.communicate(timeout=5)


def wait_for_eof(sock, timeout=3):
    sock.settimeout(timeout)

    while True:
        data = sock.recv(4096)

        if not data:
            return


class TimeoutIntegrationTests(unittest.TestCase):

    def test_idle_connection_times_out(self):
        server = start_server(
            MICROPROXY_IDLE_TIMEOUT_MS="200",
        )
        client = None

        try:
            client = socket.create_connection(
                (HOST, PROXY_PORT),
                timeout=1,
            )

            wait_for_eof(client)
            client.close()
            client = None

            stdout, stderr = stop_server(server)

            self.assertEqual(server.returncode, 0)
            self.assertEqual(stderr, "")
            self.assertIn(
                "idle timeout after",
                stdout,
            )
            self.assertIn(
                "timeouts=1",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server.poll() is None:
                server.kill()
                server.communicate(timeout=5)

    def test_incomplete_request_times_out(self):
        server = start_server(
            MICROPROXY_IDLE_TIMEOUT_MS="2000",
            MICROPROXY_REQUEST_TIMEOUT_MS="200",
        )
        client = None

        try:
            client = socket.create_connection(
                (HOST, PROXY_PORT),
                timeout=1,
            )
            client.sendall(
                b"GET /incomplete HTTP/1.1\r\n"
                b"Host: backend-a\r\n"
            )

            wait_for_eof(client)
            client.close()
            client = None

            stdout, stderr = stop_server(server)

            self.assertEqual(server.returncode, 0)
            self.assertEqual(stderr, "")
            self.assertIn(
                "HTTP request timeout after",
                stdout,
            )
            self.assertIn(
                "timeouts=1",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server.poll() is None:
                server.kill()
                server.communicate(timeout=5)

    def test_backend_response_start_times_out(self):
        backend = subprocess.Popen(
            [
                "python3",
                str(BACKEND),
                "--name",
                "backend-a",
                "--port",
                str(BACKEND_PORT),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        server = None
        client = None

        try:
            wait_for_port(BACKEND_PORT)

            server = start_server(
                MICROPROXY_IDLE_TIMEOUT_MS="3000",
                MICROPROXY_RESPONSE_TIMEOUT_MS="200",
            )

            client = socket.create_connection(
                (HOST, PROXY_PORT),
                timeout=1,
            )
            client.sendall(
                b"GET /slow?delay_ms=800 HTTP/1.1\r\n"
                b"Host: backend-a\r\n"
                b"Connection: close\r\n"
                b"\r\n"
            )
            client.shutdown(socket.SHUT_WR)

            wait_for_eof(client)
            client.close()
            client = None

            stdout, stderr = stop_server(server)

            self.assertEqual(server.returncode, 0)
            self.assertEqual(stderr, "")
            self.assertIn(
                "backend response timeout after",
                stdout,
            )
            self.assertIn(
                "timeouts=1",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server is not None and server.poll() is None:
                server.kill()
                server.communicate(timeout=5)

            if backend.poll() is None:
                backend.terminate()

            try:
                backend.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                backend.kill()
                backend.communicate(timeout=5)

    def test_invalid_timeout_configuration_is_rejected(self):
        env = os.environ.copy()
        env["MICROPROXY_IDLE_TIMEOUT_MS"] = "0"

        result = subprocess.run(
            [str(SERVER)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
            timeout=3,
            check=False,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "Invalid MICROPROXY_IDLE_TIMEOUT_MS=0",
            result.stderr,
        )


if __name__ == "__main__":
    unittest.main()
