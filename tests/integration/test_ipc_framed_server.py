#!/usr/bin/env python3
import os
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest

SERVER = os.environ.get("IPC_FRAMED_SERVER_BIN", "build/ipc_framed_server")
MAGIC = b"SHP1"
VERSION = 1
HEADER = struct.Struct("!4sBBHI")
MAX_PAYLOAD = 64 * 1024
EVENT = 1
CONTROL = 2
HEARTBEAT = 3


def frame(frame_type: int, payload: bytes) -> bytes:
    return HEADER.pack(MAGIC, VERSION, frame_type, 0, len(payload)) + payload


def recv_exact(sock: socket.socket, n: int) -> bytes:
    out = bytearray()
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise AssertionError(f"EOF after {len(out)} of {n} bytes")
        out.extend(chunk)
    return bytes(out)


def recv_frame(sock: socket.socket):
    raw = recv_exact(sock, HEADER.size)
    magic, version, frame_type, flags, payload_len = HEADER.unpack(raw)
    assert magic == MAGIC
    assert version == VERSION
    assert flags == 0
    payload = recv_exact(sock, payload_len)
    return frame_type, payload


class UdsFramedServerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="shp30-")
        self.path = os.path.join(self.tmp.name, "ipc.sock")
        self.proc = None

    def tearDown(self):
        if self.proc is not None:
            if self.proc.poll() is None:
                self.proc.send_signal(signal.SIGTERM)
            self.proc.communicate(timeout=3)
        self.tmp.cleanup()

    def run_cli(self, *args):
        return subprocess.run(
            [SERVER, *args], capture_output=True, text=True, timeout=5, check=False
        )

    def start_server(self):
        self.proc = subprocess.Popen(
            [SERVER, "--socket", self.path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                out, err = self.proc.communicate(timeout=1)
                self.fail(f"server exited early rc={self.proc.returncode}\n{out}\n{err}")
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                s.settimeout(0.2)
                s.connect(self.path)
                return
            except (FileNotFoundError, ConnectionRefusedError, socket.timeout):
                time.sleep(0.02)
            finally:
                s.close()
        self.fail("framed IPC server did not become connectable")

    def test_help_and_invalid_cli(self):
        help_result = self.run_cli("--help")
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("--socket", help_result.stderr)

        bad = self.run_cli()
        self.assertNotEqual(bad.returncode, 0)

    def test_fragmented_and_coalesced_frames_echo_exactly(self):
        self.start_server()
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect(self.path)

        first = frame(EVENT, b"alpha")
        for byte in first:
            s.sendall(bytes([byte]))
        self.assertEqual(recv_frame(s), (EVENT, b"alpha"))

        joined = frame(CONTROL, b"beta") + frame(HEARTBEAT, b"") + frame(EVENT, b"gamma")
        s.sendall(joined)
        self.assertEqual(recv_frame(s), (CONTROL, b"beta"))
        self.assertEqual(recv_frame(s), (HEARTBEAT, b""))
        self.assertEqual(recv_frame(s), (EVENT, b"gamma"))
        s.close()

    def test_payload_can_span_many_recv_calls(self):
        self.start_server()
        payload = bytes((i % 251 for i in range(32000)))
        wire = frame(EVENT, payload)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(self.path)

        for off in range(0, len(wire), 137):
            s.sendall(wire[off:off + 137])
        self.assertEqual(recv_frame(s), (EVENT, payload))
        s.close()

    def test_malformed_client_is_dropped_but_server_survives(self):
        self.start_server()

        bad = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        bad.settimeout(1)
        bad.connect(self.path)
        oversized = HEADER.pack(MAGIC, VERSION, EVENT, 0, MAX_PAYLOAD + 1)
        bad.sendall(oversized)
        try:
            data = bad.recv(1)
            self.assertEqual(data, b"")
        except ConnectionResetError:
            pass
        bad.close()

        good = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        good.settimeout(2)
        good.connect(self.path)
        good.sendall(frame(EVENT, b"still-alive"))
        self.assertEqual(recv_frame(good), (EVENT, b"still-alive"))
        good.close()

    def test_cleanup_removes_socket(self):
        self.start_server()
        self.assertTrue(os.path.exists(self.path))
        self.proc.send_signal(signal.SIGTERM)
        self.proc.communicate(timeout=3)
        self.assertEqual(self.proc.returncode, 0)
        self.assertFalse(os.path.exists(self.path))


if __name__ == "__main__":
    unittest.main(verbosity=2)
