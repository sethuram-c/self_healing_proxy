import json
import os
import signal
import socket
import subprocess
import threading
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

    raise RuntimeError(f"port {port} did not become ready")


def read_all(sock):
    data = bytearray()

    while True:
        chunk = sock.recv(4096)

        if not chunk:
            break

        data.extend(chunk)

    return bytes(data)


def request_raw(path, timeout=5):
    with socket.create_connection(
        (HOST, PROXY_PORT),
        timeout=2,
    ) as client:
        client.settimeout(timeout)

        request = (
            f"GET {path} HTTP/1.1\r\n"
            "Host: admission-test\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")

        client.sendall(request)
        client.shutdown(socket.SHUT_WR)
        return read_all(client)


def parse_json_response(raw):
    head, body = raw.split(b"\r\n\r\n", 1)
    status_line = head.split(b"\r\n", 1)[0]

    if b" 200 " not in status_line:
        raise AssertionError(
            f"unexpected status line: {status_line!r}"
        )

    return json.loads(body.decode("utf-8"))


class AdmissionControlIntegrationTests(unittest.TestCase):

    def test_full_backends_reject_then_recover_capacity(self):
        backend_processes = []
        server = None
        results = [None, None]
        errors = []

        def slow_request(slot):
            try:
                raw = request_raw(
                    "/slow?delay_ms=1500",
                    timeout=5,
                )
                results[slot] = parse_json_response(raw)
            except Exception as exc:  # noqa: BLE001
                errors.append(exc)

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

            env = os.environ.copy()
            env["MICROPROXY_BACKEND_MAX_CONNECTIONS"] = "1"
            env["MICROPROXY_IDLE_TIMEOUT_MS"] = "10000"
            env["MICROPROXY_RESPONSE_TIMEOUT_MS"] = "5000"

            server = subprocess.Popen(
                [str(SERVER)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
            )

            wait_for_port(PROXY_PORT)

            threads = [
                threading.Thread(
                    target=slow_request,
                    args=(0,),
                ),
                threading.Thread(
                    target=slow_request,
                    args=(1,),
                ),
            ]

            for thread in threads:
                thread.start()

            time.sleep(0.30)

            rejected = request_raw(
                "/should-be-rejected",
                timeout=2,
            )
            self.assertEqual(rejected, b"")

            for thread in threads:
                thread.join(timeout=6)
                self.assertFalse(thread.is_alive())

            if errors:
                raise errors[0]

            self.assertEqual(
                {result["backend"] for result in results},
                {"backend-a", "backend-b"},
            )

            recovered = parse_json_response(
                request_raw("/after-capacity-released")
            )
            self.assertEqual(
                recovered["backend"],
                "backend-a",
            )

            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(timeout=5)

            self.assertEqual(
                server.returncode,
                0,
                msg=(
                    f"stdout:\n{stdout}\n"
                    f"stderr:\n{stderr}"
                ),
            )
            self.assertEqual(stderr, "")
            self.assertIn(
                "admission rejected: all backends at capacity",
                stdout,
            )
            self.assertIn(
                "admission_rejections=1",
                stdout,
            )
            self.assertIn(
                "backend-a 127.0.0.1:9001 "
                "(index=0 active=1/1)",
                stdout,
            )
            self.assertIn(
                "backend-b 127.0.0.1:9002 "
                "(index=1 active=1/1)",
                stdout,
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
