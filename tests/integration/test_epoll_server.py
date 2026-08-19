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
            time.sleep(0.05)

    raise RuntimeError(
        "epoll server did not become ready"
    )


class EpollServerIntegrationTests(
    unittest.TestCase
):

    def test_idle_client_does_not_block_active_clients(
        self
    ):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        clients = []

        try:
            idle = connect_with_retry()
            clients.append(idle)

            active_one = connect_with_retry()
            clients.append(active_one)

            active_two = connect_with_retry()
            clients.append(active_two)

            active_one.sendall(
                b"active-one"
            )

            active_one.shutdown(
                socket.SHUT_WR
            )

            active_two.sendall(
                b"active-two"
            )

            active_two.shutdown(
                socket.SHUT_WR
            )

            time.sleep(0.1)

            idle.sendall(
                b"idle-finally-sends"
            )

            idle.shutdown(
                socket.SHUT_WR
            )

            for client in clients:
                while client.recv(1024):
                    pass

                client.close()

            clients.clear()

            server.send_signal(
                signal.SIGTERM
            )

            stdout, stderr = (
                server.communicate(
                    timeout=3
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

            self.assertGreaterEqual(
                stdout.count("Accepted "),
                3,
            )

            self.assertIn(
                "read 10 bytes",
                stdout,
            )

            self.assertIn(
                "read 18 bytes",
                stdout,
            )

            self.assertGreaterEqual(
                stdout.count(
                    "reached EOF"
                ),
                3,
            )

            self.assertIn(
                "epoll server shut down cleanly",
                stdout,
            )

        finally:
            for client in clients:
                client.close()

            if server.poll() is None:
                server.kill()
                server.communicate()


if __name__ == "__main__":
    unittest.main()
