# Self-Healing Edge-Aware Micro-Proxy

A small Linux reverse proxy built in C to explore event-driven networking, failure handling, observability, process isolation, and runtime routing control.

The proxy uses a single `epoll` loop for client and upstream sockets. Supporting labs cover Linux namespaces/cgroups, local IPC, OpenTelemetry, health checking, recovery, and adaptive backend selection.

## What is implemented

### Proxy core

- non-blocking TCP listener and upstream connections
- per-connection state tracked through `epoll`
- HTTP/1.1 request framing with fixed-size buffers
- bidirectional forwarding with partial-write handling
- high/low-watermark backpressure
- connect, request, response-start, and idle timeouts
- round-robin backend selection
- per-backend connection limits
- pre-send failover with replay protection once upstream bytes have been sent

### Linux isolation labs

- PID and mount namespace inspection
- network namespaces connected with a veth pair
- cgroups v2 accounting
- memory limits and controlled OOM tests
- CPU quota/throttling tests
- Python launcher that combines namespace and cgroup isolation

The privileged isolation tests are separate from the proxy data path and require root or the relevant capabilities.

### Observability

- Unix-domain-socket telemetry transport
- framed IPC protocol
- non-blocking telemetry delivery with bounded drops
- request and trace correlation IDs
- structured JSON events
- OTLP/HTTP log, metric, and trace export
- Grafana/LGTM development stack
- routing-specific dashboards and bounded-cardinality metrics

### Health and recovery

- backend health state machine
- active TCP health probes
- passive request-path health evidence
- health-aware backend selection
- circuit breaker/failure window
- bounded remediation policy
- process restart executor
- replacement readiness and recovery orchestration
- fleet-wide restart-storm protection
- fault-injection tests for recovery paths

### Edge-aware routing

- locality-aware backend preference
- latency freshness tracking
- adaptive latency/load scoring
- stability tests for stale and oscillating signals
- routing decision telemetry
- validated routing-policy configuration
- atomic policy generations with rollback/stale-writer protection
- live reload with `SIGHUP`
- automatic reload through Linux `inotify`

## Build

Requirements used during development:

- Fedora Linux
- GCC
- Make
- Python 3

Build the default target:

```bash
make
```

Build the proxy:

```bash
make epoll-server
```

## Quick local run

Start a backend:

```bash
make mock-backend-a
```

In another terminal, start the proxy using the arguments printed by:

```bash
./build/epoll_server --help
```

For policy-driven routing, use the sample configuration in:

```text
config/routing-policy.conf
```

A running proxy can reload that file either automatically through `inotify` or explicitly with `SIGHUP`.

## Tests

Run the normal unit, integration, and stress suite:

```bash
make test
```

Run sanitizer builds/tests:

```bash
make sanitize
```

The final project gate is:

```bash
make phase60-gate
```

Some namespace/cgroup and real observability-stack checks need elevated privileges or external containers. Their Make targets make that requirement explicit; an unprivileged skip is useful for normal regression testing but is not a substitute for the privileged gate.

## Useful targets

```bash
make test
make sanitize
make benchmark
make phase60-gate
```

Isolation labs:

```bash
make netns-veth-lab
make cgroup-v2-lab
make cgroup-memory-lab
make cgroup-cpu-lab
make isolation-launcher
```

Observability labs:

```bash
make otel-collector-start
make grafana-lgtm-start
make test-otel-bridge
make test-otel-metrics
make test-otel-traces
make test-grafana
```

Use `make -n <target>` if you want to inspect a gate before running it.

## Performance baseline

Phase 20 records a simple local baseline using one TCP connection per request and two mock backends. The checked-in result is in `benchmarks/phase20_baseline.json`.

The recorded run completed 1,000/1,000 requests on both paths. On that machine, the proxy handled about 652 requests/s versus about 741 requests/s for the direct backend pool, with roughly 1.9 ms higher mean request latency. This is a local development baseline, not a general performance claim.

Re-run it with:

```bash
make benchmark
```

See `benchmarks/README.md` for the workload details.

## Repository layout

```text
include/          C interfaces
src/              proxy and lab implementations
control-plane/    Python helpers and telemetry bridge
config/           sample routing policy
observability/    OpenTelemetry/Grafana configuration
scripts/          isolation, collector, and benchmark helpers
tests/            unit, integration, and stress tests
benchmarks/       recorded benchmark output
```

## Project status

The planned 60 phases are complete. `PROGRESS.md` keeps the phase-by-phase record; this README describes the final repository rather than repeating the development diary.

This is a learning/portfolio implementation, not a drop-in production proxy. It intentionally keeps the protocol surface and deployment model small enough that the networking, recovery, and control-flow behavior can be inspected directly.
