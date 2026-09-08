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
