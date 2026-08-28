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


def read_all(sock):
    data = bytearray()

    while True:
        chunk = sock.recv(4096)

        if not chunk:
            break

        data.extend(chunk)

    return bytes(data)


class BidirectionalProxyTests(unittest.TestCase):

    def test_request_and_response_are_forwarded(self):
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

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            wait_for_port(PROXY_PORT)

            client = socket.create_connection(
                (HOST, PROXY_PORT),
                timeout=2,
            )
            client.settimeout(3)

            first_half = (
                b"GET /phase14?status=201 HTTP/1.1\r\n"
                b"Host: backend-a\r\n"
            )

            second_half = (
                b"Connection: close\r\n"
                b"\r\n"
            )

            client.sendall(first_half)
            time.sleep(0.05)
            client.sendall(second_half)
            client.shutdown(socket.SHUT_WR)

            raw_response = read_all(client)

            client.close()
            client = None

            self.assertIn(
                b" 201 ",
                raw_response.split(b"\r\n", 1)[0],
            )

            header_block, body = raw_response.split(
                b"\r\n\r\n",
                1,
            )

            self.assertIn(
                b"Content-Type: application/json",
                header_block,
            )

            payload = json.loads(
                body.decode("utf-8")
            )

            self.assertEqual(
                payload["backend"],
                "backend-a",
            )
            self.assertEqual(
                payload["path"],
                "/phase14",
            )
            self.assertEqual(
                payload["status"],
                201,
            )

            time.sleep(0.05)

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(
                timeout=5
            )

            self.assertEqual(server.returncode, 0)
            self.assertEqual(stderr, "")
            self.assertIn(
                "HTTP request 1 complete",
                stdout,
            )
            self.assertIn(
                "backend-a connected",
                stdout,
            )
            self.assertIn(
                "backend reached EOF",
                stdout,
            )
            self.assertIn(
                "bytes to backend",
                stdout,
            )
            self.assertIn(
                "bytes to client",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server is not None:
                if server.poll() is None:
                    server.kill()

                server.communicate(timeout=5)

            if backend.poll() is None:
                backend.terminate()

            try:
                backend.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                backend.kill()
                backend.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
