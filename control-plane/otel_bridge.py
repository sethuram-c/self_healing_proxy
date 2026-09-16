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
DEFAULT_METRICS_EXPORT_EVERY = 0
MAX_BACKEND_LABELS = 16

_stop = False


@dataclass
class Counters:
    received: int = 0
    exported: int = 0
    export_failed: int = 0
    invalid: int = 0
    metrics_exported: int = 0
    metrics_export_failed: int = 0
    traces_exported: int = 0
    traces_export_failed: int = 0


class MetricsState:
    """Bounded cumulative metrics derived from Phase 33 events."""

    def __init__(self, start_unix_ns: int | None = None, max_backends: int = MAX_BACKEND_LABELS):
        if max_backends <= 0:
            raise ValueError("max_backends must be positive")
        self.start_unix_ns = time.time_ns() if start_unix_ns is None else start_unix_ns
        self.max_backends = max_backends
        self.requests_total = 0
        self.retries_total = 0
        self.completions = {"success": 0, "error": 0, "other": 0}
        self.backend_attempts: dict[tuple[str, str], int] = {}
        self._backends: set[str] = set()

    @staticmethod
    def normalize_result(value: str) -> str:
        lowered = value.lower()
        if lowered == "success":
            return "success"
        if lowered == "retry":
            return "retry"
        if lowered in {"error", "failed", "failure"}:
            return "error"
        return "other"

    def backend_label(self, backend: str) -> str:
        if backend in self._backends:
            return backend
        if len(self._backends) < self.max_backends:
            self._backends.add(backend)
            return backend
        return "__other__"

    def observe(self, event: dict[str, Any]) -> None:
        event = validate_phase33_event(event)
        result = self.normalize_result(event["result"])
        backend = self.backend_label(event["backend"])

        if event["attempt"] == 1:
            self.requests_total += 1
        if event["attempt"] > 1:
            self.retries_total += 1
        if result in {"success", "error"}:
            self.completions[result] += 1

        key = (backend, result)
        self.backend_attempts[key] = self.backend_attempts.get(key, 0) + 1

    @property
    def tracked_backends(self) -> int:
        return len(self._backends)


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



def _metric_attr(key: str, value: str) -> dict[str, Any]:
    return _attribute(key, value)


def _sum_metric(name: str, description: str, unit: str, points: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "name": name,
        "description": description,
        "unit": unit,
        "sum": {
            "aggregationTemporality": 2,
            "isMonotonic": True,
            "dataPoints": points,
        },
    }


def _sum_point(value: int, start_ns: int, now_ns: int, attrs: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    point: dict[str, Any] = {
        "startTimeUnixNano": str(start_ns),
        "timeUnixNano": str(now_ns),
        "asInt": str(value),
    }
    if attrs:
        point["attributes"] = attrs
    return point


def _gauge_metric(name: str, description: str, unit: str, value: int, now_ns: int) -> dict[str, Any]:
    return {
        "name": name,
        "description": description,
        "unit": unit,
        "gauge": {
            "dataPoints": [
                {
                    "timeUnixNano": str(now_ns),
                    "asInt": str(value),
                }
            ]
        },
    }


def build_otlp_metrics_request(metrics: MetricsState, counters: Counters, now_unix_ns: int | None = None) -> dict[str, Any]:
    """Build cumulative OTLP metrics with bounded label cardinality."""
    now_ns = time.time_ns() if now_unix_ns is None else now_unix_ns
    start_ns = metrics.start_unix_ns

    backend_points = []
    for (backend, result), value in sorted(metrics.backend_attempts.items()):
        backend_points.append(
            _sum_point(
                value,
                start_ns,
                now_ns,
                [_metric_attr("backend", backend), _metric_attr("result", result)],
            )
        )

    completion_points = [
        _sum_point(
            value,
            start_ns,
            now_ns,
            [_metric_attr("outcome", outcome)],
        )
        for outcome, value in sorted(metrics.completions.items())
    ]

    metric_docs = [
        _sum_metric(
            "shp.requests",
            "Logical requests observed at first backend attempt",
            "{request}",
            [_sum_point(metrics.requests_total, start_ns, now_ns)],
        ),
        _sum_metric(
            "shp.retries",
            "Backend attempts after the first attempt",
            "{retry}",
            [_sum_point(metrics.retries_total, start_ns, now_ns)],
        ),
        _sum_metric(
            "shp.backend.attempts",
            "Backend attempts grouped by bounded backend and normalized result",
            "{attempt}",
            backend_points,
        ),
        _sum_metric(
            "shp.requests.completed",
            "Completed requests grouped by terminal outcome",
            "{request}",
            completion_points,
        ),
        _sum_metric(
            "shp.telemetry.log_export_failures",
            "OTLP log export failures observed by the bridge",
            "{failure}",
            [_sum_point(counters.export_failed, start_ns, now_ns)],
        ),
        _sum_metric(
            "shp.telemetry.invalid_events",
            "Invalid SHP frames or Phase 33 events observed by the bridge",
            "{event}",
            [_sum_point(counters.invalid, start_ns, now_ns)],
        ),
        _gauge_metric(
            "shp.metrics.tracked_backends",
            "Number of distinct backend label values tracked before overflow folding",
            "{backend}",
            metrics.tracked_backends,
            now_ns,
        ),
    ]

    return {
        "resourceMetrics": [
            {
                "resource": {
                    "attributes": [
                        _attribute("service.name", "self-healing-proxy"),
                        _attribute("service.namespace", "shp"),
                        _attribute("telemetry.bridge", "phase35-otlp-metrics"),
                    ]
                },
                "scopeMetrics": [
                    {
                        "scope": {"name": "self_healing_proxy.phase35", "version": "1"},
                        "metrics": metric_docs,
                    }
                ],
            }
        ]
    }


def _metrics_endpoint(logs_endpoint: str, override: str | None = None) -> str:
    if override:
        endpoint = _validate_endpoint(override)
        parsed = urllib.parse.urlparse(endpoint)
        if parsed.path == "/v1/logs":
            parsed = parsed._replace(path="/v1/metrics")
            return urllib.parse.urlunparse(parsed)
        return endpoint
    parsed = urllib.parse.urlparse(_validate_endpoint(logs_endpoint))
    parsed = parsed._replace(path="/v1/metrics")
    return urllib.parse.urlunparse(parsed)



TRACE_SCHEMA = "shp.trace.v1"

def _hex_id(value: Any, length: int, name: str, allow_empty: bool = False) -> str:
    if allow_empty and value == "":
        return ""
    if not isinstance(value, str) or len(value) != length:
        raise ValueError(f"invalid {name} length")
    if value.lower() != value or any(ch not in "0123456789abcdef" for ch in value):
        raise ValueError(f"{name} must be lowercase hexadecimal")
    if int(value, 16) == 0:
        raise ValueError(f"{name} must be nonzero")
    return value

def validate_trace_batch(doc: Any) -> dict[str, Any]:
    if not isinstance(doc, dict) or doc.get("schema") != TRACE_SCHEMA:
        raise ValueError("unsupported trace schema")
    _hex_id(doc.get("request_id"), 32, "request_id")
    _hex_id(doc.get("trace_id"), 32, "trace_id")
    if not isinstance(doc.get("trace_source"), str):
        raise ValueError("trace_source must be a string")
    if not isinstance(doc.get("trace_flags"), int) or not (0 <= doc["trace_flags"] <= 255):
        raise ValueError("trace_flags must be in 0..255")
    spans = doc.get("spans")
    if not isinstance(spans, list) or len(spans) < 2 or len(spans) > 64:
        raise ValueError("trace batch must contain 2..64 spans")
    seen: set[str] = set()
    roots = 0
    for span in spans:
        if not isinstance(span, dict):
            raise ValueError("span must be an object")
        sid = _hex_id(span.get("span_id"), 16, "span_id")
        if sid in seen:
            raise ValueError("duplicate span_id")
        seen.add(sid)
        parent = _hex_id(span.get("parent_span_id"), 16, "parent_span_id", allow_empty=True)
        if parent == "":
            roots += 1
        if span.get("kind") not in {"server", "client"}:
            raise ValueError("unsupported span kind")
        if span.get("result") not in {"success", "retry", "error"}:
            raise ValueError("unsupported span result")
        for key in ("start_unix_ns", "end_unix_ns", "duration_ns", "attempt"):
            if not isinstance(span.get(key), int) or span[key] < 0:
                raise ValueError(f"invalid span field: {key}")
        if span["end_unix_ns"] <= span["start_unix_ns"] or span["duration_ns"] <= 0:
            raise ValueError("span duration/timestamps must be positive")
        wall_delta = span["end_unix_ns"] - span["start_unix_ns"]
        if abs(wall_delta - span["duration_ns"]) > 5_000_000:
            raise ValueError("duration must agree with derived wall timestamps within 5ms")
        if not isinstance(span.get("name"), str) or not span["name"]:
            raise ValueError("span name required")
        if not isinstance(span.get("backend"), str):
            raise ValueError("backend must be a string")
    request_spans = [sp for sp in spans if sp["name"] == "proxy.request"]
    if len(request_spans) != 1:
        raise ValueError("exactly one proxy.request span required")
    request_id = request_spans[0]["span_id"]
    for span in spans:
        if span["name"] == "proxy.backend_attempt" and span["parent_span_id"] != request_id:
            raise ValueError("backend attempt must be child of request span")
    return doc

def _span_kind(kind: str) -> int:
    return 2 if kind == "server" else 3

def _span_status(result: str) -> dict[str, Any]:
    if result == "success":
        return {"code": 1}
    return {"code": 2, "message": result}

def build_otlp_traces_request(doc: dict[str, Any]) -> dict[str, Any]:
    doc = validate_trace_batch(doc)
    spans_out = []
    for span in doc["spans"]:
        attrs = [
            _attribute("shp.request_id", doc["request_id"]),
            _attribute("shp.trace_source", doc["trace_source"]),
            _attribute("shp.result", span["result"]),
            _attribute("shp.duration_ns", span["duration_ns"]),
        ]
        if span["attempt"] > 0:
            attrs.append(_attribute("shp.attempt", span["attempt"]))
        if span["backend"]:
            attrs.append(_attribute("shp.backend", span["backend"]))
        out = {
            "traceId": doc["trace_id"],
            "spanId": span["span_id"],
            "name": span["name"],
            "kind": _span_kind(span["kind"]),
            "startTimeUnixNano": str(span["start_unix_ns"]),
            "endTimeUnixNano": str(span["end_unix_ns"]),
            "attributes": attrs,
            "status": _span_status(span["result"]),
            "flags": doc["trace_flags"],
        }
        if span["parent_span_id"]:
            out["parentSpanId"] = span["parent_span_id"]
        spans_out.append(out)
    return {
        "resourceSpans": [{
            "resource": {"attributes": [
                _attribute("service.name", "self-healing-proxy"),
                _attribute("service.namespace", "shp"),
                _attribute("telemetry.bridge", "phase36-otlp-traces"),
            ]},
            "scopeSpans": [{
                "scope": {"name": "self_healing_proxy.phase36", "version": "1"},
                "spans": spans_out,
            }],
        }]
    }

def _traces_endpoint(logs_endpoint: str, override: str | None = None) -> str:
    if override:
        parsed = urllib.parse.urlparse(override)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise ValueError("OTLP traces endpoint must be an http(s) URL")
        if parsed.path in {"", "/", "/v1/logs"}:
            parsed = parsed._replace(path="/v1/traces")
        return urllib.parse.urlunparse(parsed)
    parsed = urllib.parse.urlparse(_validate_endpoint(logs_endpoint))
    parsed = parsed._replace(path="/v1/traces")
    return urllib.parse.urlunparse(parsed)

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
    metrics_endpoint = _metrics_endpoint(endpoint, args.metrics_endpoint)
    traces_endpoint = _traces_endpoint(endpoint, args.traces_endpoint)
    counters = Counters()
    metrics = MetricsState(max_backends=args.max_backend_labels)
    listener = prepare_listener(args.socket)
    print(
        f"PHASE34_OTEL_BRIDGE_READY path={args.socket} endpoint={endpoint} "
        f"timeout_ms={args.request_timeout_ms} metrics_endpoint={metrics_endpoint} "
        f"traces_endpoint={traces_endpoint}",
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
                        doc = json.loads(payload.decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                        counters.invalid += 1
                        print(f"PHASE34_INVALID_EVENT error={exc}", file=sys.stderr, flush=True)
                        continue

                    schema = doc.get("schema") if isinstance(doc, dict) else None
                    if schema == SCHEMA:
                        try:
                            event = validate_phase33_event(doc)
                            otlp_request = build_otlp_logs_request(event)
                            metrics.observe(event)
                        except ValueError as exc:
                            counters.invalid += 1
                            print(f"PHASE34_INVALID_EVENT error={exc}", file=sys.stderr, flush=True)
                            continue
                        ok, detail = export_otlp_http(endpoint, otlp_request, args.request_timeout_ms)
                        if ok:
                            counters.exported += 1
                            print(
                                f"PHASE34_EXPORT_OK event={event['event']} attempt={event['attempt']} "
                                f"trace_id={event['trace_id']} detail={detail}", flush=True)
                        else:
                            counters.export_failed += 1
                            print(
                                f"PHASE34_EXPORT_FAILED event={event['event']} attempt={event['attempt']} detail={detail}",
                                file=sys.stderr, flush=True)
                            if args.require_export:
                                exit_code = 1

                        if args.metrics_export_every > 0 and counters.received % args.metrics_export_every == 0:
                            metrics_doc = build_otlp_metrics_request(metrics, counters)
                            metrics_ok, metrics_detail = export_otlp_http(
                                metrics_endpoint, metrics_doc, args.request_timeout_ms)
                            if metrics_ok:
                                counters.metrics_exported += 1
                                print(
                                    f"PHASE35_METRICS_EXPORT_OK requests={metrics.requests_total} "
                                    f"retries={metrics.retries_total} tracked_backends={metrics.tracked_backends} "
                                    f"detail={metrics_detail}", flush=True)
                            else:
                                counters.metrics_export_failed += 1
                                print(f"PHASE35_METRICS_EXPORT_FAILED detail={metrics_detail}", file=sys.stderr, flush=True)
                                if args.require_metrics_export:
                                    exit_code = 1
                    elif schema == TRACE_SCHEMA:
                        try:
                            trace_doc = validate_trace_batch(doc)
                            otlp_trace = build_otlp_traces_request(trace_doc)
                        except ValueError as exc:
                            counters.invalid += 1
                            print(f"PHASE36_INVALID_TRACE error={exc}", file=sys.stderr, flush=True)
                            continue
                        trace_ok, trace_detail = export_otlp_http(
                            traces_endpoint, otlp_trace, args.request_timeout_ms)
                        if trace_ok:
                            counters.traces_exported += 1
                            print(
                                f"PHASE36_TRACE_EXPORT_OK trace_id={trace_doc['trace_id']} "
                                f"spans={len(trace_doc['spans'])} detail={trace_detail}", flush=True)
                        else:
                            counters.traces_export_failed += 1
                            print(f"PHASE36_TRACE_EXPORT_FAILED detail={trace_detail}", file=sys.stderr, flush=True)
                            if args.require_trace_export:
                                exit_code = 1
                    else:
                        counters.invalid += 1
                        print(f"PHASE34_INVALID_EVENT error=unsupported schema: {schema}", file=sys.stderr, flush=True)
    finally:
        if (counters.received > 0 and args.metrics_export_every > 0 and
                counters.received % args.metrics_export_every != 0 and metrics.requests_total > 0):
            metrics_doc = build_otlp_metrics_request(metrics, counters)
            metrics_ok, metrics_detail = export_otlp_http(metrics_endpoint, metrics_doc, args.request_timeout_ms)
            if metrics_ok:
                counters.metrics_exported += 1
                print(
                    f"PHASE35_METRICS_EXPORT_OK requests={metrics.requests_total} retries={metrics.retries_total} "
                    f"tracked_backends={metrics.tracked_backends} detail={metrics_detail}", flush=True)
            else:
                counters.metrics_export_failed += 1
                print(f"PHASE35_METRICS_EXPORT_FAILED detail={metrics_detail}", file=sys.stderr, flush=True)
                if args.require_metrics_export:
                    exit_code = 1
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
    print(f"METRICS_EXPORTED={counters.metrics_exported}", flush=True)
    print(f"METRICS_EXPORT_FAILED={counters.metrics_export_failed}", flush=True)
    print(f"TRACES_EXPORTED={counters.traces_exported}", flush=True)
    print(f"TRACES_EXPORT_FAILED={counters.traces_export_failed}", flush=True)
    print(f"METRIC_REQUESTS_TOTAL={metrics.requests_total}", flush=True)
    print(f"METRIC_RETRIES_TOTAL={metrics.retries_total}", flush=True)
    print(f"METRIC_TRACKED_BACKENDS={metrics.tracked_backends}", flush=True)
    return exit_code


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Bridge SHP structured logs/trace batches to OTLP/HTTP logs, metrics, and traces."
    )
    parser.add_argument("--socket", required=True, help="Unix socket path for SHP framed EVENTs")
    parser.add_argument(
        "--otlp-endpoint",
        default="http://127.0.0.1:4318/v1/logs",
        help="OTLP/HTTP logs endpoint (default: %(default)s)",
    )
    parser.add_argument(
        "--metrics-endpoint",
        default=None,
        help="OTLP/HTTP metrics endpoint; default derives /v1/metrics from --otlp-endpoint",
    )
    parser.add_argument(
        "--traces-endpoint",
        default=None,
        help="OTLP/HTTP traces endpoint; default derives /v1/traces from --otlp-endpoint",
    )
    parser.add_argument(
        "--metrics-export-every",
        type=int,
        default=DEFAULT_METRICS_EXPORT_EVERY,
        help="export cumulative metrics every N received events; 0 disables metrics export",
    )
    parser.add_argument(
        "--max-backend-labels",
        type=int,
        default=MAX_BACKEND_LABELS,
        help="maximum distinct backend metric labels before folding to __other__",
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
        help="exit nonzero if any OTLP log export fails",
    )
    parser.add_argument(
        "--require-metrics-export",
        action="store_true",
        help="exit nonzero if any OTLP metrics export fails",
    )
    parser.add_argument(
        "--require-trace-export",
        action="store_true",
        help="exit nonzero if any OTLP trace export fails",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.request_timeout_ms <= 0 or args.request_timeout_ms > 60_000:
        parser.error("--request-timeout-ms must be in 1..60000")
    if args.max_events < 0:
        parser.error("--max-events must be >= 0")
    if args.metrics_export_every < 0:
        parser.error("--metrics-export-every must be >= 0")
    if args.max_backend_labels <= 0 or args.max_backend_labels > 1024:
        parser.error("--max-backend-labels must be in 1..1024")
    try:
        _validate_endpoint(args.otlp_endpoint)
        _traces_endpoint(args.otlp_endpoint, args.traces_endpoint)
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
