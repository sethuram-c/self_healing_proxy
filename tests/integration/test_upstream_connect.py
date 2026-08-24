import http.client
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


def wait_for_backend():
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            connection = (
                http.client.HTTPConnection(
                    HOST,
                    BACKEND_PORT,
                    timeout=1,
                )
            )

            connection.request(
                "GET",
                "/ready",
            )

            response = (
                connection.getresponse()
            )

            response.read()
            connection.close()

            if response.status == 200:
                return

        except OSError:
            pass

        time.sleep(0.05)

    raise RuntimeError(
        "backend did not become ready"
    )


def connect_to_proxy():
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection(
                (HOST, PROXY_PORT),
                timeout=1,
            )

            sock.settimeout(3)

            return sock

        except OSError:
            time.sleep(0.05)

    raise RuntimeError(
        "proxy did not become ready"
    )


class UpstreamConnectTests(
    unittest.TestCase
):

    def test_nonblocking_backend_connect(self):
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
            wait_for_backend()

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            client = connect_to_proxy()

            request = (
                b"GET /phase13 HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"\r\n"
            )

            client.sendall(request)

            #
            # Keep the downstream alive long
            # enough for the upstream connect
            # event to complete.
            #
            time.sleep(0.25)

            client.shutdown(
                socket.SHUT_WR
            )

            while client.recv(4096):
                pass

            client.close()
            client = None

            time.sleep(0.05)

            server.send_signal(
                signal.SIGTERM
            )

            stdout, stderr = (
                server.communicate(
                    timeout=5
                )
            )

            self.assertEqual(
                server.returncode,
                0,
            )

            self.assertEqual(
                stderr,
                "",
            )

            self.assertIn(
                "HTTP request 1 complete",
                stdout,
            )

            self.assertIn(
                "backend connected",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server is not None:
                if server.poll() is None:
                    server.kill()

                server.communicate(
                    timeout=5
                )

            if backend.poll() is None:
                backend.terminate()

            try:
                backend.communicate(
                    timeout=5
                )
            except subprocess.TimeoutExpired:
                backend.kill()
                backend.communicate(
                    timeout=5
                )


if __name__ == "__main__":
    unittest.main()
