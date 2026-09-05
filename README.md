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

## Planned later stages

The roadmap later adds:

- cgroups v2 resource isolation
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
