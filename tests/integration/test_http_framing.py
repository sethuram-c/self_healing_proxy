import os
import signal
import socket
import subprocess
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

BINARY = Path(
    os.environ.get(
        "EPOLL_SERVER_BIN",
        ROOT / "build" / "epoll_server",
    )
)

HOST = "127.0.0.1"
PORT = 8081


def connect_with_retry():
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection(
                (HOST, PORT),
                timeout=1,
            )

            sock.settimeout(3)

            return sock

        except OSError:
            time.sleep(0.02)

    raise RuntimeError(
        "epoll server did not become ready"
    )


class HTTPFramingIntegrationTests(
    unittest.TestCase
):

    def test_fragmented_and_coalesced_requests(
        self
    ):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        client = None

        try:
            client = connect_with_retry()

            first = (
                b"POST /fragmented HTTP/1.1\r\n"
                b"Host: example\r\n"
                b"Content-Length: 5\r\n"
                b"\r\n"
                b"hello"
            )

            #
            # Deliver request #1 literally
            # one byte at a time.
            #
            for byte in first:
                client.sendall(
                    bytes([byte])
                )

            second = (
                b"GET /second HTTP/1.1\r\n"
                b"Host: example\r\n"
                b"\r\n"
            )

            third = (
                b"GET /third HTTP/1.1\r\n"
                b"Host: example\r\n"
                b"\r\n"
            )

            #
            # Requests #2 and #3 are sent
            # together to test coalescing.
            #
            client.sendall(
                second + third
            )

            client.shutdown(
                socket.SHUT_WR
            )

            while client.recv(4096):
                pass

            client.close()
            client = None

            time.sleep(0.1)

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
                "body=5",
                stdout,
            )

            self.assertIn(
                "HTTP request 2 complete",
                stdout,
            )

            self.assertIn(
                "HTTP request 3 complete",
                stdout,
            )

            self.assertIn(
                "and 3 HTTP requests",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server.poll() is None:
                server.kill()

                server.communicate(
                    timeout=5
                )


if __name__ == "__main__":
    unittest.main()
