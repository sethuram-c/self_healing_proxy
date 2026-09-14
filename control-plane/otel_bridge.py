#!/usr/bin/env python3
"""Phase 34 bridge: SHP framed JSON logs -> OTLP/HTTP JSON logs.

The proxy side remains on the bounded/nonblocking Unix-socket telemetry path.
This separate process owns the potentially blocking HTTP export boundary.
"""

from __future__ import annotations

import argparse
import errno
import json
import os
import signal
import socket
import stat
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

MAGIC = b"SHP1"
VERSION = 1
TYPE_EVENT = 1
HEADER = struct.Struct("!4sBBHI")
MAX_PAYLOAD = 65536
SCHEMA = "shp.telemetry.v1"
DEFAULT_TIMEOUT_MS = 500

_stop = False


@dataclass
class Counters:
    received: int = 0
    exported: int = 0
    export_failed: int = 0
    invalid: int = 0


def _signal_handler(_signum: int, _frame: object) -> None:
    global _stop
    _stop = True


def _validate_endpoint(value: str) -> str:
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError("OTLP endpoint must be an http(s) URL")
    if parsed.path in {"", "/"}:
        parsed = parsed._replace(path="/v1/logs")
        return urllib.parse.urlunparse(parsed)
    return value


def _attribute(key: str, value: Any) -> dict[str, Any]:
    if isinstance(value, bool):
        otel_value = {"boolValue": value}
    elif isinstance(value, int):
        otel_value = {"intValue": str(value)}
    else:
        otel_value = {"stringValue": str(value)}
    return {"key": key, "value": otel_value}


def validate_phase33_event(event: Any) -> dict[str, Any]:
    if not isinstance(event, dict):
        raise ValueError("event payload must be a JSON object")

    required = {
        "schema": str,
        "timestamp_unix_ns": int,
        "event": str,
        "request_id": str,
        "trace_id": str,
        "span_id": str,
        "trace_source": str,
        "attempt": int,
        "backend": str,
        "result": str,
        "message": str,
    }
    for key, expected_type in required.items():
        if key not in event or not isinstance(event[key], expected_type):
            raise ValueError(f"missing/invalid Phase 33 field: {key}")

    if event["schema"] != SCHEMA:
        raise ValueError(f"unsupported schema: {event['schema']}")
    if event["timestamp_unix_ns"] <= 0 or event["attempt"] <= 0:
        raise ValueError("timestamp and attempt must be positive")
    if len(event["request_id"]) != 32 or len(event["trace_id"]) != 32 or len(event["span_id"]) != 16:
        raise ValueError("invalid correlation identifier length")
    for key in ("request_id", "trace_id", "span_id"):
        value = event[key]
        if value.lower() != value or any(ch not in "0123456789abcdef" for ch in value):
            raise ValueError(f"{key} must be lowercase hexadecimal")
    if int(event["trace_id"], 16) == 0 or int(event["span_id"], 16) == 0:
        raise ValueError("trace_id/span_id must be nonzero")
    return event


def build_otlp_logs_request(event: dict[str, Any], observed_unix_ns: int | None = None) -> dict[str, Any]:
    event = validate_phase33_event(event)
    if observed_unix_ns is None:
        observed_unix_ns = time.time_ns()

    compact_body = json.dumps(event, separators=(",", ":"), ensure_ascii=False)
    result = event["result"].lower()
    severity_number = 17 if result in {"error", "failed", "failure"} else 9
    severity_text = "ERROR" if severity_number == 17 else "INFO"

    attributes = [
        _attribute("shp.schema", event["schema"]),
        _attribute("event.name", event["event"]),
        _attribute("shp.request_id", event["request_id"]),
        _attribute("shp.trace_source", event["trace_source"]),
        _attribute("shp.attempt", event["attempt"]),
        _attribute("shp.backend", event["backend"]),
        _attribute("shp.result", event["result"]),
        _attribute("shp.message", event["message"]),
    ]

    return {
        "resourceLogs": [
            {
                "resource": {
                    "attributes": [
                        _attribute("service.name", "self-healing-proxy"),
                        _attribute("service.namespace", "shp"),
                        _attribute("telemetry.bridge", "phase34-otlp-http-json"),
                    ]
                },
                "scopeLogs": [
                    {
                        "scope": {
                            "name": "self_healing_proxy.phase34",
                            "version": "1",
                        },
                        "logRecords": [
                            {
                                "timeUnixNano": str(event["timestamp_unix_ns"]),
                                "observedTimeUnixNano": str(observed_unix_ns),
                                "severityNumber": severity_number,
                                "severityText": severity_text,
                                "body": {"stringValue": compact_body},
                                "attributes": attributes,
                                "traceId": event["trace_id"],
                                "spanId": event["span_id"],
                            }
                        ],
                    }
                ],
            }
        ]
    }


def export_otlp_http(endpoint: str, request_doc: dict[str, Any], timeout_ms: int) -> tuple[bool, str]:
    payload = json.dumps(request_doc, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=payload,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json",
            "User-Agent": "shp-phase34-otel-bridge/1",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_ms / 1000.0) as response:
            body = response.read(4 * 1024 * 1024 + 1)
            if len(body) > 4 * 1024 * 1024:
                return False, "OTLP response exceeded 4 MiB"
            if 200 <= response.status < 300:
                return True, f"HTTP {response.status}"
            return False, f"HTTP {response.status}"
    except urllib.error.HTTPError as exc:
        return False, f"HTTP {exc.code}"
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        return False, str(exc)


def recv_exact(conn: socket.socket, length: int) -> bytes | None:
    chunks = bytearray()
    while len(chunks) < length:
        data = conn.recv(length - len(chunks))
        if not data:
            return None if not chunks else bytes(chunks)
        chunks.extend(data)
    return bytes(chunks)


def recv_frame(conn: socket.socket) -> tuple[int, bytes] | None:
    header = recv_exact(conn, HEADER.size)
    if header is None:
        return None
    if len(header) != HEADER.size:
        raise ValueError("truncated SHP header")
    magic, version, frame_type, flags, length = HEADER.unpack(header)
    if magic != MAGIC or version != VERSION or flags != 0:
        raise ValueError("invalid SHP frame header")
    if frame_type != TYPE_EVENT:
        raise ValueError(f"unsupported SHP frame type: {frame_type}")
    if length > MAX_PAYLOAD:
        raise ValueError("SHP payload exceeds maximum")
    payload = recv_exact(conn, length)
    if payload is None or len(payload) != length:
        raise ValueError("truncated SHP payload")
    return frame_type, payload


def prepare_listener(path_text: str) -> socket.socket:
    path = Path(path_text)
    if len(os.fsencode(path_text)) >= 108:
        raise OSError(errno.ENAMETOOLONG, "Unix socket path too long", path_text)

    if path.exists() or path.is_symlink():
        mode = path.lstat().st_mode
        if not stat.S_ISSOCK(mode):
            raise OSError(errno.EEXIST, "refusing to replace non-socket path", path_text)
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            probe.settimeout(0.1)
            probe.connect(path_text)
        except OSError as exc:
            if exc.errno not in {errno.ECONNREFUSED, errno.ENOENT}:
                raise
            path.unlink(missing_ok=True)
        else:
            raise OSError(errno.EADDRINUSE, "live Unix socket already exists", path_text)
        finally:
            probe.close()

    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(path_text)
    os.chmod(path_text, 0o600)
    listener.listen(16)
    listener.settimeout(0.2)
    return listener


def run_bridge(args: argparse.Namespace) -> int:
    endpoint = _validate_endpoint(args.otlp_endpoint)
    counters = Counters()
    listener = prepare_listener(args.socket)
    print(
        f"PHASE34_OTEL_BRIDGE_READY path={args.socket} endpoint={endpoint} "
        f"timeout_ms={args.request_timeout_ms}",
        flush=True,
    )

    exit_code = 0
    try:
        while not _stop and (args.max_events == 0 or counters.received < args.max_events):
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            with conn:
                conn.settimeout(1.0)
                while not _stop and (args.max_events == 0 or counters.received < args.max_events):
                    try:
                        frame = recv_frame(conn)
                    except (ValueError, OSError, socket.timeout) as exc:
                        counters.invalid += 1
                        print(f"PHASE34_INVALID_FRAME error={exc}", file=sys.stderr, flush=True)
                        break
                    if frame is None:
                        break

                    _frame_type, payload = frame
                    counters.received += 1
                    try:
                        event = validate_phase33_event(json.loads(payload.decode("utf-8")))
                        otlp_request = build_otlp_logs_request(event)
                    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
                        counters.invalid += 1
                        print(f"PHASE34_INVALID_EVENT error={exc}", file=sys.stderr, flush=True)
                        continue

                    ok, detail = export_otlp_http(endpoint, otlp_request, args.request_timeout_ms)
                    if ok:
                        counters.exported += 1
                        print(
                            f"PHASE34_EXPORT_OK event={event['event']} attempt={event['attempt']} "
                            f"trace_id={event['trace_id']} detail={detail}",
                            flush=True,
                        )
                    else:
                        counters.export_failed += 1
                        print(
                            f"PHASE34_EXPORT_FAILED event={event['event']} attempt={event['attempt']} "
                            f"detail={detail}",
                            file=sys.stderr,
                            flush=True,
                        )
                        if args.require_export:
                            exit_code = 1
    finally:
        listener.close()
        try:
            Path(args.socket).unlink(missing_ok=True)
        except OSError:
            pass

    print("PHASE34_OTEL_BRIDGE_DONE", flush=True)
    print(f"RECEIVED={counters.received}", flush=True)
    print(f"EXPORTED={counters.exported}", flush=True)
    print(f"EXPORT_FAILED={counters.export_failed}", flush=True)
    print(f"INVALID={counters.invalid}", flush=True)
    return exit_code


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Bridge SHP Phase 33 framed JSON events to OTLP/HTTP JSON logs."
    )
    parser.add_argument("--socket", required=True, help="Unix socket path for SHP framed EVENTs")
    parser.add_argument(
        "--otlp-endpoint",
        default="http://127.0.0.1:4318/v1/logs",
        help="OTLP/HTTP logs endpoint (default: %(default)s)",
    )
    parser.add_argument(
        "--request-timeout-ms",
        type=int,
        default=DEFAULT_TIMEOUT_MS,
        help="per-export HTTP timeout in milliseconds (default: %(default)s)",
    )
    parser.add_argument(
        "--max-events",
        type=int,
        default=0,
        help="exit after N received frames; 0 means run until signalled",
    )
    parser.add_argument(
        "--require-export",
        action="store_true",
        help="exit nonzero if any OTLP export fails",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.request_timeout_ms <= 0 or args.request_timeout_ms > 60_000:
        parser.error("--request-timeout-ms must be in 1..60000")
    if args.max_events < 0:
        parser.error("--max-events must be >= 0")
    try:
        _validate_endpoint(args.otlp_endpoint)
    except ValueError as exc:
        parser.error(str(exc))

    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)
    try:
        return run_bridge(args)
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
