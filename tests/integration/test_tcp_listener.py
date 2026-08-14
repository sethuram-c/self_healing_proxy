import socket
import subprocess
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build" / "tcp_listener"

HOST = "127.0.0.1"
PORT = 8080


class TCPListenerIntegrationTests(unittest.TestCase):

    def test_listener_accepts_connection(self):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        try:
            deadline = time.monotonic() + 3

            while True:
                try:
                    client = socket.create_connection(
                        (HOST, PORT),
                        timeout=0.25,
                    )
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        self.fail(
                            "TCP listener did not become ready"
                        )

                    time.sleep(0.05)

            with client:
                client.sendall(b"hello proxy\n")

                response = client.recv(1024)

            stdout, stderr = server.communicate(timeout=3)

            self.assertEqual(server.returncode, 0)
            self.assertEqual(stderr, "")

            self.assertIn(
                b"hello from phase03 tcp listener",
                response,
            )

            self.assertIn(
                "Accepted client",
                stdout,
            )

            self.assertIn(
                "Received 12 bytes",
                stdout,
            )

        finally:
            if server.poll() is None:
                server.kill()

            server.wait()


if __name__ == "__main__":
    unittest.main()
