# Self-Healing Edge-Aware Micro-Proxy

A Linux-native reverse proxy and observability-engine project implemented in C and Python.

## Current implementation

The project currently includes:

- C11 event-driven TCP networking core
- Linux `epoll`-based multi-client event loop
- Per-connection client/backend state
- HTTP/1.1 request framing with bounded buffers
- Non-blocking asynchronous upstream connects
- Bidirectional buffered proxy forwarding
- Partial-write handling and `EAGAIN` recovery
- High/low-watermark backpressure with dynamic `EPOLLIN` / `EPOLLOUT` interest
- Monotonic idle/request/connect/response-start timeouts
- Two-backend round-robin routing
- Per-backend connection limits and admission control
- Conservative pre-send backend failover with replay suppression after upstream bytes are sent
- Deterministic Python HTTP test backends
- Unit, integration, stress, and ASan/UBSan test targets

## Planned later stages

The roadmap later adds:

- Linux namespaces and cgroups v2
- Unix-domain-socket IPC
- OpenTelemetry metrics and traces
- Watchdog/supervisor-based self-healing
- Failure injection and incident bundles
- Edge-aware latency-based backend routing
- Performance profiling, soak testing, and CI

## Development environment

- Fedora Linux
- GCC and Make
- Python 3
- Git

## Build

```bash
make
```

## Run

```bash
make epoll-server
```

Run a mock upstream in another terminal:

```bash
make mock-backend-a
```

## Test

```bash
make test
```

## Sanitizer check

```bash
make sanitize
```

## Stage 2 performance baseline

Build an optimized benchmark-only proxy binary and compare the direct mock
backend path with the same workload through the proxy:

```bash
make benchmark
```

The result is written to:

```text
benchmarks/phase20_baseline.json
```

The baseline records request success/failure counts, elapsed time, throughput,
mean/min/max latency, backend distribution, and direct-pool-vs-proxy overhead. It is
intentionally measurement-only; later phases perform percentile analysis and
deeper CPU/memory/syscall profiling.


## Stage 2 closure gate

Before moving into Phase 21, run the complete Stage 2 gate:

```bash
make stage2-gate
jq . benchmarks/phase20_baseline.json
git diff --check
git status --short
```

`make stage2-gate` runs the normal unit/integration/stress suite, the sanitizer
suite, and the optimized Phase 20 benchmark in sequence. Stage 2 should only be
marked complete after all three finish successfully on the local development
machine.

