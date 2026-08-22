import http.client
import json
import subprocess
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

BACKEND = (
    ROOT
    / "control-plane"
    / "mock_backend.py"
)


def request(
    port,
    path,
):
    connection = http.client.HTTPConnection(
        "127.0.0.1",
        port,
        timeout=3,
    )

    connection.request(
        "GET",
        path,
    )

    response = connection.getresponse()

    payload = response.read()

    status = response.status

    connection.close()

    return (
        status,
        json.loads(
            payload.decode("utf-8")
        ),
    )


def wait_until_ready(
    port,
):
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            status, _ = request(
                port,
                "/ready",
            )

            if status == 200:
                return

        except OSError:
            pass

        time.sleep(0.05)

    raise RuntimeError(
        f"backend on port {port} "
        f"did not become ready"
    )


class MockBackendTests(
    unittest.TestCase
):

    def setUp(self):
        self.processes = []

        self.start_backend(
            "backend-a",
            9001,
        )

        self.start_backend(
            "backend-b",
            9002,
        )

    def start_backend(
        self,
        name,
        port,
    ):
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

        self.processes.append(
            process
        )

        wait_until_ready(
            port
        )

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()

        for process in self.processes:
            try:
                process.communicate(
                    timeout=3
                )
            except subprocess.TimeoutExpired:
                process.kill()

                process.communicate(
                    timeout=3
                )

    def test_backend_identity(self):
        status_a, body_a = request(
            9001,
            "/hello",
        )

        status_b, body_b = request(
            9002,
            "/hello",
        )

        self.assertEqual(
            status_a,
            200,
        )

        self.assertEqual(
            status_b,
            200,
        )

        self.assertEqual(
            body_a["backend"],
            "backend-a",
        )

        self.assertEqual(
            body_b["backend"],
            "backend-b",
        )

        self.assertEqual(
            body_a["path"],
            "/hello",
        )

    def test_custom_status(self):
        status, body = request(
            9001,
            "/failure?status=503",
        )

        self.assertEqual(
            status,
            503,
        )

        self.assertEqual(
            body["status"],
            503,
        )

    def test_controlled_delay(self):
        start = time.monotonic()

        status, body = request(
            9002,
            "/slow?delay_ms=200",
        )

        elapsed = (
            time.monotonic()
            - start
        )

        self.assertEqual(
            status,
            200,
        )

        self.assertEqual(
            body["delay_ms"],
            200,
        )

        self.assertGreaterEqual(
            elapsed,
            0.18,
        )

        self.assertLess(
            elapsed,
            1.5,
        )


if __name__ == "__main__":
    unittest.main()
