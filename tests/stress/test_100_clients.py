import os
import signal
import socket
import subprocess
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
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

CLIENT_COUNT = 100
PAYLOAD_SIZE = 2048

EXPECTED_TOTAL_BYTES = (
    CLIENT_COUNT * PAYLOAD_SIZE
)


def connect_with_retry():
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection(
                (HOST, PORT),
                timeout=1,
            )

            sock.settimeout(5)

            return sock

        except OSError:
            time.sleep(0.02)

    raise RuntimeError(
        "epoll server did not become ready"
    )


def process_fd_count(pid):
    fd_path = Path(
        f"/proc/{pid}/fd"
    )

    return len(list(fd_path.iterdir()))


def wait_for_fd_count(
    pid,
    minimum,
):
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        try:
            count = process_fd_count(pid)
        except FileNotFoundError:
            raise RuntimeError(
                "server exited unexpectedly"
            )

        if count >= minimum:
            return count

        time.sleep(0.02)

    raise AssertionError(
        f"server never reached "
        f"{minimum} open FDs"
    )


def make_payload(index):
    value = index % 256

    return bytes(
        [value]
    ) * PAYLOAD_SIZE


def send_payload(
    client,
    index,
):
    client.sendall(
        make_payload(index)
    )

    client.shutdown(
        socket.SHUT_WR
    )


def wait_for_eof(client):
    while True:
        data = client.recv(4096)

        if not data:
            return


class StageOneStressTests(
    unittest.TestCase
):

    def test_100_simultaneous_clients(self):
        server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        clients = []

        try:
            #
            # Important:
            # establish every connection first.
            #
            for _ in range(CLIENT_COUNT):
                clients.append(
                    connect_with_retry()
                )

            #
            # Server has:
            #
            #   0 stdin
            #   1 stdout
            #   2 stderr
            #   listener
            #   epoll FD
            #   100 clients
            #
            # Therefore ~105 FDs is expected.
            #
            fd_count = wait_for_fd_count(
                server.pid,
                CLIENT_COUNT + 5,
            )

            self.assertGreaterEqual(
                fd_count,
                CLIENT_COUNT + 5,
            )

            #
            # Trigger readiness across many
            # sockets at roughly the same time.
            #
            with ThreadPoolExecutor(
                max_workers=20
            ) as executor:

                futures = [
                    executor.submit(
                        send_payload,
                        client,
                        index,
                    )
                    for index, client
                    in enumerate(clients)
                ]

                for future in futures:
                    future.result(
                        timeout=5
                    )

            #
            # Server sends no application
            # response yet. We wait only for
            # connection EOF.
            #
            for client in clients:
                wait_for_eof(client)
                client.close()

            clients.clear()

            #
            # Allow event loop to complete
            # teardown before SIGTERM.
            #
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

            self.assertEqual(
                stdout.count(
                    "opened on FD"
                ),
                CLIENT_COUNT,
            )

            self.assertEqual(
                stdout.count(
                    "closed after"
                ),
                CLIENT_COUNT,
            )

            self.assertIn(
                "total_connections=100",
                stdout,
            )

            self.assertIn(
                "peak_connections=100",
                stdout,
            )

            self.assertIn(
                "active_connections=0",
                stdout,
            )

            self.assertIn(
                (
                    "total_client_bytes="
                    f"{EXPECTED_TOTAL_BYTES}"
                ),
                stdout,
            )

            self.assertIn(
                "shut down cleanly",
                stdout,
            )

        finally:
            for client in clients:
                try:
                    client.close()
                except OSError:
                    pass

            if server.poll() is None:
                server.kill()

                server.communicate(
                    timeout=5
                )


if __name__ == "__main__":
    unittest.main()
