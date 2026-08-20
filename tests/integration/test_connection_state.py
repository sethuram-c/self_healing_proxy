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
    deadline = time.monotonic() + 3

    while time.monotonic() < deadline:
        try:
            return socket.create_connection(
                (HOST, PORT),
                timeout=1,
            )
        except OSError:
            time.sleep(0.02)

    raise RuntimeError(
        "epoll server did not become ready"
    )


class ConnectionStateTests(unittest.TestCase):

    def test_many_connection_lifecycles(self):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        connection_count = 32

        try:
            for index in range(
                connection_count
            ):
                payload = (
                    f"connection-{index}"
                ).encode()

                with connect_with_retry() as client:
                    client.sendall(payload)

                    client.shutdown(
                        socket.SHUT_WR
                    )

                    while client.recv(1024):
                        pass

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

            self.assertEqual(
                stdout.count(
                    "opened on FD"
                ),
                connection_count,
            )

            self.assertEqual(
                stdout.count(
                    "closed after"
                ),
                connection_count,
            )

            self.assertIn(
                "Connection 1 ",
                stdout,
            )

            self.assertIn(
                f"Connection "
                f"{connection_count} ",
                stdout,
            )

            self.assertIn(
                "shut down cleanly",
                stdout,
            )

        finally:
            if server.poll() is None:
                server.kill()
                server.communicate()


if __name__ == "__main__":
    unittest.main()
