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
BACKENDS = [
    ("backend-a", 9001),
    ("backend-b", 9002),
]


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


def request_once(sequence):
    with socket.create_connection(
        (HOST, PROXY_PORT),
        timeout=2,
    ) as client:
        client.settimeout(4)

        request = (
            f"GET /phase17/{sequence} HTTP/1.1\r\n"
            "Host: round-robin-test\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")

        client.sendall(request)
        client.shutdown(socket.SHUT_WR)

        raw = read_all(client)

    head, body = raw.split(b"\r\n\r\n", 1)
    status_line = head.split(b"\r\n", 1)[0]

    if b" 200 " not in status_line:
        raise AssertionError(
            f"unexpected status line: {status_line!r}"
        )

    return json.loads(body.decode("utf-8"))


class RoundRobinIntegrationTests(unittest.TestCase):

    def test_requests_rotate_between_two_backends(self):
        backend_processes = []
        server = None

        try:
            for name, port in BACKENDS:
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

                backend_processes.append(process)
                wait_for_port(port)

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            wait_for_port(PROXY_PORT)

            observed = []

            for sequence in range(6):
                payload = request_once(sequence)
                observed.append(payload["backend"])
                self.assertEqual(
                    payload["path"],
                    f"/phase17/{sequence}",
                )

            self.assertEqual(
                observed,
                [
                    "backend-a",
                    "backend-b",
                    "backend-a",
                    "backend-b",
                    "backend-a",
                    "backend-b",
                ],
            )

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(
                timeout=5
            )

            self.assertEqual(
                server.returncode,
                0,
                msg=(
                    f"stdout:\n{stdout}\n"
                    f"stderr:\n{stderr}"
                ),
            )
            self.assertEqual(stderr, "")
            self.assertEqual(
                stdout.count("selected backend backend-a"),
                3,
            )
            self.assertEqual(
                stdout.count("selected backend backend-b"),
                3,
            )

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
    unittest.main()
