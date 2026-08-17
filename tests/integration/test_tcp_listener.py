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
        "TCP_LISTENER_BIN",
        ROOT / "build" / "tcp_listener",
    )
)

HOST = "127.0.0.1"
PORT = 8080


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
        "TCP server did not become ready"
    )


def receive_until_eof(sock):
    result = bytearray()

    while True:
        chunk = sock.recv(4096)

        if not chunk:
            break

        result.extend(chunk)

    return bytes(result)


class TCPListenerIntegrationTests(unittest.TestCase):

    def setUp(self):
        self.server = subprocess.Popen(
            [str(BINARY)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def stop_server(self):
        if self.server.poll() is None:
            self.server.send_signal(signal.SIGTERM)

        try:
            stdout, stderr = self.server.communicate(
                timeout=3
            )
        except subprocess.TimeoutExpired:
            self.server.kill()

            stdout, stderr = self.server.communicate(
                timeout=3
            )

        return stdout, stderr

    def tearDown(self):
        if self.server.stdout is not None:
            self.server.stdout.close()

        if self.server.stderr is not None:
            self.server.stderr.close()

    def test_fragmented_stream(self):
        payload = b"fragmented-stream-test"

        with connect_with_retry() as client:
            for byte in payload:
                client.sendall(bytes([byte]))
                time.sleep(0.001)

            client.shutdown(socket.SHUT_WR)

            response = receive_until_eof(client)

        self.assertEqual(
            response,
            payload,
        )

        stdout, stderr = self.stop_server()

        self.assertEqual(
            self.server.returncode,
            0,
        )

        self.assertEqual(
            stderr,
            "",
        )

        self.assertIn(
            "Client reached EOF",
            stdout,
        )

        self.assertIn(
            "TCP server shut down cleanly",
            stdout,
        )

    def test_large_stream(self):
        payload = bytes(
            index % 256
            for index in range(32768)
        )

        with connect_with_retry() as client:
            offset = 0

            while offset < len(payload):
                end = min(
                    offset + 37,
                    len(payload),
                )

                client.sendall(
                    payload[offset:end]
                )

                offset = end

            client.shutdown(socket.SHUT_WR)

            response = receive_until_eof(client)

        self.assertEqual(
            len(response),
            len(payload),
        )

        self.assertEqual(
            response,
            payload,
        )

        stdout, stderr = self.stop_server()

        self.assertEqual(
            self.server.returncode,
            0,
        )

        self.assertEqual(
            stderr,
            "",
        )

        self.assertIn(
            "Client reached EOF",
            stdout,
        )


if __name__ == "__main__":
    unittest.main()
