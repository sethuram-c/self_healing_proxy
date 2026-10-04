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
- Linux namespace inspection plus PID/mount isolation experiments
- Disposable network-namespace + veth isolation lab
- cgroups v2 membership and resource-accounting lab
- cgroup memory limits with controlled OOM verification
- cgroup CPU quotas with scheduler-throttling verification

## Planned later stages

The roadmap later adds:

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



## Phase 23 network namespace lab

Phase 23 creates two disposable named network namespaces connected by a veth
pair. The lab requires root/CAP_NET_ADMIN and does not modify the reverse-proxy
networking core.

```bash
scripts/netns_veth_lab.sh help
sudo scripts/netns_veth_lab.sh setup
sudo scripts/netns_veth_lab.sh status
sudo scripts/netns_veth_lab.sh verify
sudo scripts/netns_veth_lab.sh cleanup
```

The required Phase 23 gate is:

```bash
make phase23-gate
git diff --check
git status --short
```

An unprivileged skip from the network-namespace integration test is useful for
normal regression runs but is not considered Phase 23 verification.

## Phase 24 cgroups v2 lab

Phase 24 introduces the resource-control side of Linux isolation. It creates one
disposable cgroup v2 leaf, moves a sleeper process into it, and verifies
membership plus CPU/memory/PID accounting. It deliberately does **not** set
`memory.max`, `cpu.max`, or `pids.max`; those limits belong to later phases.

```bash
scripts/cgroup_v2_lab.sh help
sudo scripts/cgroup_v2_lab.sh setup
sudo scripts/cgroup_v2_lab.sh status
sudo scripts/cgroup_v2_lab.sh verify
sudo scripts/cgroup_v2_lab.sh cleanup
```

The required Phase 24 gate is:

```bash
make phase24-gate
git diff --check
git status --short
```

A normal unprivileged `make test` may skip the privileged cgroup creation test.
That skip is not Phase 24 completion; the sudo-backed gate must actually create,
verify, and remove the disposable cgroup on the Fedora development host.

## Phase 25 memory limit and controlled OOM lab

Phase 25 turns Phase 24's accounting-only cgroup into a bounded memory domain.
A dedicated C worker allocates and touches anonymous pages so memory is genuinely
charged to the cgroup. The worker starts SIGSTOP-paused, is moved into the leaf,
then resumes only after `memory.max` is installed. This prevents pre-cgroup
allocation from contaminating the experiment.

Default experiment:

```text
memory.max        = 32 MiB
memory.swap.max   = 0 when available
allocation target = 128 MiB
```

Run the lab:

```bash
make build/memory_pressure
scripts/cgroup_memory_lab.sh help
sudo scripts/cgroup_memory_lab.sh setup
sudo scripts/cgroup_memory_lab.sh status
sudo scripts/cgroup_memory_lab.sh verify
sudo scripts/cgroup_memory_lab.sh cleanup
```

The verifier requires `memory.events` to report a real limit hit, OOM, and OOM
kill. A normal unprivileged regression run may explicitly skip the privileged
cgroup portion; that skip is not Phase 25 completion.

Required Fedora gate:

```bash
make phase25-gate
git diff --check
git status --short
```

Phase 25 intentionally does not set `cpu.max`; CPU quota/throttling is Phase 26.

## Phase 26 CPU quota and throttling lab

Phase 26 studies the cgroup v2 CPU controller independently from memory/OOM.
A dedicated CPU-burn worker starts SIGSTOP-paused, is moved into a disposable
cgroup, and resumes only after `cpu.max` is configured. The default quota is
`20000 100000`, meaning roughly 20 ms of CPU time is available per 100 ms
period on one CPU's worth of capacity.

```bash
make build/cpu_pressure
scripts/cgroup_cpu_lab.sh help
sudo scripts/cgroup_cpu_lab.sh setup
sudo scripts/cgroup_cpu_lab.sh status
sudo scripts/cgroup_cpu_lab.sh verify
sudo scripts/cgroup_cpu_lab.sh cleanup
```

The verifier samples `cpu.stat` over a fixed window and requires positive
deltas for `usage_usec`, `nr_periods`, `nr_throttled`, and `throttled_usec`.
It also requires the worker to remain alive, demonstrating throttling rather
than termination.

Required Fedora gate:

```bash
make phase26-gate
git diff --check
git status --short
```

A normal unprivileged regression may skip the real cgroup operation. That skip
is not Phase 26 completion; the sudo-backed test must observe real throttling.
Phase 26 intentionally does not change `memory.max` or implement the Python
isolation launcher; those are separate milestones.

## Phase 27 Python isolation launcher

Phase 27 composes the Stage 3 primitives already verified independently. The
Python launcher creates a disposable cgroup v2 leaf with CPU, memory, and PID
ceilings, creates one named network namespace connected to the host by a veth
pair, starts the requested command SIGSTOP-paused, moves it into the cgroup,
and only then resumes it inside the network namespace.

Default isolation model:

```text
host network namespace
  p27host0 = 10.227.0.1/30
        |
       veth
        |
shp27-edge network namespace
  p27ns0   = 10.227.0.2/30
  workload process
        |
        +-- cgroup /shp27-launch
              cpu.max    = 50000 100000
              memory.max = 64 MiB
              pids.max   = 32
```

Run a disposable workload:

```bash
python3 control-plane/isolation_launcher.py --help
sudo python3 control-plane/isolation_launcher.py start -- /usr/bin/sleep 300
sudo python3 control-plane/isolation_launcher.py status
sudo python3 control-plane/isolation_launcher.py verify
sudo python3 control-plane/isolation_launcher.py cleanup
```

The launcher deliberately provides only host<->namespace connectivity; it does
not configure NAT or Internet access. Phase 28 owns cleanup/security hardening
and production-facing policy.

Required Fedora gate:

```bash
make phase27-gate
git diff --check
git status --short
```

A normal unprivileged regression may skip the real launcher integration. That
skip is not Phase 27 completion; the sudo-backed test must prove the workload is
simultaneously in the expected cgroup and network namespace and that the veth
path is reachable in both directions.

## Phase 28 Stage 3 isolation hardening and closure

Phase 28 hardens the Phase 27 launcher before Stage 3 is declared complete. The
launcher still creates the same cgroup v2 leaf and host-to-namespace veth path,
but the final workload now also runs in a distinct PID namespace and mount
namespace with a fresh `/proc`, drops host-root privilege to the invoking sudo
user (or an explicit non-root UID/GID), clears supplementary groups, and enables
`PR_SET_NO_NEW_PRIVS` before `exec`.

The launcher now exposes two lifecycle-diagnostic commands:

```bash
sudo python3 control-plane/isolation_launcher.py doctor
sudo python3 control-plane/isolation_launcher.py recover
```

`doctor` classifies the instance as `CLEAN`, `LIVE`, `STALE`, or `PARTIAL`.
`recover` is intentionally limited to owned stale state; it refuses automatic
destructive recovery when kernel objects exist without the ownership state
file.

Run a hardened disposable workload:

```bash
sudo python3 control-plane/isolation_launcher.py start -- /usr/bin/sleep 300
sudo python3 control-plane/isolation_launcher.py status
sudo python3 control-plane/isolation_launcher.py verify
sudo python3 control-plane/isolation_launcher.py doctor
sudo python3 control-plane/isolation_launcher.py cleanup
```

`verify` checks all of the following from kernel state rather than from the JSON
bookkeeping file alone:

- exact cgroup v2 membership and CPU/memory/PID ceilings;
- network namespace identity and bidirectional veth reachability;
- a PID namespace distinct from the host with the workload visible as namespace PID 1;
- a mount namespace distinct from the host and a fresh `/proc` for that PID namespace;
- non-root real/effective UID;
- `NoNewPrivs: 1`;
- zero effective Linux capabilities.

Cleanup no longer signals a numeric PID merely because that number is present
in saved state. Modern kernels use the owned cgroup's `cgroup.kill`; the fallback
re-reads `cgroup.procs` and verifies exact cgroup membership before signaling.
This prevents stale PID state or PID reuse from targeting an unrelated host
process.

The Phase 28 gate is:

```bash
make phase28-gate
```

The authoritative Stage 3 closure gate is:

```bash
make clean
make stage3-gate
git diff --check
git status --short
```

`stage3-gate` reruns the namespace inspection/PID-mount checks, every real
privileged Phase 23-28 integration test, the complete regression suite, the
sanitizer suite, and a final launcher `doctor` check that must report
`INSTANCE_STATE=CLEAN`.

A skipped privileged test is not Stage 3 completion. Stage 4 should begin only
after the Fedora Stage 3 gate succeeds and the Phase 28/Stage 3 statuses in
`PROGRESS.md` are changed to `COMPLETE`.

## Phase 29 Unix domain sockets and the local IPC boundary

Stage 4 begins with a deliberately small local transport layer before any
telemetry schema or framing is introduced. Phase 29 adds reusable C helpers for
filesystem-backed `AF_UNIX` / `SOCK_STREAM` endpoints and a blocking lab server
that demonstrates the lifecycle rules the later observability path depends on.

Build and run the lab server:

```bash
make build/uds_server build/test_uds
./build/test_uds
./build/uds_server --socket /tmp/self-healing-proxy-phase29.sock
```

From another shell, connect with Python or `socat` and send arbitrary bytes. The
server echoes the byte stream and logs kernel-supplied peer PID/UID/GID using
Linux `SO_PEERCRED`.

The endpoint is created mode `0600`. Startup refuses to overwrite a regular
file, refuses a socket pathname that still has a live listener, and only removes
a socket inode when a connection probe establishes that the prior listener is
stale. SIGINT/SIGTERM closes the listener and removes the owned pathname.

Automated tests:

```bash
make test-uds
make sanitize-uds
make phase29-gate
```

The Phase 29 server is intentionally blocking and the payload is intentionally
unframed. `SOCK_STREAM` is a byte stream: application messages are not preserved
by `send()` boundaries. Phase 30 adds bounded framing; Phase 31 makes the
telemetry delivery path nonblocking. The production proxy core is not modified
in Phase 29.


## Phase 30 Framed IPC

Phase 30 adds explicit application message boundaries above the Phase 29 Unix
domain `SOCK_STREAM` transport. The wire format is deliberately compact and
bounded:

```text
0               4   5   6       8              12
+---------------+---+---+-------+---------------+
| magic SHP1    |ver|typ| flags | payload_len   |
+---------------+---+---+-------+---------------+
| payload bytes ...                               |
+-------------------------------------------------+
```

All multibyte integers are network byte order. Version is currently `1`, flags
must be zero, and payloads are capped at 64 KiB. Defined frame types are event
(1), control (2), and heartbeat (3). The decoder is incremental: arbitrary
chunks from `recv()` may contain part of a header, part of a payload, exactly
one frame, or several coalesced frames.

Build and run the framed lab:

```bash
make build/ipc_framed_server build/test_ipc_frame
./build/test_ipc_frame
./build/ipc_framed_server --socket /tmp/self-healing-proxy-phase30.sock
```

Targeted validation:

```bash
make test-ipc-framing
make sanitize-ipc-framing
make phase30-gate
```

The framed server is still deliberately blocking and sequential. A malformed
client is disconnected while the server remains available for later clients.
Phase 31 owns nonblocking telemetry delivery, queueing, and integration with the
proxy event loop.

## Phase 31 Nonblocking telemetry path

Phase 31 turns the Phase 30 framed transport into a bounded nonblocking producer
path. The goal is not to maximize telemetry throughput; the goal is to guarantee
that observability cannot stall request processing when the collector is slow,
missing, or disconnected.

The reusable `telemetry_channel` owns a nonblocking Unix-stream fd plus a bounded
FIFO of already-framed Phase 30 messages. `send(..., MSG_DONTWAIT|MSG_NOSIGNAL)`
is attempted opportunistically. `EAGAIN` is recorded as normal backpressure, not
as a fatal application error. `EPOLLOUT` is requested only while a connect is in
progress or queued bytes remain.

The queue has a strict byte cap. When adding another complete frame would exceed
that cap, Phase 31 deliberately drops the newest telemetry frame and increments a
counter. Previously queued order is preserved. When no collector can be
connected, frames are dropped immediately instead of accumulating indefinitely.
A broken collector disconnects only the telemetry channel and drops its remaining
queue; the producer remains healthy.

Build and targeted validation:

```bash
make build/telemetry_nonblocking_lab build/test_telemetry_channel
./build/test_telemetry_channel
python3 tests/integration/test_nonblocking_telemetry.py
make test-nonblocking-telemetry
make sanitize-nonblocking-telemetry
make phase31-gate
```

A useful slow-consumer experiment is to accept the Unix connection but stop
reading. With a small `SO_SNDBUF` the sender reaches `EAGAIN`, the userspace
queue reaches its configured cap, and subsequent telemetry is dropped while the
producer still completes promptly. A healthy draining collector test separately
requires every frame to arrive with zero drops.

Phase 31 is intentionally a reusable transport module plus lab rather than final
request telemetry instrumentation. Phase 32 adds request/trace identity; later
observability phases will attach semantic events to the proxy using this bounded
nonblocking path.

## Phase 32 Request and trace IDs

Phase 32 adds correlation identity above the Phase 31 nonblocking telemetry transport. A logical request receives a locally generated request ID plus W3C trace context. If a canonical version-00 `traceparent` is valid, its trace ID and trace flags are preserved while a new local span ID is created. Missing or malformed trace context never rejects application traffic; a new trace is generated instead.

Accepted version-00 format:

```text
00-<32 lowercase hex trace-id>-<16 lowercase hex parent-id>-<2 hex flags>
```

All-zero trace IDs and parent IDs are invalid. New trace IDs, request IDs, and span IDs use Linux `getrandom()`. Retries/backend attempts keep the same request ID and trace ID while receiving a fresh local span ID.

The Phase 32 lab emits these identifiers inside Phase 30 EVENT frames through the Phase 31 `telemetry_channel`, proving the observability layers compose before Phase 33 adds structured JSON and Phase 36 exports real spans.

```bash
make build/trace_context_telemetry_lab build/test_trace_context
./build/test_trace_context
python3 tests/integration/test_trace_context_telemetry.py
make test-trace-context
make sanitize-trace-context
make phase32-gate
```

## Phase 33 Structured JSON logs

Phase 33 replaces temporary space-delimited telemetry payloads with one stable machine-readable JSON object per event. The schema is versioned as `shp.telemetry.v1` and carries the Phase 32 request/trace/span correlation fields directly:

```json
{
  "schema": "shp.telemetry.v1",
  "timestamp_unix_ns": 1789234567890000000,
  "event": "backend_attempt",
  "request_id": "0123456789abcdef0123456789abcdef",
  "trace_id": "4bf92f3577b34da6a3ce929d0e0e4736",
  "span_id": "00f067aa0ba902b7",
  "trace_source": "remote",
  "attempt": 1,
  "backend": "backend-a",
  "result": "retry",
  "message": "upstream reset"
}
```

The C serializer is deliberately bounded and dependency-free. Event/backend/result/message fields have explicit maximum lengths, the final event is capped by a fixed output buffer, and serialization fails before enqueue if the event cannot fit. Strings are JSON-escaped so quotes, backslashes, control characters, and embedded newlines cannot corrupt the JSON document or inject additional log records.

The JSON object remains the payload of a Phase 30 EVENT frame and is delivered through the Phase 31 bounded nonblocking telemetry channel. Formatting therefore does not change the transport/backpressure guarantees established in earlier phases.

Build and targeted validation:

```bash
make build/json_log_telemetry_lab build/test_json_log
./build/test_json_log
python3 tests/integration/test_json_log_telemetry.py
make test-json-logs
make sanitize-json-logs
make phase33-gate
```

The integration collector parses every payload with Python `json.loads()`, verifies stable request/trace IDs across retries, unique span IDs per attempt, hostile-string round trips, and confirms that an oversized field causes a clean producer failure with no partial frame emitted.

## Phase 34 — OpenTelemetry Collector Integration

Phase 34 introduces the first external observability process while preserving the
proxy-side reliability boundary built in Phases 29–33.

The data path is now:

```text
C proxy / lab
  -> Phase 33 bounded JSON event
  -> Phase 30 SHP1 frame
  -> Phase 31 nonblocking Unix socket
  -> control-plane/otel_bridge.py
  -> OTLP/HTTP JSON POST /v1/logs
  -> OpenTelemetry Collector
  -> debug exporter (Phase 34)
```

The C side does **not** speak HTTP or depend on Collector availability. The bridge
owns HTTP conversion and export. If the Collector is unavailable, the bridge may
fail or stop reading, but the Phase 31 producer still follows its bounded queue /
drop policy instead of blocking the proxy data plane.

### OTLP mapping

The bridge maps each `shp.telemetry.v1` event to one OTLP `LogRecord`:

- `timestamp_unix_ns` -> `timeUnixNano` (decimal string in OTLP JSON)
- `trace_id` -> top-level OTLP `traceId`
- `span_id` -> top-level OTLP `spanId`
- original JSON -> log `body.stringValue`
- request/event/backend/result metadata -> OTLP attributes
- resource `service.name` -> `self-healing-proxy`

OTLP/HTTP JSON uses `POST /v1/logs`, `Content-Type: application/json`, lowerCamelCase
field names, hexadecimal trace/span IDs, and decimal strings for 64-bit timestamp
fields.

### Collector configuration

`observability/otel-collector-config.yaml` enables the Collector OTLP HTTP receiver
on container port `4318`, a small batch processor, and the `debug` exporter. The
Phase 34 container lab publishes that receiver on host port `14318` by default.

The project pins the lab image to:

```text
otel/opentelemetry-collector:0.156.0
```

Override it with `PHASE34_COLLECTOR_IMAGE` if needed.

### Prepare and run the real Collector

Fedora normally has Podman available. Docker is also supported.

```bash
make otel-collector-pull
make otel-collector-start
```

Expected readiness marker:

```text
PHASE34_COLLECTOR_READY endpoint=http://127.0.0.1:14318/v1/logs ...
```

Inspect exported logs:

```bash
./scripts/otel_collector_lab.sh logs
```

Stop it with:

```bash
make otel-collector-stop
```

### Bridge

Start the Collector, then start the SHP-to-OTLP bridge:

```bash
python3 control-plane/otel_bridge.py \
  --socket /tmp/shp34-bridge.sock \
  --otlp-endpoint http://127.0.0.1:14318/v1/logs
```

In another terminal, emit Phase 33 events:

```bash
./build/json_log_telemetry_lab \
  --socket /tmp/shp34-bridge.sock \
  --attempts 3
```

The bridge should print `PHASE34_EXPORT_OK` for accepted events. The Collector
`debug` exporter provides human-readable evidence that the OTLP log record reached
the real Collector pipeline.

### Tests

Fast tests use a local fake OTLP/HTTP endpoint and require no container runtime:

```bash
make test-otel-bridge
```

The real Collector test is explicit:

```bash
make otel-collector-pull
make phase34-real
```

The full gate is:

```bash
make phase34-gate
```

It requires the pinned Collector image to be present locally, verifies real
Collector acceptance, then runs the complete project regression and sanitizer
suites.

Phase 34 intentionally does not define production metrics or completed span
export. Phase 35 introduces metrics; Phase 36 introduces trace/span export.

## Phase 35 — Metrics

Phase 35 derives bounded operational metrics from the same validated `shp.telemetry.v1`
event stream used for logs. The proxy-side architecture does not change: C still emits
bounded nonblocking SHP frames to the local bridge. The bridge owns aggregation and OTLP
HTTP export.

The bridge keeps metrics export disabled by default so Phase 34 behavior remains exactly
compatible. Enable Phase 35 with, for example:

```bash
python3 control-plane/otel_bridge.py \
  --socket /tmp/shp35-bridge.sock \
  --otlp-endpoint http://127.0.0.1:14318/v1/logs \
  --metrics-export-every 10
```

The metrics endpoint is derived automatically as `/v1/metrics`. It can be overridden with
`--metrics-endpoint`.

### Metric semantics

Phase 35 exports cumulative OTLP sums and a gauge:

- `shp.requests`: logical requests, counted when `attempt == 1`.
- `shp.retries`: attempts after the first attempt.
- `shp.backend.attempts`: attempts by bounded backend label and normalized result.
- `shp.requests.completed`: terminal request outcomes.
- `shp.telemetry.log_export_failures`: bridge log-export failures.
- `shp.telemetry.invalid_events`: invalid framed/event input observed by the bridge.
- `shp.metrics.tracked_backends`: gauge of retained backend label values.

Result labels are normalized to `success`, `retry`, `error`, or `other`. Backend label
cardinality is capped (16 by default); overflow values are folded into `__other__`.
This prevents unbounded metric-series growth when backend strings are dynamic or hostile.

The Collector config now contains both `logs` and `metrics` pipelines on the same OTLP HTTP
receiver. The `debug` exporter remains the Phase 35 proof surface.

Fast tests:

```bash
make test-otel-metrics
```

Real Collector proof:

```bash
make otel-collector-pull
make phase35-real
```

Full quality gate:

```bash
make phase35-gate
```

Expected final marker:

```text
Phase 35 gate passed: bounded cumulative OTLP metrics, cardinality control, real Collector acceptance, failure isolation, regressions, and sanitizers completed.
```

Phase 35 deliberately does not invent latency histograms from log timestamps. Request latency
must be measured from a monotonic duration source before it is exported as a histogram.


## Phase 36: Native OTLP Traces

Phase 36 turns the request/trace/span identifiers introduced in Phase 32 into real OpenTelemetry spans. A completed trace contains one `proxy.request` SERVER span and one `proxy.backend_attempt` CLIENT child span per backend attempt. The attempt spans retain the logical request's trace ID, use unique span IDs, and make retry structure explicit.

The trace lab measures duration with `CLOCK_MONOTONIC`, then derives OTLP wall timestamps from a single realtime/monotonic anchor pair. This prevents wall-clock adjustments from corrupting duration while still producing absolute OTLP timestamps. A valid incoming W3C `traceparent` becomes the parent of the local request span.

Build and run the fast tests:

```bash
make build/trace_span_telemetry_lab
make test-otel-traces
make sanitize-otel-traces
```

Run the required real Collector test and full gate:

```bash
make otel-collector-pull
make phase36-real
make phase36-gate
```

The required final marker is:

```text
Phase 36 gate passed: parented OTLP request/backend spans, monotonic timing, remote-parent propagation, real Collector acceptance, failure isolation, regressions, and sanitizers completed.
```


## Phase 37 — Grafana operator view

Phase 37 adds a reproducible local Grafana/Loki/Prometheus/Tempo environment on top of the native OTLP logs, metrics, and traces completed in Phases 34–36. The lab uses pinned `docker.io/grafana/otel-lgtm:0.36.0` and keeps the dashboard in version control. The real gate captures the exact `TRACE_ID` emitted by the Phase 36 lab and retrieves that specific trace through Grafana's Tempo datasource proxy (`/api/traces/<traceID>`), avoiding search-index timing and endpoint-compatibility differences.

Start it with:

```bash
make grafana-lgtm-pull
make grafana-lgtm-start
```

Grafana is exposed at `http://127.0.0.1:13000` (admin/admin for this disposable lab) and OTLP/HTTP at `http://127.0.0.1:14319`. The dashboard UID is `shp-overview`.

The provisioned dashboard shows logical requests, retries, backend attempts grouped by bounded labels, service-scoped Loki logs, and recent Tempo traces. Grafana's built-in LGTM data sources use the stable UIDs `prometheus`, `loki`, and `tempo`, preserving cross-signal navigation.

Fast validation:

```bash
make test-grafana
```

Required real-stack validation:

```bash
make phase37-real
```

Full phase closure:

```bash
make phase37-gate
```

The real integration gate starts the pinned stack, verifies Grafana and the provisioned dashboard, sends Phase 33 log/metric telemetry and a Phase 36 trace, then queries Prometheus, Loki, and Tempo through Grafana's data-source proxy. Grafana remains outside the proxy correctness path; if the visualization stack is unavailable, the Phase 31 bounded local telemetry policy still protects request forwarding.

## Phase 39 - Health State Model

Phase 39 begins Stage 5 (Self-Healing) with a deterministic backend-health state machine. It deliberately separates **health evidence** from **healing actions**: this phase decides what a backend's state means; later phases decide how probes are scheduled, when traffic is removed, and when traffic is restored.

States:

- `UNKNOWN`: no trustworthy evidence yet; still eligible so startup does not deadlock.
- `HEALTHY`: normal traffic-eligible state.
- `SUSPECT`: one or more failures observed, but below the trip threshold; still eligible to avoid reacting to a single transient failure.
- `UNHEALTHY`: failure threshold reached; not traffic eligible and held through a cooldown.
- `RECOVERING`: cooldown elapsed; not yet traffic eligible, and multiple consecutive successes are required before returning to `HEALTHY`.

The default lab policy uses three consecutive failures to trip, a 100 ns simulated cooldown, and two consecutive recovery successes. The implementation accepts injected monotonic timestamps so unit/integration tests are deterministic and never sleep.

Useful commands:

```bash
make test-backend-health
./build/health_state_lab --scenario transient
./build/health_state_lab --scenario trip-recover
make sanitize-backend-health
make phase39-gate
```

Phase boundary: Phase 39 does **not** add active probes, mutate the existing round-robin selector, or automatically eject backends from the production proxy. It establishes the state semantics first. Phase 40 will add active health probing on top of this model.


## Phase 40 - Active Health Probing

Phase 40 turns the Phase 39 health state model into a live evidence pipeline using bounded, nonblocking TCP connect probes. The probe module is deliberately separate from the existing backend selector: it observes reachability and updates `backend_health_t`, but it does not yet change production routing.

Linux nonblocking connect semantics are central to this phase: `connect()` may return `EINPROGRESS`; readiness for writing means the attempt has completed, not necessarily succeeded, so the probe reads `SO_ERROR` to distinguish success from refusal or another socket error. Every in-flight probe also has an explicit monotonic deadline.

Useful commands:

```bash
make test-active-health-probe
make sanitize-active-health-probe
./build/active_health_probe_lab --help
make phase40-gate
```

A healthy local backend can be tested by pointing the lab at any listening TCP port. A closed loopback port should produce `REFUSED` and repeated failures should drive the Phase 39 model into `UNHEALTHY`. If the backend starts listening after cooldown, successful probes move the state through `RECOVERING` and back to `HEALTHY`.

Phase boundary: this is TCP reachability evidence only. HTTP semantic probes, routing ejection, passive request-path evidence, automated restart/remediation, and backoff/jitter policies remain later Stage 5 work.

## Phase 41 - Passive health evidence

Phase 41 adds a single request-path evidence policy on top of the Phase 39 health state machine. The passive layer does not own health states and does not make routing decisions. Its job is to classify one finalized backend attempt as positive, negative, or neutral evidence, then feed only positive/negative evidence into `backend_health_t`.

The transport-health policy is intentionally conservative:

- a completed upstream response is SUCCESS evidence, regardless of HTTP status code;
- upstream connect refused/timeout/error, upstream response timeout, and reset-before-response are FAILURE evidence;
- client disconnects, rejected client requests, local overload, local I/O errors, and application HTTP status are NEUTRAL.

This prevents client-caused or proxy-local failures from poisoning a backend's health score. It also avoids treating an HTTP 500 as proof that the TCP backend is unhealthy; application-level status policy is a separate concern and can be added explicitly later.

Each `passive_health_attempt_t` may be finalized exactly once. A second health report for the same backend attempt is rejected with `EALREADY`, protecting retry/error branches from double-counting the same failure.

Useful commands:

```bash
make test-passive-health
./build/passive_health_lab --scenario mixed
./build/passive_health_lab --scenario neutral
./build/passive_health_lab --scenario trip-recover
make sanitize-passive-health
make phase41-gate
```

The Phase 41 quality marker is:

```text
Phase 41 gate passed: request-path passive health classification, single-count attempt evidence, client/local neutrality, hysteretic recovery, regressions, and sanitizers completed.
```

## Phase 42 - Health-Aware Backend Selection

Phase 42 connects the Phase 39 health state model to backend selection without yet adding remediation. A small selector wrapper combines the existing round-robin/capacity logic with `backend_health_is_eligible()`. `UNKNOWN`, `HEALTHY`, and `SUSPECT` remain selectable; `UNHEALTHY` and `RECOVERING` are skipped.

Selection remains round-robin among eligible backends. Capacity limits still apply, retry exclusion still applies, and if every backend is non-eligible the selector returns no backend without mutating the cursor or connection counters. A backend that completes cooldown and recovery hysteresis rejoins the normal rotation automatically.

Useful commands:

```bash
make test-health-aware-selection
./build/health_aware_selection_lab --scenario skip
./build/health_aware_selection_lab --scenario all-unavailable
./build/health_aware_selection_lab --scenario rejoin
make sanitize-health-aware-selection
make phase42-gate
```

Phase boundary: Phase 42 establishes routing policy over health state. It does not restart backends, launch replacements, change health thresholds dynamically, or implement circuit-breaker backoff/remediation. Those are later Stage 5 responsibilities.

## Phase 43 - Circuit Breaker / Failure Window

Phase 43 adds a bounded per-backend circuit breaker. Failures are retained only inside a monotonic rolling window; reaching the configured threshold opens the circuit. OPEN rejects new selections immediately. After a bounded open duration, HALF_OPEN permits exactly one trial request. A successful trial closes the circuit and clears the failure window; a failed trial immediately reopens it. The new `circuit_aware_pool` composes backend health, connection capacity, retry exclusion, round-robin fairness, and circuit permission without changing the older selector contracts.

Useful commands:

```bash
make test-circuit-breaker
./build/circuit_breaker_lab --scenario open-skip
./build/circuit_breaker_lab --scenario window
./build/circuit_breaker_lab --scenario half-open
make sanitize-circuit-breaker
make phase43-gate
```

## Phase 44 - Bounded Remediation Policy

Phase 44 introduces a decision layer between failure containment and destructive healing actions. It does not restart anything yet. Instead, it answers whether a restart *would be allowed now*.

Default policy in the lab requires both strong signals: backend health must be `UNHEALTHY` and the backend circuit must be `OPEN`. When that evidence is present, the policy still enforces two independent safety bounds: capped exponential backoff and a rolling maximum-attempt budget.

The lab uses a deterministic policy of 10 ns initial backoff, 40 ns maximum backoff, a 100 ns attempt window, and at most three remediation attempts in that window. Tests inject monotonic timestamps; no sleeps are required.

Useful commands:

```bash
make test-remediation-policy
./build/remediation_policy_lab --scenario evidence
./build/remediation_policy_lab --scenario backoff
./build/remediation_policy_lab --scenario budget
./build/remediation_policy_lab --scenario recovery
make sanitize-remediation-policy
make phase44-gate
```

The phase boundary is intentional: `RESTART_BACKEND` is a policy decision only. Phase 44 does not send signals, kill processes, spawn replacements, or mutate isolation state.

## Phase 45 - Remediation Executor

Phase 45 turns an already-approved Phase 44 `RESTART_BACKEND` decision into a bounded Linux process restart. Policy remains outside the executor.

Safety properties:
- capture process identity as PID plus `/proc/<pid>/stat` start time;
- acquire a pidfd for a stable reference to the exact task;
- refuse stale/reused identities before signaling;
- send SIGTERM first and wait only for the configured grace interval;
- escalate to SIGKILL only when the exact process does not terminate in time;
- monitor termination through the pidfd rather than polling a raw PID;
- spawn the replacement through `fork` + `execvp` with a close-on-exec error pipe so exec failure is reported synchronously;
- capture and return the new process identity;
- report restart, stale identity, target-gone, signal, timeout, and spawn failures explicitly.

Targeted commands:

```bash
make test-remediation-executor
./build/remediation_executor_lab --scenario graceful
./build/remediation_executor_lab --scenario escalate
./build/remediation_executor_lab --scenario stale
./build/remediation_executor_lab --scenario spawn-fail
make sanitize-remediation-executor
make phase45-gate
```

Phase boundary: Phase 45 executes one approved restart. It does not decide whether remediation is warranted, maintain the restart budget, recreate namespaces/cgroups, or yet supervise a long-lived fleet of replacements.


## Phase 46 - Supervised Recovery Orchestration

Phase 46 closes the safety gap between a successful replacement `exec()` and permission to receive production traffic. A new process starts in a quarantine state and is supervised through real nonblocking TCP readiness probes.

Recovery is intentionally composed from earlier Stage 5 mechanisms rather than inventing new semantics: Phase 45 supplies the exact replacement identity, Phase 40 supplies real TCP probes, Phase 39 supplies recovery hysteresis, and Phase 43 supplies the circuit state. The orchestrator only declares `READY` when all of the following are true:

```text
replacement exec succeeded
AND consecutive readiness threshold satisfied
AND backend health == HEALTHY
AND circuit == CLOSED
```

Before that point `recovery_orchestrator_can_route()` is false. A startup deadline bounds the entire recovery interval. If the replacement remains unavailable until the deadline, recovery ends in `FAILED / STARTUP_TIMEOUT` instead of leaking traffic to an unready backend.

Startup probe failures are intentionally not counted as fresh production-health failures: while a replacement is quarantined, `ECONNREFUSED` often means the process has not bound its listening socket yet. Such failures reset the readiness streak and remain observable, but do not manufacture another remediation loop.

Useful commands:

```bash
make test-recovery-orchestrator
./build/recovery_orchestrator_lab --scenario ready
./build/recovery_orchestrator_lab --scenario timeout
./build/recovery_orchestrator_lab --scenario restart-fail
make sanitize-recovery-orchestrator
make phase46-gate
```

The Phase 46 completion marker is:

```text
Phase 46 gate passed: restarted backends remain quarantined until bounded real readiness probes satisfy health/circuit recovery, startup deadlines fail closed, regressions, and sanitizers completed.
```

Phase boundary: this phase supervises one replacement process. Fleet-wide concurrency limits, correlated multi-backend failures, and restart-storm coordination remain later Stage 5 work.

## Phase 47 - Fleet-Wide Healing Coordination and Restart-Storm Protection

Phase 47 coordinates remediation admission across the whole backend fleet. Earlier phases deliberately reason about one backend at a time; that is not sufficient when a shared dependency failure makes several backends request healing simultaneously.

The coordinator is a bounded, single-threaded control-plane policy object. It does not execute restarts. Each backend may have at most one queued or in-progress remediation request. Admission is constrained by both a global concurrency limit and a rolling fleet-wide attempt budget. Pending backends are selected with a rotating cursor so a repeatedly failing backend cannot monopolize the healing slots.

Default lab model:

```text
backend count                 4
max concurrent remediations   2 (concurrency scenario) / 1 (fairness,budget)
max attempts per window       3 (budget scenario)
attempt window                100 simulated ns
```

Safety invariants:
- fixed memory: at most 32 backend slots and 64 recent admitted attempts;
- duplicate request for a pending/in-progress backend returns `EALREADY` rather than multiplying work;
- global concurrency denial leaves the request pending and changes no in-progress state;
- global rolling-budget denial leaves the request pending until capacity ages back in;
- only an admitted remediation consumes the rolling attempt budget;
- completion releases exactly one concurrency slot;
- failed admission is side-effect free except observability counters;
- selection starts from `next_index`, preserving fleet fairness across queued backends;
- all timing is caller-supplied monotonic time; backward time is rejected with `ERANGE`.

Useful commands:

```bash
make test-healing-coordinator
./build/healing_coordinator_lab --scenario concurrency
./build/healing_coordinator_lab --scenario fairness
./build/healing_coordinator_lab --scenario budget
make sanitize-healing-coordinator
make phase47-gate
```

The Phase 47 completion marker is:

```text
Phase 47 gate passed: fleet-wide healing enforces global concurrency and rolling restart budgets, preserves fair remediation admission, coalesces duplicate requests, regressions, and sanitizers completed.
```

Phase boundary: Phase 47 only coordinates admission to remediation. Phase 44 still decides whether a backend deserves remediation; Phase 45 executes an admitted restart; Phase 46 supervises one replacement through recovery. Phase 47 does not reconstruct namespaces/cgroups, perform multi-process dependency ordering, or execute healing actions itself.


## Phase 48 - Self-Healing Observability, Fault Injection, and Stage 5 Closure

Phase 48 closes Stage 5 by making healing transitions machine-readable and then using deterministic fault injection to prove the control path rather than merely testing isolated state machines. The new `shp.healing.v1` JSON schema carries monotonic sequence, backend, event, health/circuit/recovery states, action, and reason in a bounded fixed buffer with RFC8259 escaping.

The recovery scenario proves this causal chain:

```text
baseline HEALTHY/CLOSED
  -> three injected backend failures
  -> UNHEALTHY + OPEN
  -> remediation approved
  -> fleet admission allowed
  -> replacement quarantined
  -> readiness success #1
  -> readiness success #2
  -> HEALTHY + CLOSED + READY
  -> ROUTE_ENABLED
```

The restart-storm scenario separately proves that global `CONCURRENCY_LIMIT` and `WINDOW_BUDGET` denials are observable events rather than silent policy decisions.

Useful commands:

```bash
make test-self-healing-observability
./build/self_healing_fault_lab --scenario recover
./build/self_healing_fault_lab --scenario storm
make sanitize-self-healing-observability
make phase48-gate
```

`phase48-gate` is intentionally stronger than a normal phase gate: it explicitly reruns the targeted Stage 5 tests and sanitizers for Phases 39 through 48, then runs the global regression and sanitizer suites. Successful closure prints both the Phase 48 marker and the Stage 5 marker.

Phase boundary: Phase 48 closes the self-healing stage. It does not introduce edge locality, topology-aware routing, edge failover policy, final performance tuning, packaging, or release engineering; those begin in Stage 6.

## Phase 49 - Edge-Awareness Foundations

Phase 49 introduces bounded `region`, `zone`, and `site` metadata and a deterministic locality ranking: SITE, ZONE, REGION, REMOTE. Selection first applies existing safety constraints (health eligibility, circuit permission, connection capacity), then chooses the nearest locality tier that still has a usable backend, preserving the existing round-robin cursor within that tier. If the nearest tier is unavailable it falls back outward; if every backend is unsafe it fails closed.

Useful commands:

```bash
make test-edge-aware-selection
./build/edge_aware_selection_lab --scenario prefer-local
./build/edge_aware_selection_lab --scenario fallback
./build/edge_aware_selection_lab --scenario fail-closed
make sanitize-edge-aware-selection
make phase49-gate
```

## Phase 52 - Adaptive Stability and Stress Testing

Phase 52 stress-tests the adaptive routing policy added in Phases 49-51 instead of adding another routing signal. The new `routing_stability` helper records bounded selection metrics, while `adaptive_stability_lab` drives thousands of deterministic routing decisions under oscillating latency, changing connection pressure, stale measurements, and locality failure. The phase verifies that adaptive scoring does not cause starvation, no-selection gaps, unsafe locality leakage, or permanent trust in stale latency. Use `make test-adaptive-stability`, `make sanitize-adaptive-stability`, and finally `make phase52-gate`.


### Phase 52 mixed-freshness correction
The stress suite found and corrects an important Phase 51 edge case: if at least one peer in the selected locality tier has fresh latency evidence, peers with stale/missing latency are no longer allowed to gain a lower score simply because their latency term disappeared. In mixed-freshness cohorts, only fresh-latency peers participate in adaptive latency/load ranking. If no peer has fresh latency, the selector still falls back to load-only adaptive selection.

## Phase 54 - Routing Decision Observability

Phase 54 makes adaptive edge-routing decisions explainable without changing the existing untraced selection API. `adaptive_aware_pool_acquire_next_traced()` runs the same selector and optionally captures facts already computed during selection: selected backend/index, locality tier, fastest fresh latency, number of fresh and safe peers, best score, cohort threshold, and the chosen backend's normalized latency/load score. The original `adaptive_aware_pool_acquire_next()` remains a thin wrapper with tracing disabled.

`routing_decision` converts that fixed-size trace into the stable `shp.routing.v1` event schema. Serialization is bounded, heap-free, and RFC8259-safe. Locality degradation is explicit (`NONE`, `SITE_UNAVAILABLE`, `SITE_ZONE_UNAVAILABLE`, `LOCAL_UNAVAILABLE`), stale/no-latency selection reports `used_latency=false`, and score/cohort fields allow an operator to answer why a backend was inside the fair selection cohort.

The phase also benchmarks trace capture separately from JSON serialization. The optimized trace-capture path must remain within a generous 3x relative budget and 10 microseconds absolute per selection; Phase 53's routing-performance benchmark is rerun as a regression. JSON formatting is intentionally outside the selector's hot-path benchmark so telemetry export policy can remain decoupled from routing correctness.

Useful commands:

```bash
make test-routing-observability
./build/routing_observability_lab --scenario selected
./build/routing_observability_lab --scenario fallback
./build/routing_observability_lab --scenario load-only
./build/routing_observability_lab-perf --scenario overhead
make sanitize-routing-observability
make phase54-gate
```

Phase boundary: Phase 54 explains routing decisions and bounds trace-capture cost. It does not yet export these events through the Phase 31 telemetry transport, add new routing signals, or tune the Phase 51 weights dynamically.
