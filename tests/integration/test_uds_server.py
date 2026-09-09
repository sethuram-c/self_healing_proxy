#!/usr/bin/env python3
import os
import signal
import socket
import stat
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

BIN = os.environ.get("UDS_SERVER_BIN", "build/uds_server")


def wait_for_socket(path: Path, proc: subprocess.Popen[str], timeout: float = 3.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            stdout, stderr = proc.communicate(timeout=1)
            raise AssertionError(
                f"server exited before socket became ready: rc={proc.returncode}\n"
                f"stdout={stdout}\nstderr={stderr}"
            )
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            time.sleep(0.01)
            continue
        if stat.S_ISSOCK(mode):
            probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            probe.settimeout(0.2)
            try:
                probe.connect(str(path))
            except (ConnectionRefusedError, FileNotFoundError):
                probe.close()
                time.sleep(0.01)
                continue
            except OSError:
                probe.close()
                time.sleep(0.01)
                continue
            else:
                probe.close()
                return
        raise AssertionError(f"expected Unix socket at {path}, found another file type")
    raise AssertionError(f"timed out waiting for Unix socket {path}")


def echo_exchange(path: Path, chunks: list[bytes]) -> bytes:
    payload = b"".join(chunks)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(2)
        client.connect(str(path))
        for chunk in chunks:
            client.sendall(chunk)
        client.shutdown(socket.SHUT_WR)
        received = bytearray()
        while True:
            block = client.recv(4096)
            if not block:
                break
            received.extend(block)
    if bytes(received) != payload:
        raise AssertionError(f"echo mismatch: expected {payload!r}, got {bytes(received)!r}")
    return bytes(received)


class UdsServerTests(unittest.TestCase):
    def run_server_once(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [BIN, *args],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

    def start_server(self, path: Path) -> subprocess.Popen[str]:
        proc = subprocess.Popen(
            [BIN, "--socket", str(path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        wait_for_socket(path, proc)
        return proc

    def stop_server(self, proc: subprocess.Popen[str]) -> tuple[str, str]:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
        return proc.communicate(timeout=5)

    def test_help_and_invalid_cli(self) -> None:
        help_result = self.run_server_once("--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn("AF_UNIX/SOCK_STREAM", help_result.stdout)

        missing = self.run_server_once()
        self.assertEqual(missing.returncode, 2)
        self.assertIn("--socket PATH is required", missing.stderr)

    def test_refuses_regular_file_and_overlong_path(self) -> None:
        with tempfile.TemporaryDirectory(prefix="shp29-file-") as tmp:
            path = Path(tmp) / "telemetry.sock"
            path.write_text("do not delete me", encoding="utf-8")
            result = self.run_server_once("--socket", str(path))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_text(encoding="utf-8"), "do not delete me")

        long_path = "/tmp/" + ("x" * 150)
        result = self.run_server_once("--socket", long_path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("File name too long", result.stderr)

    def test_live_socket_raw_stream_peer_credentials_and_cleanup(self) -> None:
        with tempfile.TemporaryDirectory(prefix="shp29-live-") as tmp:
            path = Path(tmp) / "telemetry.sock"
            proc = self.start_server(path)
            try:
                mode = path.lstat().st_mode
                self.assertTrue(stat.S_ISSOCK(mode))
                self.assertEqual(stat.S_IMODE(mode), 0o600)

                self.assertEqual(echo_exchange(path, [b"alpha", b"-", b"beta"]), b"alpha-beta")
                self.assertEqual(echo_exchange(path, [b"second-client"]), b"second-client")

                duplicate = self.run_server_once("--socket", str(path))
                self.assertNotEqual(duplicate.returncode, 0)
                self.assertIn("Address already in use", duplicate.stderr)
                self.assertIsNone(proc.poll())
            finally:
                stdout, stderr = self.stop_server(proc)

            self.assertEqual(proc.returncode, 0, stderr)
            self.assertFalse(path.exists())
            self.assertIn("PHASE29_UDS_READY", stdout)
            self.assertIn(f"uid={os.getuid()}", stdout)
            self.assertIn(f"gid={os.getgid()}", stdout)
            self.assertIn("PHASE29_UDS_CLEANUP_OK", stdout)

    def test_stale_socket_is_recovered_safely(self) -> None:
        with tempfile.TemporaryDirectory(prefix="shp29-stale-") as tmp:
            path = Path(tmp) / "telemetry.sock"
            stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            stale.bind(str(path))
            stale.close()
            self.assertTrue(stat.S_ISSOCK(path.lstat().st_mode))

            proc = self.start_server(path)
            try:
                self.assertEqual(echo_exchange(path, [b"stale-recovered"]), b"stale-recovered")
            finally:
                stdout, stderr = self.stop_server(proc)

            self.assertEqual(proc.returncode, 0, stderr)
            self.assertFalse(path.exists())
            self.assertIn("PHASE29_UDS_CLEANUP_OK", stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
