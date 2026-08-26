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

BODY_BYTES = 8 * 1024 * 1024


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


def read_http_response(sock):
    data = bytearray()

    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            raise AssertionError(
                "connection closed before headers"
            )

        data.extend(chunk)

    header_block, body = bytes(data).split(
        b"\r\n\r\n",
        1,
    )

    content_length = None

    for line in header_block.split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")

        if name.lower() == b"content-length":
            content_length = int(
                value.strip()
            )
            break

    if content_length is None:
        raise AssertionError(
            "missing Content-Length"
        )

    body_data = bytearray(body)

    while len(body_data) < content_length:
        chunk = sock.recv(16384)

        if not chunk:
            break

        body_data.extend(chunk)

        #
        # Deliberately consume slowly enough
        # that the proxy's downstream send
        # path experiences pressure.
        #
        time.sleep(0.0005)

    return (
        header_block,
        bytes(body_data),
        content_length,
    )


class BackpressureIntegrationTests(
    unittest.TestCase
):

    def test_slow_client_pauses_and_resumes_backend_reads(
        self
    ):
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

            client = socket.socket(
                socket.AF_INET,
                socket.SOCK_STREAM,
            )

            client.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_RCVBUF,
                4096,
            )

            client.settimeout(15)
            client.connect(
                (HOST, PROXY_PORT)
            )

            request = (
                b"GET /bytes?body_bytes="
                + str(BODY_BYTES).encode("ascii")
                + b" HTTP/1.1\r\n"
                b"Host: backend-a\r\n"
                b"Connection: close\r\n"
                b"\r\n"
            )

            client.sendall(request)
            client.shutdown(
                socket.SHUT_WR
            )

            #
            # Do not read immediately. The backend
            # produces a large body, so the proxy
            # must eventually stop reading upstream
            # rather than overflowing its 4 KiB
            # userspace response buffer.
            #
            time.sleep(0.75)

            (
                header_block,
                body,
                content_length,
            ) = read_http_response(client)

            self.assertIn(
                b" 200 ",
                header_block.split(
                    b"\r\n",
                    1,
                )[0],
            )

            self.assertEqual(
                content_length,
                BODY_BYTES,
            )

            self.assertEqual(
                len(body),
                BODY_BYTES,
            )

            self.assertEqual(
                body[:32],
                b"x" * 32,
            )

            self.assertEqual(
                body[-32:],
                b"x" * 32,
            )

            client.close()
            client = None

            time.sleep(0.1)

            server.send_signal(
                signal.SIGTERM
            )

            stdout, stderr = (
                server.communicate(
                    timeout=10
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
                "backend read paused",
                stdout,
            )

            self.assertIn(
                "backend read resumed",
                stdout,
            )

            self.assertIn(
                "backend pauses=",
                stdout,
            )

            self.assertIn(
                "shut down cleanly",
                stdout,
            )

        finally:
            if client is not None:
                client.close()

            if server is not None:
                if server.poll() is None:
                    server.kill()

                server.communicate(
                    timeout=10
                )

            if backend.poll() is None:
                backend.terminate()

            try:
                backend.communicate(
                    timeout=10
                )
            except subprocess.TimeoutExpired:
                backend.kill()
                backend.communicate(
                    timeout=10
                )


if __name__ == "__main__":
    unittest.main()
