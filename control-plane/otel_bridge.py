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
ROUTING_SCHEMA = "shp.routing.v1"
ROUTING_LOCALITIES = ("SITE", "ZONE", "REGION", "REMOTE")
ROUTING_FALLBACKS = ("NONE", "SITE_UNAVAILABLE", "SITE_ZONE_UNAVAILABLE", "LOCAL_UNAVAILABLE")
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


class RoutingMetricsState:
    """Low-cardinality cumulative metrics derived from shp.routing.v1 events."""

    def __init__(self, start_unix_ns: int | None = None):
        self.start_unix_ns = time.time_ns() if start_unix_ns is None else start_unix_ns
        self.decisions_total = 0
        self.by_locality = {name: 0 for name in ROUTING_LOCALITIES}
        self.by_fallback = {name: 0 for name in ROUTING_FALLBACKS}
        self.latency_used = {True: 0, False: 0}

    def observe(self, event: dict[str, Any]) -> None:
        event = validate_routing_event(event)
        self.decisions_total += 1
        self.by_locality[event["locality_rank"]] += 1
        self.by_fallback[event["fallback_reason"]] += 1
        self.latency_used[event["used_latency"]] += 1


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


def validate_routing_event(event: Any) -> dict[str, Any]:
    if not isinstance(event, dict):
        raise ValueError("routing event payload must be a JSON object")
    required = {
        "schema": str, "timestamp_ns": int, "sequence": int, "selected_index": int,
        "backend": str, "locality_rank": str, "fallback_reason": str,
        "fastest_fresh_latency_ns": int, "fresh_latency_candidates": int,
        "safe_locality_candidates": int, "best_score": int, "score_threshold": int,
        "selected_score": int, "latency_component": int, "load_component": int,
        "used_latency": bool,
    }
    for key, expected in required.items():
        if key not in event or type(event[key]) is not expected:
            raise ValueError(f"missing/invalid routing field: {key}")
    if event["schema"] != ROUTING_SCHEMA:
        raise ValueError(f"unsupported schema: {event['schema']}")
    if event["sequence"] <= 0 or event["timestamp_ns"] <= 0 or not event["backend"]:
        raise ValueError("routing sequence/timestamp/backend must be valid")
    if event["locality_rank"] not in ROUTING_LOCALITIES:
        raise ValueError("invalid routing locality")
    if event["fallback_reason"] not in ROUTING_FALLBACKS:
        raise ValueError("invalid routing fallback reason")
    if event["selected_score"] > event["score_threshold"] or event["best_score"] > event["selected_score"]:
        raise ValueError("routing score ordering is invalid")
    return event


def build_otlp_routing_logs_request(event: dict[str, Any], observed_unix_ns: int | None = None) -> dict[str, Any]:
    event = validate_routing_event(event)
    observed = time.time_ns() if observed_unix_ns is None else observed_unix_ns
    compact = json.dumps(event, separators=(",", ":"), ensure_ascii=False)
    attrs = [
        _attribute("shp.schema", event["schema"]),
        _attribute("shp.routing.sequence", event["sequence"]),
        _attribute("shp.routing.monotonic_ns", event["timestamp_ns"]),
        _attribute("shp.routing.backend", event["backend"]),
        _attribute("shp.routing.locality", event["locality_rank"]),
        _attribute("shp.routing.fallback", event["fallback_reason"]),
        _attribute("shp.routing.used_latency", event["used_latency"]),
        _attribute("shp.routing.selected_score", event["selected_score"]),
    ]
    return {"resourceLogs": [{"resource": {"attributes": [
        _attribute("service.name", "self-healing-proxy"),
        _attribute("service.namespace", "shp"),
        _attribute("telemetry.bridge", "phase55-routing"),
    ]}, "scopeLogs": [{"scope": {"name": "self_healing_proxy.phase55", "version": "1"},
        "logRecords": [{"timeUnixNano": str(observed), "observedTimeUnixNano": str(observed),
        "severityNumber": 9, "severityText": "INFO", "body": {"stringValue": compact},
        "attributes": attrs}]}]}]}


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


def build_otlp_metrics_request(metrics: MetricsState, counters: Counters, now_unix_ns: int | None = None, routing: RoutingMetricsState | None = None) -> dict[str, Any]:
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

    if routing is not None:
        metric_docs.extend([
            _sum_metric("shp.routing.decisions", "Routing decisions observed", "{decision}",
                [_sum_point(routing.decisions_total, routing.start_unix_ns, now_ns)]),
            _sum_metric("shp.routing.locality", "Routing decisions by bounded locality tier", "{decision}",
                [_sum_point(v, routing.start_unix_ns, now_ns, [_metric_attr("tier", k)]) for k, v in sorted(routing.by_locality.items())]),
            _sum_metric("shp.routing.fallbacks", "Routing decisions by bounded fallback reason", "{decision}",
                [_sum_point(v, routing.start_unix_ns, now_ns, [_metric_attr("reason", k)]) for k, v in sorted(routing.by_fallback.items())]),
            _sum_metric("shp.routing.latency_signal", "Routing decisions grouped by fresh-latency usage", "{decision}",
                [_sum_point(v, routing.start_unix_ns, now_ns, [_metric_attr("used", str(k).lower())]) for k, v in sorted(routing.latency_used.items())]),
        ])

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
    counters = Counters()
    metrics = MetricsState(max_backends=args.max_backend_labels)
    routing_metrics = RoutingMetricsState()
    listener = prepare_listener(args.socket)
    print(
        f"PHASE34_OTEL_BRIDGE_READY path={args.socket} endpoint={endpoint} "
        f"timeout_ms={args.request_timeout_ms} metrics_endpoint={metrics_endpoint}",
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
                        event = json.loads(payload.decode("utf-8"))
                        schema = event.get("schema") if isinstance(event, dict) else None
                        if schema == SCHEMA:
                            event = validate_phase33_event(event)
                            otlp_request = build_otlp_logs_request(event)
                            metrics.observe(event)
                            event_name = event["event"]
                            event_detail = f"attempt={event['attempt']} trace_id={event['trace_id']}"
                        elif schema == ROUTING_SCHEMA:
                            event = validate_routing_event(event)
                            otlp_request = build_otlp_routing_logs_request(event)
                            routing_metrics.observe(event)
                            event_name = "routing_decision"
                            event_detail = f"sequence={event['sequence']} locality={event['locality_rank']}"
                        else:
                            raise ValueError(f"unsupported schema: {schema}")
                    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
                        counters.invalid += 1
                        print(f"PHASE34_INVALID_EVENT error={exc}", file=sys.stderr, flush=True)
                        continue

                    ok, detail = export_otlp_http(endpoint, otlp_request, args.request_timeout_ms)
                    if ok:
                        counters.exported += 1
                        print(
                            f"PHASE34_EXPORT_OK event={event_name} {event_detail} detail={detail}",
                            flush=True,
                        )
                    else:
                        counters.export_failed += 1
                        print(
                            f"PHASE34_EXPORT_FAILED event={event_name} {event_detail} detail={detail}",
                            file=sys.stderr,
                            flush=True,
                        )
                        if args.require_export:
                            exit_code = 1

                    if args.metrics_export_every > 0 and counters.received % args.metrics_export_every == 0:
                        metrics_doc = build_otlp_metrics_request(metrics, counters, routing=routing_metrics)
                        metrics_ok, metrics_detail = export_otlp_http(
                            metrics_endpoint, metrics_doc, args.request_timeout_ms
                        )
                        if metrics_ok:
                            counters.metrics_exported += 1
                            print(
                                f"PHASE35_METRICS_EXPORT_OK requests={metrics.requests_total} "
                                f"retries={metrics.retries_total} tracked_backends={metrics.tracked_backends} "
                                f"detail={metrics_detail}",
                                flush=True,
                            )
                        else:
                            counters.metrics_export_failed += 1
                            print(
                                f"PHASE35_METRICS_EXPORT_FAILED detail={metrics_detail}",
                                file=sys.stderr,
                                flush=True,
                            )
                            if args.require_metrics_export:
                                exit_code = 1
    finally:
        if (
            counters.received > 0
            and args.metrics_export_every > 0
            and counters.received % args.metrics_export_every != 0
        ):
            metrics_doc = build_otlp_metrics_request(metrics, counters, routing=routing_metrics)
            metrics_ok, metrics_detail = export_otlp_http(
                metrics_endpoint, metrics_doc, args.request_timeout_ms
            )
            if metrics_ok:
                counters.metrics_exported += 1
                print(
                    f"PHASE35_METRICS_EXPORT_OK requests={metrics.requests_total} "
                    f"retries={metrics.retries_total} tracked_backends={metrics.tracked_backends} "
                    f"detail={metrics_detail}",
                    flush=True,
                )
            else:
                counters.metrics_export_failed += 1
                print(
                    f"PHASE35_METRICS_EXPORT_FAILED detail={metrics_detail}",
                    file=sys.stderr,
                    flush=True,
                )
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
    print(f"METRIC_REQUESTS_TOTAL={metrics.requests_total}", flush=True)
    print(f"METRIC_RETRIES_TOTAL={metrics.retries_total}", flush=True)
    print(f"METRIC_TRACKED_BACKENDS={metrics.tracked_backends}", flush=True)
    print(f"METRIC_ROUTING_DECISIONS={routing_metrics.decisions_total}", flush=True)
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
        "--metrics-endpoint",
        default=None,
        help="OTLP/HTTP metrics endpoint; default derives /v1/metrics from --otlp-endpoint",
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
