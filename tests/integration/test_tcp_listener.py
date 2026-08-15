import signal
import socket
import subprocess
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build" / "tcp_listener"

HOST = "127.0.0.1"
PORT = 8080


def connect_with_retry():
    deadline = time.monotonic() + 3

    while time.monotonic() < deadline:
        try:
            return socket.create_connection(
                (HOST, PORT),
                timeout=0.5,
            )
        except OSError:
            time.sleep(0.05)

    raise RuntimeError(
        "TCP server did not become ready"
    )


class TCPListenerIntegrationTests(unittest.TestCase):

    def test_multiple_clients_and_graceful_shutdown(self):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        try:
            messages = [
                b"client one\n",
                b"client two\n",
                b"client three\n",
            ]

            for message in messages:
                with connect_with_retry() as client:
                    client.sendall(message)

                    response = client.recv(1024)

                    self.assertIn(
                        b"hello from phase04 tcp server",
                        response,
                    )

            server.send_signal(signal.SIGTERM)

            stdout, stderr = server.communicate(
                timeout=3
            )

            self.assertEqual(
                server.returncode,
                0,
            )

            self.assertEqual(stderr, "")

            self.assertGreaterEqual(
                stdout.count("Accepted "),
                3,
            )

            self.assertIn(
                "TCP server shut down cleanly",
                stdout,
            )

        finally:
            if server.poll() is None:
                server.kill()
                server.wait()


if __name__ == "__main__":
    unittest.main()
