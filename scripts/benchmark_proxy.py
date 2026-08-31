#!/usr/bin/env python3
"""Repeatable Stage 2 reverse-proxy performance baseline.

This harness intentionally measures the implementation as it exists at the end
of Stage 2. It does not tune the proxy. It compares a direct backend request
path with the same request path through the C proxy and records a machine-
readable JSON result.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import platform
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

HOST = "127.0.0.1"
PROXY_PORT = 8081
BACKEND_A_PORT = 9001
BACKEND_B_PORT = 9002
DEFAULT_PATH = "/phase20/benchmark"


@dataclass(frozen=True)
class RequestResult:
    ok: bool
    latency_ms: float
    backend: str | None
    error: str | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark the Stage 2 proxy against a direct mock backend."
    )
    parser.add_argument(
        "--proxy-bin",
        default="build/epoll_server-bench",
        help="proxy binary to launch (default: build/epoll_server-bench)",
    )
    parser.add_argument(
        "--requests",
        type=int,
        default=1000,
        help="measured requests per target (default: 1000)",
    )
    parser.add_argument(
        "--concurrency",
        type=int,
        default=32,
        help="maximum in-flight requests (default: 32)",
    )
    parser.add_argument(
        "--warmup",
        type=int,
        default=100,
        help="warm-up requests per target (default: 100)",
    )
    parser.add_argument(
        "--path",
        default=DEFAULT_PATH,
        help=f"HTTP path used for the benchmark (default: {DEFAULT_PATH})",
    )
    parser.add_argument(
        "--output",
        default="benchmarks/phase20_baseline.json",
        help="JSON result path (default: benchmarks/phase20_baseline.json)",
    )
    parser.add_argument(
        "--request-timeout",
        type=float,
        default=5.0,
        help="per-request socket timeout in seconds (default: 5.0)",
    )
    args = parser.parse_args()

    if args.requests <= 0:
        parser.error("--requests must be > 0")
    if args.concurrency <= 0:
        parser.error("--concurrency must be > 0")
    if args.warmup < 0:
        parser.error("--warmup must be >= 0")
    if args.request_timeout <= 0:
        parser.error("--request-timeout must be > 0")

    return args


def port_is_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind((HOST, port))
        except OSError:
            return False
    return True


def wait_for_port(port: int, process: subprocess.Popen[Any], timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"process exited with status {process.returncode} before port {port} became ready"
            )
        try:
            with socket.create_connection((HOST, port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.02)
    raise RuntimeError(f"timed out waiting for {HOST}:{port}")


def stop_process(process: subprocess.Popen[Any] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3.0)


def parse_http_response(raw: bytes) -> tuple[int, bytes]:
    marker = b"\r\n\r\n"
    header_end = raw.find(marker)
    if header_end == -1:
        raise ValueError("response did not contain a complete HTTP header block")

    header = raw[:header_end].decode("iso-8859-1")
    body = raw[header_end + len(marker) :]
    lines = header.split("\r\n")
    if not lines:
        raise ValueError("empty HTTP response header")

    parts = lines[0].split()
    if len(parts) < 2:
        raise ValueError(f"invalid HTTP status line: {lines[0]!r}")

    try:
        status = int(parts[1])
    except ValueError as exc:
        raise ValueError(f"invalid HTTP status code: {parts[1]!r}") from exc

    content_length: int | None = None
    for line in lines[1:]:
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        if name.strip().lower() == "content-length":
            content_length = int(value.strip())
            break

    if content_length is not None and len(body) != content_length:
        raise ValueError(
            f"body length mismatch: expected {content_length}, received {len(body)}"
        )

    return status, body


def issue_request(port: int, path: str, timeout: float) -> RequestResult:
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {HOST}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")

    started_ns = time.perf_counter_ns()
    try:
        chunks: list[bytes] = []
        with socket.create_connection((HOST, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall(request)
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)

        elapsed_ms = (time.perf_counter_ns() - started_ns) / 1_000_000.0
        status, body = parse_http_response(b"".join(chunks))
        if status != 200:
            return RequestResult(False, elapsed_ms, None, f"HTTP status {status}")

        payload = json.loads(body.decode("utf-8"))
        backend = payload.get("backend")
        if not isinstance(backend, str):
            return RequestResult(False, elapsed_ms, None, "response missing backend identity")

        return RequestResult(True, elapsed_ms, backend, None)
    except Exception as exc:  # benchmark should report errors, not hide them
        elapsed_ms = (time.perf_counter_ns() - started_ns) / 1_000_000.0
        return RequestResult(False, elapsed_ms, None, f"{type(exc).__name__}: {exc}")


def run_requests(
    ports: tuple[int, ...],
    path: str,
    request_count: int,
    concurrency: int,
    timeout: float,
) -> dict[str, Any]:
    started = time.perf_counter()
    results: list[RequestResult] = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [
            executor.submit(
                issue_request,
                ports[index % len(ports)],
                path,
                timeout,
            )
            for index in range(request_count)
        ]
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())

    duration_s = time.perf_counter() - started
    successes = [result for result in results if result.ok]
    failures = [result for result in results if not result.ok]
    latencies = [result.latency_ms for result in successes]
    backends = Counter(result.backend for result in successes if result.backend is not None)

    if latencies:
        mean_ms = sum(latencies) / len(latencies)
        min_ms = min(latencies)
        max_ms = max(latencies)
    else:
        mean_ms = None
        min_ms = None
        max_ms = None

    return {
        "requests": request_count,
        "successes": len(successes),
        "failures": len(failures),
        "duration_seconds": round(duration_s, 6),
        "requests_per_second": round(len(successes) / duration_s, 3) if duration_s > 0 else 0.0,
        "latency_ms": {
            "mean": round(mean_ms, 3) if mean_ms is not None else None,
            "min": round(min_ms, 3) if min_ms is not None else None,
            "max": round(max_ms, 3) if max_ms is not None else None,
        },
        "backend_distribution": dict(sorted(backends.items())),
        "sample_errors": [result.error for result in failures[:5]],
    }


def warm_up(ports: tuple[int, ...], path: str, count: int, timeout: float) -> None:
    for index in range(count):
        port = ports[index % len(ports)]
        result = issue_request(port, path, timeout)
        if not result.ok:
            raise RuntimeError(f"warm-up request failed on port {port}: {result.error}")


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    proxy_bin = (repo_root / args.proxy_bin).resolve()
    output_path = (repo_root / args.output).resolve()
    backend_script = repo_root / "control-plane" / "mock_backend.py"

    if not proxy_bin.is_file():
        print(f"benchmark error: proxy binary not found: {proxy_bin}", file=sys.stderr)
        return 2
    if not backend_script.is_file():
        print(f"benchmark error: mock backend not found: {backend_script}", file=sys.stderr)
        return 2

    occupied = [
        port
        for port in (PROXY_PORT, BACKEND_A_PORT, BACKEND_B_PORT)
        if not port_is_free(port)
    ]
    if occupied:
        ports = ", ".join(str(port) for port in occupied)
        print(
            f"benchmark error: required port(s) already in use: {ports}. "
            "Stop manual proxy/backend processes first.",
            file=sys.stderr,
        )
        return 2

    backend_a: subprocess.Popen[Any] | None = None
    backend_b: subprocess.Popen[Any] | None = None
    proxy: subprocess.Popen[Any] | None = None

    try:
        quiet = subprocess.DEVNULL
        backend_a = subprocess.Popen(
            [sys.executable, str(backend_script), "--name", "backend-a", "--port", str(BACKEND_A_PORT)],
            cwd=repo_root,
            stdout=quiet,
            stderr=quiet,
        )
        wait_for_port(BACKEND_A_PORT, backend_a)

        backend_b = subprocess.Popen(
            [sys.executable, str(backend_script), "--name", "backend-b", "--port", str(BACKEND_B_PORT)],
            cwd=repo_root,
            stdout=quiet,
            stderr=quiet,
        )
        wait_for_port(BACKEND_B_PORT, backend_b)

        proxy_env = os.environ.copy()
        proxy_env.setdefault("MICROPROXY_BACKEND_MAX_CONNECTIONS", "100000")
        proxy = subprocess.Popen(
            [str(proxy_bin)],
            cwd=repo_root,
            env=proxy_env,
            stdout=quiet,
            stderr=quiet,
        )
        wait_for_port(PROXY_PORT, proxy)

        print(
            f"Warm-up: {args.warmup} direct + {args.warmup} proxy requests",
            flush=True,
        )
        direct_ports = (BACKEND_A_PORT, BACKEND_B_PORT)
        proxy_ports = (PROXY_PORT,)
        warm_up(direct_ports, args.path, args.warmup, args.request_timeout)
        warm_up(proxy_ports, args.path, args.warmup, args.request_timeout)

        print(
            f"Direct backend-pool baseline: {args.requests} requests, concurrency={args.concurrency}",
            flush=True,
        )
        direct = run_requests(
            direct_ports,
            args.path,
            args.requests,
            args.concurrency,
            args.request_timeout,
        )

        print(
            f"Proxy baseline: {args.requests} requests, concurrency={args.concurrency}",
            flush=True,
        )
        proxied = run_requests(
            proxy_ports,
            args.path,
            args.requests,
            args.concurrency,
            args.request_timeout,
        )

        direct_rps = float(direct["requests_per_second"])
        proxy_rps = float(proxied["requests_per_second"])
        direct_mean = direct["latency_ms"]["mean"]
        proxy_mean = proxied["latency_ms"]["mean"]

        comparison: dict[str, Any] = {
            "proxy_throughput_as_percent_of_direct": (
                round((proxy_rps / direct_rps) * 100.0, 3) if direct_rps > 0 else None
            ),
            "proxy_mean_latency_overhead_ms": (
                round(float(proxy_mean) - float(direct_mean), 3)
                if direct_mean is not None and proxy_mean is not None
                else None
            ),
        }

        report = {
            "phase": 20,
            "benchmark": "stage2_reverse_proxy_baseline",
            "methodology": {
                "path": args.path,
                "measured_requests_per_target": args.requests,
                "concurrency": args.concurrency,
                "warmup_requests_per_target": args.warmup,
                "connection_model": "one TCP connection per HTTP request",
                "proxy_binary": os.path.relpath(proxy_bin, repo_root),
                "notes": [
                    "Direct control traffic alternates between backend-a :9001 and backend-b :9002.",
                    "Proxy traffic targets 127.0.0.1:8081 and should distribute across backend-a/backend-b.",
                    "Proxy/backend stdout and stderr are redirected to avoid terminal rendering overhead.",
                    "Phase 20 records a baseline; it does not optimize the proxy.",
                    "Formal latency percentiles and deeper CPU/syscall profiling remain later roadmap phases.",
                ],
            },
            "environment": {
                "system": platform.system(),
                "kernel": platform.release(),
                "machine": platform.machine(),
                "python": platform.python_version(),
            },
            "direct_backend_pool": direct,
            "proxy": proxied,
            "comparison": comparison,
        }

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

        print()
        print("Phase 20 baseline")
        print(f"  direct: {direct['successes']}/{direct['requests']} ok, {direct_rps:.2f} req/s, mean={direct_mean} ms")
        print(f"  proxy:  {proxied['successes']}/{proxied['requests']} ok, {proxy_rps:.2f} req/s, mean={proxy_mean} ms")
        print(f"  proxy backend distribution: {proxied['backend_distribution']}")
        print(f"  result: {output_path}")

        if direct["failures"] != 0 or proxied["failures"] != 0:
            print("benchmark failed: one or more measured requests failed", file=sys.stderr)
            return 1

        distribution = proxied["backend_distribution"]
        if distribution.get("backend-a", 0) == 0 or distribution.get("backend-b", 0) == 0:
            print(
                "benchmark failed: proxy traffic did not reach both configured backends",
                file=sys.stderr,
            )
            return 1

        return 0
    finally:
        stop_process(proxy)
        stop_process(backend_b)
        stop_process(backend_a)


if __name__ == "__main__":
    raise SystemExit(main())
