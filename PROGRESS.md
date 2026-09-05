# Project Progress

Total planned phases: 60

---

## Phase 01 — Environment & Git Foundation

Status: COMPLETE

### Concepts Learned

- Fedora development environment
- GCC
- Make
- Strict compiler warnings
- Debug builds
- AddressSanitizer
- UndefinedBehaviorSanitizer
- Basic automated testing
- Git repository foundation

### Tests

- Strict compilation: PASS
- Program execution: PASS
- Smoke test: PASS
- ASan: PASS
- UBSan: PASS
- ELF inspection: PASS

---

## Phase 02 — Linux Processes, File Descriptors, /proc, and strace

Status: COMPLETE

### Concepts Learned

- Linux process identity using PID and PPID
- Per-process file descriptor tables
- Standard descriptors 0, 1, and 2
- `/proc/<PID>/fd`
- `/proc/<PID>/status`
- `/proc/<PID>/limits`
- `/proc/<PID>/maps`
- System calls
- Syscall tracing using strace

### Tests

- Strict compilation: PASS
- Process inspection test: PASS
- Regression smoke test: PASS
- ASan/UBSan: PASS

---

## Phase 03 — TCP/IP Fundamentals and Socket Lifecycle

Status: COMPLETE

### Concepts Learned

- IPv4 loopback networking
- TCP server socket lifecycle
- `socket()`
- `setsockopt()`
- `bind()`
- `listen()`
- `accept()`
- `recv()`
- `send()`
- Listening versus connected sockets
- TCP ports
- Ephemeral client ports
- Network byte order
- `SO_REUSEADDR`
- Socket inspection with `ss`
- Socket FDs through `/proc`
- Socket syscall tracing using `strace`

### Tests

- Strict compilation: PASS
- Manual TCP connection: PASS
- `ss` socket inspection: PASS
- `/proc` FD inspection: PASS
- `strace` socket lifecycle: PASS
- TCP integration test: PASS
- Regression suite: PASS
- ASan/UBSan: PASS


## Phase 04 — Robust Accept Loop and Graceful Shutdown

Status: COMPLETE

### Concepts Learned

- Long-lived listening sockets
- Repeated `accept()` loops
- File descriptor reuse
- `errno`
- `EINTR`
- POSIX signals
- `sigaction()`
- `sig_atomic_t`
- `SIGINT`
- `SIGTERM`
- Graceful process shutdown
- `SIGPIPE` avoidance
- Sequential blocking server limitations

### Tests

- Strict compilation: PASS
- Multiple sequential clients: PASS
- File descriptor lifecycle: PASS
- SIGINT shutdown: PASS
- SIGTERM shutdown: PASS
- EINTR handling: PASS
- strace inspection: PASS
- Integration tests: PASS
- Regression tests: PASS

---

## Phase 05 — TCP I/O, EOF, and Connection Lifecycle

Status: COMPLETE

### Concepts Learned

- TCP byte-stream semantics
- Repeated `recv()` processing
- `recv()` return-value semantics
- Orderly EOF
- TCP half-close
- `shutdown()`
- `close()` versus `shutdown()`
- EINTR during socket I/O
- Partial send semantics
- `send_all()`
- `SIGPIPE`
- `MSG_NOSIGNAL`
- `EPIPE`
- Connection-scoped failures

### Tests

- Repeated stream I/O: PASS
- EOF detection: PASS
- TCP half-close: PASS
- Multiple sequential clients: PASS
- Graceful shutdown: PASS
- Integration tests: PASS
- Regression tests: PASS

---

## Phase 06 — TCP Fragmentation, Short I/O, and Buffering

Status: COMPLETE

### Concepts Learned

- TCP stream fragmentation
- Arbitrary `recv()` boundaries
- Send and receive boundary independence
- Partial write semantics
- Fixed-capacity byte buffers
- Buffer offsets
- Buffer compaction
- Pending-byte tracking
- Exact byte-stream validation
- Foundations of backpressure

### Tests

- `io_buffer` initialization: PASS
- `io_buffer` produce/consume: PASS
- `io_buffer` compaction: PASS
- Fragmented one-byte sender: PASS
- Mismatched send/read chunk sizes: PASS
- 32 KB binary stream integrity: PASS
- EOF handling: PASS
- Regression tests: PASS
- ASan/UBSan buffer tests: PASS
- ASan/UBSan TCP tests: PASS

## Phase 07 — Non-Blocking Sockets and EAGAIN

Status: COMPLETE

### Concepts Learned

- Blocking versus non-blocking I/O
- fcntl()
- F_GETFL
- F_SETFL
- O_NONBLOCK
- EAGAIN
- EWOULDBLOCK
- Non-blocking accept()
- Non-blocking recv()
- Temporary readiness failures
- socketpair()
- Busy-loop avoidance
- Relationship between buffering and non-blocking writes

### Tests

- O_NONBLOCK flag verification: PASS
- Empty non-blocking recv(): PASS
- EAGAIN/EWOULDBLOCK handling: PASS
- Receive after data arrival: PASS
- Empty non-blocking accept(): PASS
- strace verification: PASS
- Regression tests: PASS
- ASan/UBSan: PASS

## Phase 08 — First epoll Event Loop

Status: COMPLETE

### Concepts Learned

- epoll_create1()
- EPOLL_CLOEXEC
- epoll_ctl()
- EPOLL_CTL_ADD
- EPOLL_CTL_DEL
- epoll_wait()
- EPOLLIN
- EPOLLRDHUP
- EPOLLERR
- EPOLLHUP
- Level-triggered epoll
- Event dispatch
- Accept-until-EAGAIN
- Read-until-EAGAIN
- Multiplexing multiple client sockets
- epoll as a file descriptor

### Tests

- epoll instance creation: PASS
- Listener registration: PASS
- Multiple simultaneous clients: PASS
- Idle-client isolation: PASS
- Non-blocking accept loop: PASS
- Non-blocking recv loop: PASS
- EOF cleanup: PASS
- strace epoll verification: PASS
- Regression tests: PASS
- ASan/UBSan: PASS


## Phase 09 — Per-Connection State and FD Lifecycle

Status: COMPLETE

### Concepts Learned

- Per-connection state objects
- Logical connection IDs
- File descriptor reuse
- epoll_event.data.ptr
- Tagged event sources
- Connection ownership
- Dynamic allocation with calloc()
- Linked-list connection tracking
- Centralized teardown
- Resource acquisition failure paths
- Memory and FD lifecycle
- Use-after-free prevention

### Tests

- Connection initialization: PASS
- Event-source association: PASS
- Logical connection IDs: PASS
- Rapid connection churn: PASS
- FD reuse safety: PASS
- Connection teardown: PASS
- Server shutdown cleanup: PASS
- Integration tests: PASS
- Regression tests: PASS
- ASan/UBSan: PASS


## Phase 10 — Stage 1 Integration and 100-Client Stress Test

Status: COMPLETE

### Concepts Learned

- Concurrent TCP connection scaling
- Stage 1 subsystem integration
- Active connection accounting
- Peak connection accounting
- Aggregate byte accounting
- Kernel-visible FD measurement
- /proc/<PID>/fd under concurrency
- Stress-test design
- Simultaneous versus sequential connections
- Resource cleanup under connection churn
- Event-loop concurrency baseline
- Sanitizer testing under stress

### Tests

- 100 simultaneous connections: PASS
- Peak active connections = 100: PASS
- Aggregate stream processing: PASS
- /proc FD growth validation: PASS
- All clients reach EOF: PASS
- Active connections return to zero: PASS
- Connection teardown under churn: PASS
- Integration regressions: PASS
- Stress regression: PASS
- ASan/UBSan stress test: PASS

### Stage 1 Result

The server can multiplex at least 100 simultaneous non-blocking TCP
connections through a single epoll event loop while maintaining explicit
per-connection state and clean resource ownership.


## Phase 11 — Deterministic Python HTTP Backends

Status: COMPLETE

### Concepts Learned

- Deterministic mock upstream services
- Python `ThreadingHTTPServer`
- Backend identity in responses
- Controlled status-code injection
- Controlled response-delay injection
- Stable local upstreams for repeatable proxy integration tests
- Independent backend ports for later routing tests

### Backend Layout

- backend-a: `127.0.0.1:9001`
- backend-b: `127.0.0.1:9002`
- Responses include backend name, method, path, status, and delay
- Test hooks support custom `status` and `delay_ms`

### Tests

- backend-a identity: PASS
- backend-b identity: PASS
- custom HTTP status: PASS
- controlled delay: PASS
- backend startup/readiness: PASS
- backend cleanup: PASS

### Next Phase

Phase 12 — HTTP/1.1 request framing and exact request boundaries.


## Phase 12 — HTTP/1.1 Request Framing

Status: COMPLETE

### Concepts Learned

- HTTP over TCP byte streams
- Request-line framing
- CRLF
- CRLF-CRLF header termination
- Content-Length
- Header/body boundaries
- Fragmented HTTP requests
- Coalesced HTTP requests
- Exact message consumption
- Case-insensitive header names
- Duplicate Content-Length handling
- Transfer-Encoding rejection
- Strict framing
- Foundations of HTTP request-smuggling defense

### Tests

- Simple GET framing: PASS
- Fragmented headers: PASS
- Content-Length body: PASS
- Partial body detection: PASS
- Case-insensitive Content-Length: PASS
- Equal duplicate Content-Length: PASS
- Conflicting Content-Length rejection: PASS
- Invalid Content-Length rejection: PASS
- Transfer-Encoding rejection: PASS
- Malformed request-line rejection: PASS
- Coalesced request framing: PASS
- One-byte HTTP integration: PASS
- Multi-request integration: PASS
- Regression tests: PASS
- ASan/UBSan: PASS

## Phase 13 — Non-Blocking Asynchronous Upstream Connections

Status: COMPLETE

### Concepts Learned

- Proxy downstream and upstream sockets
- Non-blocking connect()
- EINPROGRESS
- Asynchronous TCP connection establishment
- EPOLLOUT during connect
- getsockopt()
- SO_ERROR
- Backend connection state machines
- Backend FD ownership
- Multiple epoll registrations per logical connection
- Deferred connection-state reclamation
- Idempotent teardown
- Backend failure isolation

### Tests

- Non-blocking upstream socket creation: PASS
- EINPROGRESS handling: PASS
- Backend epoll registration: PASS
- SO_ERROR connect completion: PASS
- Backend A connection: PASS
- Backend failure isolation: PASS
- Downstream/backend FD cleanup: PASS
- Deferred free under event batching: PASS
- /proc dual-socket inspection: PASS
- strace asynchronous connect verification: PASS
- Regression tests: PASS
- ASan/UBSan: PASS


## Phase 14 — Bidirectional Buffered Forwarding

Status: COMPLETE

### Concepts Learned

- End-to-end reverse-proxy data path
- Client-to-backend buffering
- Backend-to-client buffering
- HTTP framing without discarding request bytes
- Ready-byte accounting for complete HTTP frames
- Dynamic EPOLLOUT registration
- Non-blocking request forwarding
- Non-blocking response forwarding
- Partial send preservation
- Client half-close while awaiting a response
- Backend EOF with downstream drain
- Two-direction socket lifecycle

### Tests

- Request reaches backend-a: PASS
- Backend response reaches client: PASS
- Fragmented request through proxy: PASS
- HTTP status preserved: PASS
- Backend identity preserved: PASS
- Backend EOF handling: PASS
- Downstream response drain: PASS
- Dynamic EPOLLOUT handling: PASS
- Regression tests: PASS
- ASan/UBSan: PASS

### Current Limitation

The per-direction buffers are still fixed-size. If a producer fills a
buffer before the consumer can drain it, Phase 14 closes the connection
safely.

Phase 15 replaces that behavior with true backpressure by pausing reads
and resuming them when output drains.



## Phase 15 — Partial Writes and Backpressure

Status: COMPLETE

### Concepts Learned

- Non-blocking partial writes
- send() short-write preservation
- EAGAIN/EWOULDBLOCK on output
- Producer/consumer imbalance
- Backpressure
- High-water and low-water marks
- Dynamic EPOLLIN throttling
- Dynamic EPOLLOUT draining
- Client read pause/resume
- Backend read pause/resume
- Bounded userspace buffers
- Slow-consumer protection
- Flow-control state transitions

### Tests

- Partial send preservation: PASS
- Client-to-backend buffering: PASS
- Backend-to-client buffering: PASS
- Slow downstream client: PASS
- Large backend response: PASS
- Backend EPOLLIN pause: PASS
- Backend EPOLLIN resume: PASS
- Full response integrity after pause/resume: PASS
- Existing bidirectional forwarding: PASS
- Stage 2 closure regression suite: PASS in audit snapshot
- Stage 2 closure ASan/UBSan proxy suite: PASS in audit snapshot

### Current Limitation

HTTP request framing still uses a bounded fixed-size request buffer. A single
request that cannot be framed within that bound is rejected rather than grown
dynamically. This is intentional for the current learning stage.


## Phase 16 — Connection and Upstream Timeouts

Status: COMPLETE

### Concepts Learned

- CLOCK_MONOTONIC for elapsed-time decisions
- Deadline-driven epoll_wait timeouts
- Backend connect timeout state
- Incomplete HTTP request timeout
- Backend response-start timeout
- Idle connection timeout
- Per-connection activity timestamps
- Runtime timeout configuration through environment variables
- Timeout cleanup through the existing deferred-reclamation path
- Timeout observability and counters

### Timeout Defaults

- Backend connect: 3000 ms
- Idle connection: 30000 ms
- Incomplete HTTP request: 10000 ms
- Backend response start: 10000 ms

The defaults can be overridden with:

- MICROPROXY_CONNECT_TIMEOUT_MS
- MICROPROXY_IDLE_TIMEOUT_MS
- MICROPROXY_REQUEST_TIMEOUT_MS
- MICROPROXY_RESPONSE_TIMEOUT_MS

### Tests

- Idle connection timeout: PASS in review build
- Incomplete request timeout: PASS in review build
- Backend response-start timeout: PASS in review build
- Invalid timeout configuration: PASS in review build
- Existing bidirectional forwarding: PASS in review build
- Backpressure regression: PASS in review build
- 100-client stress regression: PASS in review build
- Phase 16 ASan/UBSan epoll suite: PASS in review build
- Stage 2 closure audit: PASS for proxy/integration/stress paths; final local full-suite gate required before Phase 20 commit

### Scope Note

The backend response timeout currently protects time-to-first-byte. Full HTTP
response-completion deadlines require upstream HTTP response framing, which is
not yet part of the proxy data model.

### Next Phase

Phase 17 — Round-robin backend selection.


## Phase 17 — Round-Robin Backend Selection

Status: COMPLETE

### Concepts Learned

- Backend pool abstraction
- Deterministic round-robin selection
- Separating routing policy from socket lifecycle
- Per-connection backend identity
- Selection at first upstream connection attempt
- Stable routing across the lifetime of one logical proxy connection
- Routing observability through explicit selection logs

### Backend Pool

- backend-a: 127.0.0.1:9001
- backend-b: 127.0.0.1:9002
- Selection order: A, B, A, B, ...

### Tests

- Backend pool unit rotation: PASS in review build
- Six sequential proxy requests alternate A/B: PASS in review build
- Backend identity preserved in response: PASS in review build
- Existing forwarding regression: PASS in review build
- Existing timeout regression: PASS in review build
- Existing backpressure regression: PASS in review build
- Phase 17 ASan/UBSan paths: PASS in Stage 2 closure audit
- Stage 2 closure audit: PASS for proxy/integration/stress paths; final local full-suite gate required before Phase 20 commit

### Scope Note

Round robin currently selects a backend when a logical connection first needs
an upstream socket. Backend health, admission limits, retry policy, and
latency-aware routing remain later phases. A failed selected backend is not
retried in Phase 17.

### Next Phase

Phase 18 — Backend connection limits and admission control.


## Phase 18 — Backend Connection Limits and Admission Control

Status: COMPLETE

### Concepts Learned

- Per-backend connection capacity
- Admission control before opening an upstream socket
- Capacity reservation for connecting and connected upstreams
- Skipping saturated backends during round-robin selection
- Clean rejection when every backend is saturated
- Centralized backend-slot release on EOF, connect failure, timeout, and teardown
- Capacity observability through active/max counters
- Admission-rejection accounting

### Capacity Model

- Default per-backend limit: 64 active upstream slots
- Runtime override: `MICROPROXY_BACKEND_MAX_CONNECTIONS`
- A slot is reserved when a backend is selected, before non-blocking `connect()` completes
- A slot is released as soon as the upstream socket closes or the attempt is torn down
- Round robin skips any backend whose active count has reached its configured maximum

### Tests

- Backend pool capacity unit tests: PASS in review build
- Saturated backends reject a third request: PASS in review build
- Capacity releases after slow requests complete: PASS in review build
- New request succeeds after capacity recovery: PASS in review build
- Round-robin regression: PASS in review build
- Timeout/backpressure/forwarding regressions: PASS in review build
- 100-client stress regression: PASS in review build
- Phase 18 admission control under ASan/UBSan: PASS in review build
- Stage 2 closure audit: PASS for proxy/integration/stress paths; final local full-suite gate required before Phase 20 commit

### Scope Note

Phase 18 limits concurrent upstream connection slots, but it does not queue
requests when all backends are full. A saturated pool rejects the logical
connection cleanly. Retry safety and more advanced failure policy remain later
phases.

### Next Phase

Phase 19 — Failure handling and retry safety.


## Phase 19 — Failure Handling and Retry Safety

Status: COMPLETE

### Concepts Learned

- Backend failover after pre-send connection failure
- Retry eligibility based on upstream transmission state
- Excluding the failed backend from a retry selection
- Preserving backend admission accounting across retries
- Retry suppression after any request byte reaches an upstream
- Avoiding duplicate side effects through conservative replay policy
- Retry and suppression observability

### Retry Policy

- Maximum automatic failover retries per logical connection: 1
- A retry is allowed only while `bytes_forwarded_to_backend == 0` and no backend response bytes have arrived
- The failed backend is excluded from the retry selection
- The failed backend slot is released before the alternate backend is acquired
- Once any request byte has been written upstream, automatic replay is forbidden
- Backend EOF after request transmission is therefore surfaced to the client rather than replayed elsewhere

### Tests

- Failed backend-a connect safely fails over to backend-b: PASS in review build
- Alternate backend produces the real response after safe failover: PASS in review build
- `/drop` closes after receiving a request and is not replayed to backend-b: PASS in review build
- Retry suppression is logged after upstream request bytes were sent: PASS in review build
- Backend-pool exclusion selection: PASS in review build
- Round-robin regression: PASS in review build
- Admission-control regression: PASS in review build
- Timeout/backpressure/forwarding regressions: PASS in review build
- Stage 2 closure audit: PASS for proxy/integration/stress paths; final local full-suite gate required before Phase 20 commit

### Scope Note

Phase 19 deliberately implements only provably safe pre-send failover. It does not
replay requests after any request bytes have reached a backend, even for GET.
Supporting post-send retries would require retaining a replayable request copy and
defining method/idempotency policy carefully.

### Next Phase

Phase 20 — Stage 2 performance baseline and reverse-proxy benchmark.


## Phase 20 — Stage 2 Performance Baseline

Status: IN PROGRESS

### Concepts Learned

- Benchmark methodology before optimization
- Direct-backend versus proxy comparison
- Warm-up versus measured requests
- Fixed request count and concurrency
- Throughput measurement in requests/second
- Mean/min/max request latency as a basic Stage 2 baseline
- Error-rate accounting
- Round-robin distribution validation during load
- Separating optimized benchmark builds from debug and sanitizer builds
- Machine-readable benchmark artifacts

### Benchmark Build

- Benchmark binary: `build/epoll_server-bench`
- Compiler profile: `-O2 -DNDEBUG` with the project's strict warning flags
- Sanitizers are intentionally excluded from performance measurement
- Proxy/backend terminal output is redirected by the harness to reduce terminal-rendering noise

### Baseline Workload

- Direct control targets: backend-a `:9001` and backend-b `:9002`, alternated evenly
- Proxy target: `127.0.0.1:8081`
- Proxy upstreams: backend-a `:9001`, backend-b `:9002`
- Default warm-up: 100 requests per measured path
- Default measured load: 1000 requests per path
- Default concurrency: 32
- Connection model: one TCP connection per HTTP request
- Result: `benchmarks/phase20_baseline.json`

### Tests / Gates

- Benchmark harness argument validation: PASS in review build
- Optimized benchmark proxy build: PENDING LOCAL GATE
- Direct-backend benchmark completes without errors: PENDING LOCAL GATE
- Proxy benchmark completes without errors: PENDING LOCAL GATE
- Proxy traffic reaches both backend-a and backend-b: PENDING LOCAL GATE
- JSON baseline artifact generated: PENDING LOCAL GATE
- Full local `make test` / `make sanitize`: PENDING USER GATE

### Scope Note

Phase 20 establishes a repeatable Stage 2 reference point. It deliberately does
not optimize the proxy and does not claim formal p50/p95/p99, CPU, memory, FD,
or syscall profiling. Those are deeper performance-engineering tasks in later
roadmap phases.

### Next Phase

Phase 21 — Process model and Linux isolation foundations.


## Stage 2 Closure Gate

Status: PENDING LOCAL FINAL GATE

The Stage 2 closure audit corrected stale regression assertions from earlier phases,
restored the missing Phase 11 progress entry, and re-ran the complete proxy
integration/stress suite plus the ASan/UBSan proxy suite successfully in the audit
snapshot.

Before declaring Stage 2 closed and committing Phase 20, run locally:

```bash
make clean
make test
make sanitize
make benchmark
jq . benchmarks/phase20_baseline.json
git diff --check
git status --short
```

Required final conditions:

- full normal unit/integration/stress suite passes;
- full sanitizer suite passes;
- benchmark optimized binary builds;
- direct benchmark has zero failures;
- proxy benchmark has zero failures;
- proxy benchmark reaches both backend-a and backend-b;
- `benchmarks/phase20_baseline.json` exists and parses;
- no whitespace errors remain.

After those conditions pass, set Phase 20 to `COMPLETE` and this Stage 2 closure
status to `COMPLETE`.


## Phase 21 — Linux Process Model and Namespace Inspection

Status: IMPLEMENTED — AWAITING LOCAL FEDORA QUALITY GATE

### Concepts Learned

- Process identity: PID versus parent PID; processes versus threads
- Kernel namespace types: user, pid, mount, net, IPC, UTS, cgroup, time
- `/proc/<PID>/ns/*` namespace handles and inode identities
- `readlink()` displays namespace labels; `stat()` compares kernel objects
- Namespace membership is not equivalent to a security boundary
- Inspecting another process does not require `unshare()` or privilege
- Namespace creation and PID/mount isolation are reserved for Phase 22

### Implementation

- `src/namespace_inspect.c`: inspect own and optional target PID namespace membership
- `tests/unit/test_namespace_inspect.py`: self, child, invalid PID and argument tests
- Makefile targets: `make inspect-namespaces`, inclusion in `make test-unit`
- Existing proxy network event loop remains unchanged

### Quality Gate

- `make clean && make test`: PENDING LOCAL FEDORA RUN
- `make sanitize`: PENDING LOCAL FEDORA RUN
- `make inspect-namespaces`: PENDING LOCAL FEDORA RUN
- `./build/namespace_inspect $$`: PENDING LOCAL FEDORA RUN (use a real PID)
- `git diff --check`: PENDING LOCAL FEDORA RUN
- Commit only after all gates pass

### Scope Note

Phase 21 inspects namespace identities without creating namespace boundaries.
A separate worker/supervisor is not introduced: process supervision belongs to Stage 5.

### Next Phase

Phase 22 — PID and mount namespace experiments and isolated process-tree verification.


## Phase 22 — PID and Mount Namespace Isolation

Status: IMPLEMENTED — AWAITING LOCAL FEDORA QUALITY GATE

### Concepts Learned

- Linux `unshare(CLONE_NEWUSER)` and UID/GID mapping for rootless experiments
- `CLONE_NEWNS` creates a separate mount namespace
- `CLONE_NEWPID` affects only subsequently forked children
- The first child in a new PID namespace becomes PID 1
- `/proc` must be remounted in the isolated mount namespace to reflect its PID tree
- `MS_REC | MS_PRIVATE` prevents mount propagation back to the host
- `waitpid()` reaps child processes to prevent zombies
- Exit status 77 distinguishes host restrictions from implementation failures

### Implementation

- `src/pid_mount_lab.c`: disposable process experiment, never launches the proxy
- `tests/unit/test_pid_mount_lab.py`: CLI, bad args, and kernel isolation verification
- Makefile: `make pid-mount-lab` and integration in `make test-unit`
- Existing Stage 2 epoll proxy and routing logic are unchanged

### Quality Gate

- strict compilation: PENDING LOCAL FEDORA RUN
- CLI tests: PENDING LOCAL FEDORA RUN
- PID 1 and PID 2 visibility: PENDING LOCAL FEDORA RUN
- mount namespace /proc visibility: PENDING LOCAL FEDORA RUN
- `make test`: PENDING LOCAL FEDORA RUN
- `make sanitize`: PENDING LOCAL FEDORA RUN
- `git diff --check`: PENDING LOCAL FEDORA RUN

A 77/skip result is **not** isolation PASS; diagnose kernel user namespace
settings or repeat the experiment in a disposable Fedora VM. Do not bypass
namespace restrictions on a production host merely to satisfy this phase.

### Next Phase

Phase 23 — Network namespaces and virtual Ethernet.


## Phase 23 — Network Namespaces and Virtual Ethernet

Status: IN PROGRESS

### Concepts Learned

- A network namespace owns its own interfaces, addresses, routes, sockets, and loopback device
- `/proc/<PID>/ns/net` identifies the kernel network-namespace object
- A veth pair behaves like an in-kernel Ethernet cable: packets entering one endpoint emerge from the peer
- Moving each veth endpoint into a different network namespace creates a private point-to-point L2 path
- Interfaces start administratively down and loopback must be enabled independently in each network namespace
- Same-subnet `/30` addressing allows the two namespaces to communicate without a router
- Network-namespace creation and veth manipulation require `CAP_NET_ADMIN`; this phase uses a disposable root-only lab
- Cleanup must be idempotent because partial namespace creation can otherwise leave stale names/interfaces behind

### Implementation

- `scripts/netns_veth_lab.sh`
  - `setup`: create two named namespaces and one veth pair
  - `verify`: compare namespace identities and test bidirectional ICMP reachability
  - `status`: show per-namespace links, addresses, and routes
  - `cleanup`: safely remove the disposable topology, including partial leftovers
- `tests/integration/test_netns_veth.py`
  - CLI/help validation
  - invalid-command rejection
  - unique per-test namespace/interface names
  - real root/CAP_NET_ADMIN setup/verify/cleanup test
  - duplicate-setup rejection
  - bidirectional ping verification
  - distinct host/edge/backend namespace identity verification
  - idempotent cleanup verification
- `make test` may explicitly SKIP the privileged topology when run unprivileged
- `make phase23-gate` reruns the real topology with sudo and does not accept that skip as Phase 23 completion

### Lab Topology

```text
namespace: shp23-edge                  namespace: shp23-backend
10.203.0.1/30                          10.203.0.2/30
       p23-edge0 <========================> p23-back0
                           veth
```

### Quality Gate

- Phase 22 PID/mount isolation must already be genuinely verified: PENDING USER CONFIRMATION
- `scripts/netns_veth_lab.sh help`: PENDING LOCAL FEDORA RUN
- root setup creates both namespaces and both veth endpoints: PENDING LOCAL FEDORA RUN
- host, edge, and backend network namespace IDs are all distinct: PENDING LOCAL FEDORA RUN
- edge -> backend ping succeeds: PENDING LOCAL FEDORA RUN
- backend -> edge ping succeeds: PENDING LOCAL FEDORA RUN
- duplicate setup is rejected rather than destroying an existing topology: PENDING LOCAL FEDORA RUN
- cleanup succeeds twice and leaves no `shp23-*` namespace: PENDING LOCAL FEDORA RUN
- `make test`: PENDING LOCAL FEDORA RUN
- `make sanitize`: PENDING LOCAL FEDORA RUN
- `git diff --check`: PENDING LOCAL FEDORA RUN

A normal unprivileged `make test` skip is not sufficient to complete Phase 23.
The privileged integration run must actually create, verify, and clean up the
network namespaces and veth pair on the Fedora development host.

### Scope Note

Phase 23 demonstrates L2/L3 isolation only. It does not yet run the micro-proxy
inside the namespace, configure host-to-namespace routing/NAT, create cgroups, or
build the Python isolation launcher. Those are later Stage 3 responsibilities.

### Next Phase

Phase 24 — cgroups v2 foundations and bounded resource groups.


## Phase 24 — cgroups v2 Foundations and Resource Accounting

Status: IN PROGRESS

### Concepts Learned

- Namespaces isolate visibility; cgroups control and account for resource consumption
- Fedora/systemd uses the unified cgroup v2 hierarchy exposed at `/sys/fs/cgroup`
- `cgroup.controllers` lists controllers available to children of a cgroup
- `cgroup.subtree_control` determines which controllers are enabled for child cgroups
- `cgroup.procs` moves whole processes between cgroups by PID
- `/proc/<PID>/cgroup` is the process-side source of truth for cgroup membership
- `cpu.stat`, `memory.current`, and `pids.current` expose per-cgroup accounting
- A cgroup must be empty before its directory can be removed
- Cleanup must be idempotent and must never kill unknown processes merely to satisfy a test
- Phase 24 intentionally reads accounting only; memory, CPU, and PID limits are introduced separately

### Implementation

- `scripts/cgroup_v2_lab.sh`
  - `setup`: create one disposable leaf cgroup and move one `sleep` worker into it
  - `verify`: cross-check `cgroup.procs`, `/proc/<PID>/cgroup`, and controller accounting
  - `status`: print root controllers, subtree control, membership, and accounting files
  - `cleanup`: stop only the recorded lab worker, refuse unknown remaining tasks, and remove the leaf
  - does not write `cgroup.subtree_control` or resource-limit files
- `tests/integration/test_cgroup_v2.py`
  - CLI/help validation
  - invalid-command rejection
  - unique per-test cgroup/state names
  - real root-backed setup/verify/cleanup test
  - duplicate-setup rejection
  - PID membership verification
  - CPU/memory/PID accounting verification
  - idempotent cleanup verification
- `make test` may explicitly SKIP the privileged creation test when run unprivileged
- `make phase24-gate` reruns the real cgroup test with sudo and does not accept that skip as completion

### Lab Model

```text
/sys/fs/cgroup
      |
      +-- shp24-lab/              disposable cgroup v2 leaf
             |
             +-- cgroup.procs     contains one lab worker PID
             +-- cpu.stat         CPU accounting
             +-- memory.current   current charged memory
             +-- pids.current     current task/process count
```

### Quality Gate

- Phase 23 network namespace/veth isolation genuinely verified: PENDING USER CONFIRMATION
- unified cgroup v2 hierarchy detected: PENDING LOCAL FEDORA RUN
- root controller and subtree state inspected: PENDING LOCAL FEDORA RUN
- disposable cgroup leaf created: PENDING LOCAL FEDORA RUN
- worker PID appears in leaf `cgroup.procs`: PENDING LOCAL FEDORA RUN
- worker `/proc/<PID>/cgroup` reports the same cgroup path: PENDING LOCAL FEDORA RUN
- `cpu.stat` readable and exposes numeric `usage_usec`: PENDING LOCAL FEDORA RUN
- `memory.current` readable and numeric: PENDING LOCAL FEDORA RUN
- `pids.current` reports at least one task: PENDING LOCAL FEDORA RUN
- duplicate setup rejected: PENDING LOCAL FEDORA RUN
- cleanup succeeds twice and leaves no Phase 24 cgroup: PENDING LOCAL FEDORA RUN
- `make test`: PENDING LOCAL FEDORA RUN
- `make sanitize`: PENDING LOCAL FEDORA RUN
- `git diff --check`: PENDING LOCAL FEDORA RUN

A normal unprivileged test skip is not sufficient to complete Phase 24. The
privileged integration run must actually create the leaf cgroup, move the worker,
verify membership/accounting, and remove the leaf on Fedora.

### Scope Note

Phase 24 establishes cgroup v2 hierarchy mechanics and accounting only. It does
not intentionally trigger OOM, set `memory.max`, throttle CPU with `cpu.max`, or
launch the proxy inside a cgroup. Memory/OOM and CPU quota behavior are separate
later phases so each resource-control mechanism can be understood and tested in
isolation.

### Next Phase

Phase 25 — Memory limits, memory pressure, and controlled OOM behavior.


## Phase 25 — Memory Limits, Memory Pressure, and Controlled OOM

Status: IN PROGRESS

### Concepts Learned

- `memory.current` reports current memory charged to a cgroup; `memory.max` imposes a hard upper boundary
- Virtual allocation alone is not enough for a deterministic memory test; pages must be touched so the kernel actually charges them
- A pressure worker must enter the cgroup before allocation begins, otherwise host-side allocation can race ahead of the limit
- `memory.swap.max=0` (when available) prevents swap from masking the memory.max experiment
- `memory.events` exposes `max`, `oom`, and `oom_kill` counters that prove enforcement happened in the cgroup
- A cgroup OOM kill is scoped to the offending cgroup workload; it does not mean the entire host is globally out of memory
- `cgroup.events populated 0` confirms the workload has left the cgroup after the kill
- Cleanup after OOM differs from normal cleanup because the recorded PID may already be dead
- Phase 25 intentionally uses one disposable memory worker and does not yet throttle CPU

### Implementation

- `src/memory_pressure.c`
  - requires an explicit `--mib` target
  - optionally stops itself with SIGSTOP before allocation
  - allocates anonymous memory and touches every page to force real memory charging
  - emits READY/START/COMPLETE markers so tests can distinguish lifecycle stages
- `scripts/cgroup_memory_lab.sh`
  - `setup`: create one Phase 25 leaf, set memory.max, disable swap when supported, start the worker paused, move it into the cgroup, then resume it
  - `verify`: wait for the worker to leave the cgroup and require memory.events max/oom/oom_kill counters
  - `status`: show memory.current, memory.peak when available, memory.max, swap state, memory.events, cgroup.events, membership, and worker log
  - `cleanup`: stop only the recorded worker if it survived, refuse unknown remaining tasks, and remove the leaf idempotently
- `tests/unit/test_memory_pressure.py`
  - help contract
  - required/invalid argument checks
  - small allocation completion proving pages are actually touched
- `tests/integration/test_cgroup_memory_oom.py`
  - CLI contract and invalid configuration tests
  - unique root-backed test cgroup
  - 32 MiB memory.max vs 128 MiB touched allocation
  - duplicate setup rejection
  - required OOM/oom_kill event verification
  - verification that the worker did not reach its COMPLETE marker
  - cleanup and second-cleanup verification
- `make test` may skip only the privileged Phase 25 topology when unprivileged
- `make phase25-gate` requires the real root-backed memory/OOM experiment and then runs full regressions/sanitizers

### Default Lab Model

```text
/sys/fs/cgroup
      |
      +-- shp25-memory/
             |
             +-- memory.max = 32 MiB
             +-- memory.swap.max = 0 (when available)
             +-- cgroup.procs -> memory_pressure PID
             |
             +-- memory_pressure --mib 128
                       |
                       +-- touches pages
                       +-- reaches cgroup memory boundary
                       +-- kernel records OOM
                       +-- kernel kills worker
```

### Quality Gate

- Phase 24 cgroup v2 accounting gate genuinely verified: PENDING USER CONFIRMATION
- strict `memory_pressure.c` compilation: PENDING LOCAL FEDORA RUN
- memory-pressure CLI/unit tests: PENDING LOCAL FEDORA RUN
- disposable Phase 25 cgroup created: PENDING LOCAL FEDORA RUN
- `memory.max` equals configured hard limit: PENDING LOCAL FEDORA RUN
- worker is moved into cgroup before SIGCONT: PENDING LOCAL FEDORA RUN
- worker attempts allocation above limit: PENDING LOCAL FEDORA RUN
- `memory.events max >= 1`: PENDING LOCAL FEDORA RUN
- `memory.events oom >= 1`: PENDING LOCAL FEDORA RUN
- `memory.events oom_kill >= 1`: PENDING LOCAL FEDORA RUN
- worker does not emit `MEMORY_PRESSURE_COMPLETE`: PENDING LOCAL FEDORA RUN
- `cgroup.events populated 0` after OOM: PENDING LOCAL FEDORA RUN
- cleanup succeeds twice and leaves no Phase 25 cgroup: PENDING LOCAL FEDORA RUN
- `make test`: PENDING LOCAL FEDORA RUN
- `make sanitize`: PENDING LOCAL FEDORA RUN
- `git diff --check`: PENDING LOCAL FEDORA RUN

An unprivileged skip is not Phase 25 completion. The Fedora sudo-backed gate must
actually enforce memory.max and observe the cgroup OOM event counters.

### Scope Note

Phase 25 studies the memory controller only. It does not write `cpu.max`, benchmark
CPU throttling, or launch the production proxy under cgroup limits. CPU quotas are
Phase 26; orchestration comes later.

### Next Phase

Phase 26 — CPU quotas and throttling with `cpu.max` and `cpu.stat`.
