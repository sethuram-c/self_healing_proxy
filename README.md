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
