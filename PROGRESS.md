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

Status: COMPLETE

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

- `make clean && make test`: PASS
- `make sanitize`: PASS
- `make inspect-namespaces`: PASS
- `./build/namespace_inspect $$`: PASS (use a real PID)
- `git diff --check`: PASS
- Commit only after all gates pass

### Scope Note

Phase 21 inspects namespace identities without creating namespace boundaries.
A separate worker/supervisor is not introduced: process supervision belongs to Stage 5.

### Next Phase

Phase 22 — PID and mount namespace experiments and isolated process-tree verification.


## Phase 22 — PID and Mount Namespace Isolation

Status: COMPLETE

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

- strict compilation: PASS
- CLI tests: PASS
- PID 1 and PID 2 visibility: PASS
- mount namespace /proc visibility: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS

A 77/skip result is **not** isolation PASS; diagnose kernel user namespace
settings or repeat the experiment in a disposable Fedora VM. Do not bypass
namespace restrictions on a production host merely to satisfy this phase.

### Next Phase

Phase 23 — Network namespaces and virtual Ethernet.


## Phase 23 — Network Namespaces and Virtual Ethernet

Status: COMPLETE

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

- Phase 22 PID/mount isolation must already be genuinely verified: PASS
- `scripts/netns_veth_lab.sh help`: PASS
- root setup creates both namespaces and both veth endpoints: PASS
- host, edge, and backend network namespace IDs are all distinct: PASS
- edge -> backend ping succeeds: PASS
- backend -> edge ping succeeds: PASS
- duplicate setup is rejected rather than destroying an existing topology: PASS
- cleanup succeeds twice and leaves no `shp23-*` namespace: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS

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

Status: COMPLETE

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

- Phase 23 network namespace/veth isolation genuinely verified: PASS
- unified cgroup v2 hierarchy detected: PASS
- root controller and subtree state inspected: PASS
- disposable cgroup leaf created: PASS
- worker PID appears in leaf `cgroup.procs`: PASS
- worker `/proc/<PID>/cgroup` reports the same cgroup path: PASS
- `cpu.stat` readable and exposes numeric `usage_usec`: PASS
- `memory.current` readable and numeric: PASS
- `pids.current` reports at least one task: PASS
- duplicate setup rejected: PASS
- cleanup succeeds twice and leaves no Phase 24 cgroup: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS

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

Status: COMPLETE

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

- Phase 24 cgroup v2 accounting gate genuinely verified: PASS
- strict `memory_pressure.c` compilation: PASS
- memory-pressure CLI/unit tests: PASS
- disposable Phase 25 cgroup created: PASS
- `memory.max` equals configured hard limit: PASS
- worker is moved into cgroup before SIGCONT: PASS
- worker attempts allocation above limit: PASS
- `memory.events max >= 1`: PASS
- `memory.events oom >= 1`: PASS
- `memory.events oom_kill >= 1`: PASS
- worker does not emit `MEMORY_PRESSURE_COMPLETE`: PASS
- `cgroup.events populated 0` after OOM: PASS
- cleanup succeeds twice and leaves no Phase 25 cgroup: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS

An unprivileged skip is not Phase 25 completion. The Fedora sudo-backed gate must
actually enforce memory.max and observe the cgroup OOM event counters.

### Scope Note

Phase 25 studies the memory controller only. It does not write `cpu.max`, benchmark
CPU throttling, or launch the production proxy under cgroup limits. CPU quotas are
Phase 26; orchestration comes later.

### Next Phase

Phase 26 — CPU quotas and throttling with `cpu.max` and `cpu.stat`.


## Phase 26 — CPU Quotas and Throttling

Status: COMPLETE

### Concepts Learned

- `cpu.max` controls CPU bandwidth in cgroups v2 using a quota and period
- `20000 100000` means roughly 20 ms of CPU time may be consumed per 100 ms period
- CPU quota exhaustion normally throttles a runnable task instead of killing it
- `cpu.stat` exposes `usage_usec`, `nr_periods`, `nr_throttled`, and `throttled_usec`
- Throttling should be proven from counter deltas across a controlled observation window rather than from wall-clock timing alone
- A CPU-pressure worker should enter the cgroup before its busy loop begins, so Phase 26 again uses a SIGSTOP/SIGCONT handoff
- `/proc/<PID>/cgroup` and `cgroup.procs` are cross-checked to prove membership before measuring throttling
- CPU control and memory/OOM enforcement are deliberately kept separate because their failure semantics differ

### Implementation

- `src/cpu_pressure.c`
  - requires `--duration-ms`
  - optionally raises SIGSTOP before the busy loop
  - burns CPU using a monotonic-clock-bounded deterministic arithmetic loop
  - emits READY/START/COMPLETE markers
- `scripts/cgroup_cpu_lab.sh`
  - `setup`: create a Phase 26 leaf, write `cpu.max`, start the worker paused, move it into the cgroup, then resume it
  - `verify`: sample `cpu.stat`, wait through multiple quota periods, require positive throttling deltas, and prove the worker is still alive
  - `status`: show root controller state, `cpu.max`, `cpu.stat`, cgroup membership, and worker log
  - `cleanup`: terminate only the recorded worker, refuse unknown remaining tasks, and remove the leaf idempotently
- `tests/unit/test_cpu_pressure.py`
  - help contract
  - missing/invalid duration rejection
  - short CPU-burn completion
- `tests/integration/test_cgroup_cpu_throttle.py`
  - CLI and invalid-configuration tests
  - unique root-backed Phase 26 cgroup
  - `cpu.max = 20000 100000`
  - duplicate setup rejection
  - positive deltas for usage, periods, throttle count, and throttled time
  - worker-alive-after-throttling assertion
  - cleanup and second-cleanup verification
- `make phase26-gate` requires the real root-backed throttling experiment and then runs full regressions/sanitizers

### Default Lab Model

```text
/sys/fs/cgroup
      |
      +-- shp26-cpu/
             |
             +-- cpu.max = 20000 100000
             +-- cgroup.procs -> cpu_pressure PID
             +-- cpu.stat
                    |
                    +-- usage_usec increases
                    +-- nr_periods increases
                    +-- nr_throttled increases
                    +-- throttled_usec increases

cpu_pressure remains alive while the scheduler throttles it.
```

### Quality Gate

- Phase 25 memory/OOM gate genuinely verified: PASS
- strict `cpu_pressure.c` compilation: PASS
- CPU-pressure CLI/unit tests: PASS
- CPU controller available to disposable child cgroup: PASS
- `cpu.max` equals `20000 100000`: PASS
- worker enters cgroup before SIGCONT: PASS
- `/proc/<PID>/cgroup` matches Phase 26 leaf: PASS (required-real integration)
- `cpu.stat usage_usec` increases during sample: PASS
- `cpu.stat nr_periods` increases during sample: PASS
- `cpu.stat nr_throttled` increases during sample: PASS
- `cpu.stat throttled_usec` increases during sample: PASS
- worker remains alive after throttling sample: PASS
- duplicate setup rejected: PASS
- cleanup succeeds twice and leaves no Phase 26 cgroup: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS / verify again before Phase 27 commit

An unprivileged skip is not Phase 26 completion. The Fedora sudo-backed gate must
actually set `cpu.max` and observe real `cpu.stat` throttling deltas.

### Scope Note

Phase 26 studies CPU bandwidth only. It does not change Phase 25 memory limits,
combine all isolation mechanisms in one launcher, or run the production proxy
inside the final namespace+cgroup environment. The Python isolation launcher is
Phase 27.

### Next Phase

Phase 27 — Python isolation launcher combining namespaces and cgroups.

## Phase 27 — Python Isolation Launcher

Status: COMPLETE

### Concepts Learned

- Namespace isolation and cgroup resource control are complementary mechanisms and can be composed around one workload
- A launcher must establish resource/network boundaries before allowing the workload to execute
- The Phase 27 handoff uses SIGSTOP/SIGCONT so the child cannot run before cgroup membership is established
- Cgroup membership is verified independently through both `cgroup.procs` and `/proc/<pid>/cgroup`
- Network-namespace membership is verified through `/proc/<pid>/ns/net` and compared with both the host and named namespace identities
- A veth pair can provide a narrow host-to-sandbox control/data path without giving the sandbox a default route or Internet access
- `cpu.max`, `memory.max`, and `pids.max` can be installed together once each controller has already been understood independently
- Cleanup must terminate only the launcher-owned process, reject unknown cgroup occupants, remove kernel objects in a safe order, and remain idempotent
- Phase 27 is composition/orchestration, not yet production security hardening

### Implementation

- `control-plane/isolation_launcher.py`
  - `start -- <command>` creates one cgroup v2 leaf and one named network namespace
  - configures a host<->namespace veth pair using `10.227.0.0/30` by default
  - sets `cpu.max = 50000 100000`, `memory.max = 64 MiB`, and `pids.max = 32`
  - starts a shell wrapper SIGSTOP-paused, writes the PID into `cgroup.procs`, persists state under `/run`, then sends SIGCONT
  - `verify` cross-checks cgroup membership, network namespace identity, resource controls, interface addresses, and bidirectional ping
  - `status` exposes state, workload identity, cgroup accounting/control files, namespace addresses, and workload log
  - `cleanup` terminates only the recorded process group, refuses unknown remaining cgroup tasks, removes veth/netns/cgroup objects, and succeeds idempotently
- `tests/integration/test_isolation_launcher.py`
  - help contract
  - unsafe-name rejection before privileged changes
  - command requirement
  - unique root-backed namespace/cgroup/interface names
  - duplicate start rejection
  - `/proc/<pid>/cgroup` and `/proc/<pid>/ns/net` verification
  - CPU/memory/PID limit verification
  - host-to-namespace and namespace-to-host reachability
  - cleanup and second-cleanup verification
- `make phase27-gate` requires the real composed launcher test and then reruns the complete regression/sanitizer suites

### Default Isolation Model

```text
host network namespace
  |
  +-- p27host0 10.227.0.1/30
           |
           +========== veth =========+
                                      |
                              shp27-edge netns
                                p27ns0 10.227.0.2/30
                                      |
                                      +-- workload PID
                                             |
                                             +-- /shp27-launch cgroup
                                                  cpu.max    50000 100000
                                                  memory.max 67108864
                                                  pids.max   32
```

### Quality Gate

- Phase 26 cpu.max throttling gate genuinely verified: PASS
- launcher help/validation contracts: PASS
- disposable network namespace created: PASS
- host/namespace veth configured and UP: PASS
- disposable cgroup v2 leaf created: PASS
- `cpu.max` equals `50000 100000`: PASS
- `memory.max` equals 64 MiB: PASS
- `pids.max` equals 32: PASS
- workload starts SIGSTOP-paused: PASS
- workload enters cgroup before SIGCONT: PASS
- `/proc/<pid>/cgroup` matches Phase 27 leaf: PASS
- workload network namespace differs from host: PASS
- workload netns identity matches named Phase 27 namespace: PASS
- host -> namespace ping: PASS
- namespace -> host ping: PASS
- duplicate start rejected: PASS
- cleanup succeeds twice and leaves no Phase 27 kernel objects: PASS
- `make test`: PASS
- `make sanitize`: PASS
- `git diff --check`: PASS

An unprivileged skip is not Phase 27 completion. The Fedora sudo-backed gate must
prove that one live workload is simultaneously inside the expected network
namespace and cgroup with all configured limits visible from the kernel.

### Scope Note

Phase 27 composes isolation primitives into one launcher. It intentionally does
not yet drop capabilities, add seccomp, design crash-safe garbage collection, or
harden stale-state recovery. Those cleanup/security concerns are Phase 28.

### Next Phase

Phase 28 — isolation cleanup, stale-state recovery, and security hardening.


## Phase 28 — Stage 3 Isolation Hardening and Closure

Status: COMPLETE

### Concepts Learned

- A dedicated cgroup can be used as a process-ownership boundary for cleanup; a stale JSON PID is not sufficient evidence that a process is still ours.
- PID reuse makes `kill(recorded_pid)` unsafe unless process identity is revalidated with `/proc/<pid>/stat` start time and current cgroup membership.
- `cgroup.kill` provides an atomic cgroup-scoped termination primitive on modern cgroup v2 kernels; the fallback must re-read `cgroup.procs` and verify membership before signaling.
- A composed isolation launcher should reduce host-root privilege before the final workload executes and should set `PR_SET_NO_NEW_PRIVS`.
- `CLONE_NEWPID` affects subsequently forked children; the final workload can become PID 1 while a host-visible helper waits outside the child PID namespace.
- A private mount namespace plus a fresh `/proc` gives tools inside the sandbox a PID-namespace-correct process view without propagating mounts back to the host.
- Runtime state has four useful classifications: CLEAN, LIVE, STALE, and PARTIAL. Automatic recovery is safe only when ownership can still be established.
- Stage closure should rerun every privileged Stage 3 mechanism instead of assuming previous phase commits remain valid after launcher hardening.

### Implementation

- `control-plane/isolation_launcher.py`
  - retains the Phase 27 network namespace, veth, and cgroup limits;
  - adds `doctor` and `recover` operations;
  - stores leader/workload PID start-time identities;
  - refuses host-root final workloads;
  - verifies network, PID, and mount namespace identities independently;
  - verifies non-root UID, `NoNewPrivs: 1`, zero effective capabilities, namespace PID 1, and a fresh isolated `/proc`;
  - cleans up through the owned cgroup instead of blindly signaling a recorded numeric PID;
  - refuses automatic destructive recovery when kernel objects exist without the ownership state file.
- `control-plane/isolation_child.py`
  - internal race-free child helper;
  - starts SIGSTOP-paused so the parent can install cgroup membership first;
  - enters the named network namespace;
  - creates mount and child PID namespaces;
  - makes mount propagation private and mounts a fresh `/proc`;
  - drops to the invoking sudo UID/GID (or explicit Phase 28 UID/GID), clears supplementary groups, enables `PR_SET_NO_NEW_PRIVS`, and execs the command as PID 1.
- `tests/integration/test_stage3_hardening.py`
  - help/security contract tests;
  - required-real PID/mount/net/cgroup composition test;
  - non-root/no-new-privs/capability verification;
  - PID-namespace PID 1 and mount namespace verification;
  - stale recorded-PID corruption test proving an unrelated host process survives cleanup;
  - natural-exit STALE classification and explicit recovery test.
- `src/pid_mount_lab.c`
  - restricted mount propagation or isolated `/proc` mount failures now return exit 77, matching the Phase 22 documented restricted-environment contract.
- Makefile
  - `test-stage3-hardening`
  - `phase28-gate`
  - authoritative `stage3-gate` that reruns real privileged tests for Phases 23–28, Phase 21/22 namespace tests, full regressions, sanitizers, and a final CLEAN-state check.

### Hardened Runtime Model

```text
host
 |
 +-- Python launcher (root only for orchestration)
       |
       +-- /shp27-launch cgroup
       |      cpu.max / memory.max / pids.max
       |
       +-- named network namespace + veth
       |
       +-- stopped isolation helper
              |
              +-- setns(network)
              +-- unshare(mount + pid-for-children)
              +-- private mount propagation
              +-- fork
                    |
                    +-- workload = PID 1 in child PID namespace
                        fresh /proc
                        non-root UID/GID
                        no_new_privs=1
                        effective capabilities=0
```

### Quality Gate

- Phase 27 required-real composed isolation: PASS
- hardened launcher/help validation: PASS
- final workload UID is non-root: PASS
- final workload `NoNewPrivs` equals 1: PASS
- final workload effective capability mask equals zero: PASS
- final workload network namespace matches named namespace and differs from host: PASS
- final workload PID namespace differs from host and namespace PID equals 1: PASS
- final workload mount namespace differs from host: PASS
- fresh `/proc` reflects the child PID namespace: PASS
- cgroup CPU/memory/PID limits remain correct: PASS
- stale recorded PID cannot cause an unrelated host process to be killed: PASS
- naturally exited workload is classified STALE and recovered safely: PASS
- cleanup remains idempotent and final `doctor` reports CLEAN: PASS
- `make phase28-gate`: PASS
- `make stage3-gate`: PASS
- `git diff --check`: PASS

An unprivileged skip is not Phase 28 or Stage 3 completion. The Fedora
`stage3-gate` must execute every required-real Stage 3 test and finish with the
launcher reporting `INSTANCE_STATE=CLEAN`.

### Stage 3 Closure Gate

Status: COMPLETE

Run:

```bash
make clean
make stage3-gate
git diff --check
git status --short
```

The authoritative Fedora closure gate passed with all required-real Phase 23-28
checks, the full regression suite, the sanitizer suite, and a final CLEAN-state
launcher check. Stage 3 is closed.

### Next Phase

Phase 29 — Unix domain sockets and the local observability/control IPC boundary.


## Phase 29 — Unix Domain Sockets and the Local IPC Boundary

Status: COMPLETE

### Concepts Learned

- `AF_UNIX` provides local inter-process communication without IP routing, ports, or a network stack hop.
- `SOCK_STREAM` still provides a byte stream, not message boundaries. One `send()` is not one application message; framing belongs above the transport and is intentionally deferred to Phase 30.
- Filesystem Unix sockets have pathname lifecycle concerns that TCP listeners do not: a crashed process can leave a stale socket inode behind.
- Startup must never blindly `unlink()` an arbitrary pre-existing pathname. A regular file at the configured socket path is evidence of a collision and must be preserved.
- A live Unix socket pathname and a stale Unix socket pathname are distinguished by connection behavior, not by `S_ISSOCK` alone.
- The socket inode's filesystem mode is part of the local trust boundary. Phase 29 uses `0600` so only the owning user can open the endpoint through normal DAC checks.
- Linux `SO_PEERCRED` lets the accepting process inspect the connected peer's PID, UID, and GID using kernel-provided credentials rather than trusting application payload fields.
- `SOCK_CLOEXEC` / `accept4(..., SOCK_CLOEXEC)` prevent unintended descriptor inheritance across `execve()`.
- Graceful server shutdown must close the listener and remove only the Unix socket inode it owns.

### Implementation

- `include/uds.h`
  - reusable Unix-domain stream API for listener creation, client connection, peer credential retrieval, and safe pathname cleanup.
- `src/uds.c`
  - validates `sockaddr_un.sun_path` length;
  - opens `AF_UNIX/SOCK_STREAM|SOCK_CLOEXEC` sockets;
  - refuses to delete non-socket path collisions;
  - probes an existing socket pathname and refuses a live listener;
  - removes only a stale socket inode whose connect probe returns a stale-listener condition;
  - binds, applies mode `0600`, listens, connects, accepts with `SOCK_CLOEXEC`, and exposes Linux `SO_PEERCRED`.
- `src/uds_server.c`
  - deliberately blocking Phase 29 lab server;
  - accepts sequential local clients and prints kernel peer credentials;
  - echoes the raw byte stream without defining application message boundaries;
  - handles partial sends and `EINTR`;
  - ignores `SIGPIPE` and exits cleanly on SIGINT/SIGTERM;
  - removes the owned socket pathname on shutdown.
- `tests/unit/test_uds.c`
  - exercises the reusable C listener/connect/accept helpers with a forked local peer;
  - verifies `0600`, socket file type, peer PID/UID/GID, bidirectional bytes, and cleanup.
- `tests/integration/test_uds_server.py`
  - help and invalid CLI checks;
  - overlong `sun_path` rejection;
  - preservation of a regular-file collision;
  - readiness based on a successful connection, not pathname existence alone;
  - raw stream echo across deliberately split writes;
  - sequential-client behavior;
  - live-listener duplicate-start rejection;
  - kernel peer UID/GID evidence;
  - stale-socket recovery;
  - SIGTERM cleanup and pathname removal.

### Phase Boundary

Phase 29 proves only the local transport and lifecycle contract. It intentionally
stays blocking and intentionally treats the payload as an unstructured byte
stream. It does not yet add length prefixes, message types, JSON/telemetry
schemas, proxy integration, queueing, or nonblocking delivery.

Those belong to:

```text
Phase 30 -> framed IPC protocol
Phase 31 -> nonblocking telemetry path
```

### Quality Gate

- strict C11 `-Wall -Wextra -Wpedantic -Werror` build: PASS in assistant validation
- C helper unit test: PASS in assistant validation
- Python UDS integration suite: PASS in assistant validation
- ASan/UBSan C helper + server/integration run: PASS in assistant validation
- filesystem socket mode is `0600`: PASS in automated test
- regular-file collision is preserved: PASS in automated test
- overlong Unix socket path is rejected: PASS in automated test
- live duplicate listener is rejected: PASS in automated test
- stale socket inode is recovered: PASS in automated test
- peer credentials come from `SO_PEERCRED`: PASS in automated test
- split writes round-trip as one raw byte stream: PASS in automated test
- graceful shutdown removes the socket pathname: PASS in automated test
- complete project `make test`: PASS
- complete project `make sanitize`: PASS
- `make phase29-gate`: PASS
- `git diff --check`: PASS in assistant validation

### Next Phase

Phase 30 — framed IPC: turn the Phase 29 byte stream into explicit application
messages with a bounded binary header and incremental parser state machine.

## Phase 30 — Framed IPC Protocol

Status: COMPLETE

### Concepts Learned

- `SOCK_STREAM` preserves byte order but not application message boundaries.
- Framing must tolerate arbitrary fragmentation and coalescing across `recv()` calls.
- Phase 30 uses a fixed 12-byte header: magic, version, type, flags, and big-endian payload length.
- The protocol caps payloads at 64 KiB before allocating or consuming payload data.
- Magic/version/type/flags are validated before payload processing.
- A decoder call may emit zero, one, or many complete frames.
- Zero-length frames are valid and are useful for heartbeats.
- Malformed input is a per-client protocol failure; the listening server must survive and accept later peers.
- Blocking framing is intentionally separate from Phase 31 nonblocking telemetry delivery.

### Wire Format

```text
bytes 0..3   magic       "SHP1"
byte  4      version     1
byte  5      type        1=event, 2=control, 3=heartbeat
bytes 6..7   flags       must be zero
bytes 8..11  payload_len unsigned 32-bit, network byte order
bytes 12..   payload     0..65536 bytes
```

### Implementation

- `include/ipc_frame.h`
  - protocol constants, frame types, decoder state, callback contract, header/send API.
- `src/ipc_frame.c`
  - network-byte-order header encoding/validation;
  - bounded incremental state machine for split/coalesced stream input;
  - `MSG_NOSIGNAL` send-all helper.
- `src/ipc_framed_server.c`
  - Phase 29 UDS lifecycle and peer credentials plus Phase 30 framing;
  - echoes complete frames only;
  - logs decoded type/length;
  - drops a malformed client without terminating the listener.
- `tests/unit/test_ipc_frame.c`
  - fragmented header/payload input;
  - multiple coalesced frames;
  - zero-length heartbeat;
  - invalid magic/version/type/flags;
  - oversized payload rejection;
  - encoded send helper wire check.
- `tests/integration/test_ipc_framed_server.py`
  - one-byte-at-a-time frame delivery;
  - multiple frames in one write;
  - 32 KiB payload across many writes;
  - oversized malformed client followed by a healthy client;
  - SIGTERM pathname cleanup.

### Quality Gate

- strict C11 build: PASS in assistant validation
- frame unit tests: PASS in assistant validation
- framed UDS integration tests: PASS in assistant validation
- ASan/UBSan frame unit + server tests: PASS in assistant validation
- complete project `make test`: PASS on Fedora
- complete project `make sanitize`: PASS on Fedora
- `make phase30-gate`: PENDING LOCAL FEDORA RUN
- `git diff --check`: PASS in assistant validation

### Next Phase

Phase 31 — nonblocking telemetry path: connect framed IPC to event-driven delivery
without allowing observability backpressure to stall proxy traffic.

## Phase 31 — Nonblocking Telemetry Path

Status: COMPLETE

### Concepts Learned

- Telemetry is auxiliary work and must never become proxy data-plane backpressure.
- Nonblocking `send()` may complete fully, partially, or fail with `EAGAIN`; all are normal states.
- `EPOLLOUT` is useful only while connection completion or queued output needs progress.
- A bounded userspace queue converts an unbounded slow-consumer problem into an explicit loss policy.
- Phase 31 uses drop-newest on queue overflow, preserving ordering of already accepted telemetry.
- Queue accounting tracks allocated frame bytes until a frame is completely sent, so the byte cap is a real memory bound even after partial writes.
- A missing collector causes immediate telemetry drops rather than application blocking or unbounded buffering.
- A disconnected/broken collector tears down only telemetry state and counts unsent queued frames as dropped.
- Phase 30 framing remains unchanged; Phase 31 changes delivery semantics, not the wire protocol.

### Implementation

- `include/telemetry_channel.h`
  - telemetry state machine (`DISCONNECTED`, `CONNECTING`, `CONNECTED`);
  - bounded queue/counter contract;
  - nonblocking connect, enqueue, flush, epoll-event and lifecycle API.
- `src/telemetry_channel.c`
  - nonblocking `AF_UNIX/SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK` connection;
  - `SO_ERROR` connect completion;
  - Phase 30 frame encoding before enqueue;
  - FIFO partial-write offsets;
  - `MSG_DONTWAIT|MSG_NOSIGNAL` writes;
  - `EAGAIN` accounting;
  - hard queue byte cap and drop-newest overflow policy;
  - queued-frame dropping on peer failure.
- `src/telemetry_nonblocking_lab.c`
  - epoll-driven producer lab;
  - configurable event count, payload size, queue cap, drain window, and socket send-buffer size;
  - prints generated/enqueued/sent/dropped/would-block/connect-failure/queued statistics and enqueue latency.
- `tests/unit/test_telemetry_channel.c`
  - disconnected immediate-drop behavior;
  - encoded frame flushing through `socketpair()`;
  - bounded queue/drop-newest policy;
  - peer-close isolation and queued-frame drop accounting.
- `tests/integration/test_nonblocking_telemetry.py`
  - missing collector must drop 20,000 events promptly;
  - intentionally stalled collector must cause `EAGAIN` plus bounded drops without blocking the producer;
  - healthy collector must decode all 200 Phase 30 frames with zero loss;
  - CLI validation.

### Phase Boundary

Phase 31 proves the nonblocking delivery primitive independently. It does not yet
assign request IDs, define the final telemetry schema, emit JSON, or integrate
OpenTelemetry. The reusable channel is ready to be attached to semantic proxy
events in subsequent phases.

### Quality Gate

- strict C11 build: PASS in assistant validation
- telemetry channel unit test: PASS in assistant validation
- missing collector nonblocking/drop test: PASS in assistant validation
- slow collector `EAGAIN` + hard queue cap test: PASS in assistant validation
- healthy collector zero-drop framed delivery: PASS in assistant validation
- ASan/UBSan unit + integration run: PASS in assistant validation
- `git diff --check`: PASS in assistant validation
- complete project `make test`: PASS ON FEDORA
- complete project `make sanitize`: PASS ON FEDORA
- `make phase31-gate`: PENDING LOCAL FEDORA RUN

### Next Phase

Phase 32 — request/trace IDs: introduce stable per-request correlation identity
so subsequent structured logs, metrics, and traces can describe the same work
across the proxy and observability pipeline.

## Phase 32 — Request / Trace IDs

Status: COMPLETE

### Concepts Learned

- A request ID is a local correlation key; a trace ID is the distributed correlation key carried across process/service boundaries.
- Phase 32 accepts W3C `traceparent` version `00` in canonical lowercase-hex form.
- All-zero trace IDs and parent/span IDs are invalid and are never propagated.
- A valid incoming trace ID is preserved while the proxy generates a new local span ID.
- Missing or malformed `traceparent` never aborts request processing; a fresh trace is generated and the source is recorded as absent, accepted, or rejected.
- Request IDs, trace IDs, and span IDs are generated with Linux `getrandom()` and retried if an all-zero value is ever produced.
- Retry/backend-attempt spans change per attempt, but request ID and trace ID stay stable for one logical request.
- The identifiers are carried inside Phase 30 EVENT frames sent through the Phase 31 nonblocking telemetry channel.

### Implementation

- `include/trace_context.h`: fixed-size ID types, source classification, request-start/retry/formatting API.
- `src/trace_context.c`: strict version-00 parser, lower-hex validation, nonzero checks, `getrandom()` generation, retry-span generation, canonical formatting.
- `src/trace_context_telemetry_lab.c`: simulates multiple backend attempts and emits correlated Phase 30 EVENT frames through Phase 31.
- `tests/unit/test_trace_context.c`: valid, invalid, generated, and retry invariants.
- `tests/integration/test_trace_context_telemetry.py`: end-to-end framed propagation, remote trace preservation, malformed fallback, stable trace/request IDs and unique attempt spans.

### Phase Boundary

Phase 32 establishes correlation identity and propagation only. It does not yet define final JSON logging, export spans, or implement OpenTelemetry. Phase 33 will define structured JSON logs; Phase 36 will export traces.

### Quality Gate

- strict C11 build: PASS in assistant validation
- trace context unit tests: PASS in assistant validation
- generated identity propagation: PASS in assistant validation
- valid W3C traceparent preservation: PASS in assistant validation
- malformed/zero traceparent safe fallback: PASS in assistant validation
- request/trace stability across retries: PASS in assistant validation
- unique attempt span IDs: PASS in assistant validation
- Phase 30 framed telemetry propagation: PASS in assistant validation
- ASan/UBSan unit + integration run: PASS in assistant validation
- complete project `make test`: PASS ON FEDORA
- complete project `make sanitize`: PASS ON FEDORA
- `make phase32-gate`: PENDING LOCAL FEDORA RUN

### Next Phase

Phase 33 — structured JSON logs: define a stable event schema carrying the Phase 32 correlation identifiers without blocking the proxy fast path.

## Phase 33 — Structured JSON Logs

Status: COMPLETE

### Concepts Learned

- Structured logs are a machine contract, not formatted prose; every telemetry event is one complete JSON object with stable field names and types.
- Phase 33 defines schema version `shp.telemetry.v1` so later consumers can detect incompatible event-shape changes explicitly.
- Request, trace, and span identifiers from Phase 32 are first-class JSON fields rather than embedded text tokens.
- `timestamp_unix_ns` is an integer wall-clock timestamp suitable for ordering/correlation across processes; duration measurement still belongs on monotonic clocks.
- Untrusted strings must be JSON-escaped before serialization. Quotes, backslashes, newlines, tabs, and control bytes cannot be allowed to inject a second log line or corrupt the document.
- Serialization is bounded. Oversized fields fail before a Phase 30 frame is queued, so no partial JSON event can escape.
- JSON formatting changes event representation only; delivery still uses Phase 30 framing over the Phase 31 bounded nonblocking telemetry channel.

### Schema

Each Phase 33 event contains:

- `schema`: `shp.telemetry.v1`
- `timestamp_unix_ns`: integer Unix timestamp in nanoseconds
- `event`: stable event name
- `request_id`: 32 lowercase hex characters
- `trace_id`: 32 lowercase hex characters
- `span_id`: 16 lowercase hex characters
- `trace_source`: `generated`, `remote`, or `rejected`
- `attempt`: positive integer
- `backend`: escaped string
- `result`: escaped string
- `message`: escaped string

### Implementation

- `include/json_log.h`: schema limits, event input structure, bounded serializer and escaping API.
- `src/json_log.c`: fixed-buffer JSON writer, RFC 8259 string escaping, schema/correlation serialization, field-length checks, realtime nanosecond timestamp helper.
- `src/json_log_telemetry_lab.c`: emits retry-correlated JSON EVENT frames through the Phase 31 telemetry channel.
- `tests/unit/test_json_log.c`: escaping, schema/correlation fields, output-buffer exhaustion, oversized-field rejection and invalid-input behavior.
- `tests/integration/test_json_log_telemetry.py`: parses real Phase 30 payloads with Python `json`, verifies stable request/trace IDs and unique attempt spans, hostile-string escaping, remote-trace preservation, and no partial emission on serialization failure.

### Phase Boundary

Phase 33 defines and validates the structured event representation. It does not yet run an OpenTelemetry Collector, export OTLP, define production metrics, or emit completed distributed traces. Phase 34 introduces the OpenTelemetry Collector boundary.

### Quality Gate

- strict C11 build: PASS in assistant validation
- JSON serializer unit tests: PASS in assistant validation
- valid JSON integration decoding: PASS in assistant validation
- hostile-string/log-injection resistance: PASS in assistant validation
- stable request/trace correlation across attempts: PASS in assistant validation
- unique span IDs per attempt: PASS in assistant validation
- remote trace preservation: PASS in assistant validation
- oversized-field no-partial-frame behavior: PASS in assistant validation
- ASan/UBSan unit + integration run: PASS in assistant validation
- complete project `make test`: PASS ON FEDORA
- complete project `make sanitize`: PASS ON FEDORA
- `make phase33-gate`: PENDING LOCAL FEDORA RUN

### Next Phase

Phase 34 — OpenTelemetry Collector: introduce an external collector process and a clean export boundary without making the proxy depend on collector availability.

## Phase 34 — OpenTelemetry Collector Integration

Status: COMPLETE

### Concepts Learned

- The OpenTelemetry Collector is a separate observability process and must not become a synchronous dependency of the proxy data plane.
- The existing Phase 29–33 protocol remains the local producer contract: Unix socket + SHP1 frame + bounded nonblocking channel + correlated structured JSON.
- A Python bridge is an explicit protocol boundary: it validates `shp.telemetry.v1`, maps it to OTLP log records, and owns the potentially blocking HTTP export operation.
- OTLP/HTTP logs use `POST /v1/logs` and may use Protobuf JSON encoding with `Content-Type: application/json`.
- OTLP JSON represents trace/span identifiers as hexadecimal strings and 64-bit timestamp fields such as `timeUnixNano` as decimal strings.
- Trace and span correlation become native OTLP `LogRecord` fields instead of only custom JSON keys.
- Resource metadata such as `service.name=self-healing-proxy` identifies the telemetry-producing service independently from individual log attributes.
- Collector/backend failure is isolated from the producer: HTTP 503/export failure is recorded by the bridge while the Phase 31 producer can still complete without waiting on an external backend.
- The real Phase 34 gate uses an actual OpenTelemetry Collector container rather than treating a fake HTTP receiver as sufficient proof.

### Implementation

- `control-plane/otel_bridge.py`: validates Phase 33 framed JSON EVENTs, converts them to OTLP/HTTP JSON log requests, exports with a bounded per-request timeout, reports export counters, and cleans up its Unix socket.
- `observability/otel-collector-config.yaml`: OTLP HTTP receiver, batch processor, logs pipeline, and detailed debug exporter.
- `scripts/otel_collector_lab.sh`: Podman/Docker lifecycle wrapper with a pinned Collector image, deterministic port/config overrides, explicit image preparation, readiness probe, logs, and cleanup.
- `tests/unit/test_otel_bridge.py`: validates Phase 33 schema handling and OTLP mapping, including native trace/span fields, decimal-string timestamps, attributes, endpoint normalization, and severity mapping.
- `tests/integration/test_otel_collector_bridge.py`: end-to-end Phase 33 producer -> SHP frame -> bridge -> OTLP/HTTP tests, backend failure isolation, and an opt-in/required real Collector test.
- `Makefile`: bridge checks/tests, Collector lifecycle helpers, `phase34-real`, and full `phase34-gate`.

### Phase Boundary

Phase 34 proves Collector integration for logs only. The bridge is intentionally a separate process and the Collector uses the debug exporter as observable proof. Phase 35 adds production metrics semantics; Phase 36 adds completed trace/span export. Backend selection, long-term storage, Grafana dashboards, and telemetry-overhead characterization remain later phases.

### Quality Gate

- Python syntax/bytecode validation: PASS in assistant validation
- OTLP JSON mapping unit tests: PASS in assistant validation
- native OTLP trace/span correlation fields: PASS in assistant validation
- Phase 33 event schema validation: PASS in assistant validation
- fake OTLP/HTTP successful export: PASS in assistant validation
- HTTP 503 export failure isolation: PASS in assistant validation
- Phase 31 producer completes despite OTLP backend failure: PASS in assistant validation
- real OpenTelemetry Collector acceptance: PASS on Fedora
- complete project `make test`: PASS on Fedora
- complete project `make sanitize`: PASS on Fedora
- `make phase34-gate`: PASS on Fedora

### Next Phase

Phase 35 — Metrics: define bounded counters/gauges/histograms for request volume, latency, backend health, retries, drops, and resource behavior, then export them through OpenTelemetry.


## Phase 35 — Metrics

Status: COMPLETE

### Concepts Learned

- Metrics are aggregated state, not per-request event logs. The bridge derives cumulative OTLP metric streams from the validated Phase 33 event stream without changing the proxy-side nonblocking contract.
- Monotonic counters use cumulative OTLP Sum points with a stable `startTimeUnixNano`; collector gaps therefore do not require the producer to replay every individual event.
- `shp.requests` counts logical requests at attempt 1; `shp.retries` counts attempts after the first; backend-attempt and completion counters provide bounded operational dimensions.
- Result labels are normalized to a fixed set (`success`, `retry`, `error`, `other`) instead of copying arbitrary strings into metric dimensions.
- Backend label cardinality is explicitly bounded. The bridge tracks at most a configured number of backend label values and folds additional values into `__other__`.
- Metrics export uses OTLP/HTTP `/v1/metrics` through the same external Collector boundary; it remains outside the proxy correctness path.
- Metrics-export failure is counted and reported but, unless explicitly required by a test gate, does not make successful log ingestion or proxy-side telemetry production fail.
- Phase 35 intentionally does not invent request-latency histograms because Phase 33 events do not yet carry a trustworthy monotonic request-duration measurement. A metric should represent a real measurement, not a synthetic placeholder.

### Metrics

- `shp.requests`: cumulative monotonic count of logical requests.
- `shp.retries`: cumulative monotonic count of attempts after attempt 1.
- `shp.backend.attempts`: cumulative monotonic count labeled by bounded `backend` and normalized `result`.
- `shp.requests.completed`: cumulative monotonic count labeled by terminal `outcome`.
- `shp.telemetry.log_export_failures`: cumulative bridge log-export failures.
- `shp.telemetry.invalid_events`: cumulative invalid SHP/Phase 33 input count.
- `shp.metrics.tracked_backends`: gauge showing the number of backend label values retained before overflow folding.

### Implementation

- `control-plane/otel_bridge.py`: adds bounded metric aggregation, OTLP Metrics JSON generation, `/v1/metrics` export, periodic cumulative snapshots, and metrics-export counters while preserving the Phase 34 log-export API by default.
- `observability/otel-collector-config.yaml`: adds a native metrics pipeline to the existing OTLP receiver / batch / debug-exporter configuration.
- `tests/unit/test_otel_metrics.py`: validates counter semantics, cumulative temporality, timestamp windows, result normalization, backend-label cardinality bounding, and metrics endpoint derivation.
- `tests/integration/test_otel_metrics.py`: validates real framed events -> bridge -> OTLP logs + OTLP metrics, metrics failure isolation, and opt-in required acceptance by a real OpenTelemetry Collector.
- `Makefile`: adds `check-otel-metrics`, `test-otel-metrics`, `phase35-real`, and `phase35-gate`.

### Phase Boundary

Phase 35 defines and exports operational counters/gauges that can be derived faithfully from the current event stream. It does not yet fabricate latency histograms or full distributed spans. Phase 36 introduces completed trace/span export; later phases add dashboards and overhead characterization.

### Quality Gate

- OTLP metrics unit tests: PASS in assistant validation
- cumulative Sum semantics: PASS in assistant validation
- backend label cardinality cap: PASS in assistant validation
- result-label normalization: PASS in assistant validation
- fake OTLP `/v1/metrics` export: PASS in assistant validation
- metrics-export failure isolation: PASS in assistant validation
- Phase 34 log-export regression compatibility: PASS in assistant validation
- real OpenTelemetry Collector metrics acceptance: PASS on Fedora
- complete project `make test`: PASS on Fedora
- complete project `make sanitize`: PASS on Fedora
- `make phase35-gate`: PASS on Fedora

### Next Phase

Phase 36 — Traces: export completed request/backend-attempt spans with correct parentage and retry structure using the Phase 32 correlation context.

## Phase 36 — Traces

Status: COMPLETE

### Concepts Learned

- Correlation IDs become real trace structure only when exported as OTLP spans with explicit parent/child relationships and start/end timestamps.
- The logical proxy request is represented as one SERVER span. Each backend attempt is a CLIENT child span with the same trace ID and a unique span ID.
- A valid incoming W3C `traceparent` becomes the parent of the local request span; locally generated traces have no parent span.
- Retry attempts are separate child spans, not separate traces. Failed/retried attempts carry ERROR status while the terminal successful attempt carries OK status.
- Duration is measured with `CLOCK_MONOTONIC`; wall-clock OTLP timestamps are derived from one `CLOCK_REALTIME` + `CLOCK_MONOTONIC` anchor pair so NTP/time adjustments do not corrupt duration measurement.
- Trace export uses OTLP/HTTP `/v1/traces` through the same external bridge boundary and remains isolated from the proxy's request-serving correctness path.
- A trace-export HTTP failure is observable but non-fatal by default; only the explicit quality-gate option requires successful export.

### Implementation

- `src/trace_span_telemetry_lab.c`: creates one completed logical request trace, measures backend-attempt durations with `CLOCK_MONOTONIC`, preserves remote parentage, and emits one bounded `shp.trace.v1` batch through the Phase 31 telemetry channel.
- `control-plane/otel_bridge.py`: validates `shp.trace.v1`, maps request/attempt spans to native OTLP traces, derives `/v1/traces`, exports trace batches, and records trace-export counters without regressing Phase 34 logs or Phase 35 metrics.
- `observability/otel-collector-config.yaml`: adds a native traces pipeline alongside logs and metrics.
- `tests/unit/test_otel_traces.py`: validates OTLP span kind/status mapping, trace ID continuity, remote-parent propagation, parentage rules, timing validation, and traces-endpoint derivation.
- `tests/integration/test_otel_traces.py`: validates C lab -> SHP frame -> bridge -> OTLP traces, retry hierarchy, remote W3C parentage, trace-export failure isolation, socket cleanup, and real Collector acceptance.
- `Makefile`: adds `trace_span_telemetry_lab`, `check-otel-traces`, `test-otel-traces`, `sanitize-otel-traces`, `phase36-real`, and `phase36-gate`.

### Phase Boundary

Phase 36 proves native trace/span export and retry parentage. It does not add dashboards or trace storage/query UI; Phase 37 owns Grafana visualization. Phase 38 later measures telemetry overhead and failure behavior.

### Quality Gate

- strict C trace-lab build: PASS in assistant validation
- trace mapping unit tests: PASS in assistant validation
- request SERVER span + backend CLIENT child spans: PASS in assistant validation
- same trace ID / unique span IDs: PASS in assistant validation
- retry/error + terminal success status mapping: PASS in assistant validation
- remote W3C parent propagation: PASS in assistant validation
- monotonic duration + derived wall timestamp validation: PASS in assistant validation
- fake `/v1/traces` export: PASS in assistant validation
- trace HTTP 503 failure isolation: PASS in assistant validation
- ASan/UBSan trace-lab integration: PASS in assistant validation
- Phase 34/35 bridge regressions: PASS in assistant validation
- real OpenTelemetry Collector trace acceptance: PASS on Fedora
- complete project `make test`: PASS on Fedora
- complete project `make sanitize`: PASS on Fedora
- `make phase36-gate`: PASS on Fedora

### Next Phase

Phase 37 — Grafana: visualize logs, metrics, and traces with an operator-facing dashboard while preserving signal correlation.


## Phase 37 — Grafana

Status: COMPLETE

### Concepts Learned

- Grafana is the operator-facing query/visualization layer; it does not belong in the proxy request-serving correctness path.
- Phase 37 uses Grafana's pinned `grafana/otel-lgtm` development image to provide Grafana, Prometheus, Loki, Tempo, and an OTLP Collector in one reproducible lab container.
- Existing OTLP logs, metrics, and traces are sent unchanged to the LGTM OTLP/HTTP receiver; Phase 37 does not invent a fourth telemetry protocol.
- Grafana data sources are treated as code and verified by stable UIDs: `prometheus`, `loki`, and `tempo`.
- The dashboard scopes logs/traces to `service.name=self-healing-proxy` and avoids request/trace/span IDs as metric dimensions.
- Cross-signal correlation remains identity-based: logs retain trace IDs, Tempo stores the native trace tree, and Prometheus holds bounded aggregates.
- A dashboard being visually attractive is not enough; the gate queries all three real backends through Grafana's data-source proxy and proves telemetry is actually searchable.
- The `grafana/otel-lgtm` image is intentionally a development/demo/test backend rather than a production deployment model.

### Implementation

- `scripts/grafana_lgtm_lab.sh`: Podman/Docker lifecycle wrapper around pinned `docker.io/grafana/otel-lgtm:0.36.0`, exposing deterministic localhost ports and mounting the project dashboard as code.
- `observability/grafana/provisioning/dashboards/shp.yaml`: file-based Grafana dashboard provider.
- `observability/grafana/dashboards/self-healing-proxy-overview.json`: provisioned overview with request/retry stats, backend-attempt metrics, correlated Loki logs, Tempo trace search, and an operator correlation note.
- `tests/unit/test_grafana_dashboard.py`: validates dashboard identity, panel/data-source coverage, bounded metric-query dimensions, service scoping, and pinned lab image.
- `tests/integration/test_grafana_lgtm.py`: starts the real LGTM container when required, verifies Grafana/data sources/dashboard, emits Phase 33 logs+metrics and Phase 36 traces, and queries Prometheus/Loki/Tempo through Grafana.
- `Makefile`: adds lifecycle helpers, fast validation, `phase37-real`, and `phase37-gate`.

### Phase Boundary

Phase 37 proves that the three native observability signals are queryable and correlated in an operator-facing Grafana environment. It does not yet benchmark telemetry cost or deliberately overload/kill observability components; Phase 38 owns telemetry overhead and failure characterization.

### Quality Gate

- dashboard JSON/provisioning validation: PASS in assistant validation
- pinned LGTM lifecycle shell syntax: PASS in assistant validation
- dashboard unit tests: PASS in assistant validation
- default integration test skip behavior: PASS in assistant validation
- real Grafana health/data sources/dashboard: PASS on Fedora
- real Prometheus query through Grafana: PASS on Fedora
- real Loki query through Grafana: PASS on Fedora
- real Tempo query through Grafana: PASS on Fedora
- complete project `make test`: PASS ON FEDORA
- complete project `make sanitize`: PASS ON FEDORA
- `make phase37-gate`: PASS as part of Stage 4 closure on Fedora

### Next Phase

Phase 38 — Telemetry overhead and failure behavior: quantify latency/CPU/memory impact, force slow/dead observability components, and close Stage 4 with explicit failure-budget evidence.

## Stage 4 - Observability

Status: COMPLETE

Stage 4 gate passed on Fedora after Phase 38: nonblocking IPC, correlation, structured logs, OpenTelemetry logs/metrics/traces, Grafana, and measured failure-isolated observability are complete.

## Phase 38 - Telemetry Overhead & Failure Closure

Status: COMPLETE

- latency/CPU/memory budgets: PASS on Fedora
- missing collector bounded fast failure: PASS on Fedora
- stalled collector EAGAIN + bounded queue + drops: PASS on Fedora
- real cross-signal Grafana proof: PASS on Fedora
- project regressions and sanitizers inside the phase gate: PASS on Fedora
- `make phase38-gate`: PASS on Fedora
- Stage 4 gate: PASS on Fedora


## Phase 39 - Health State Model

Status: COMPLETE

### Concepts Learned

- Self-healing must begin with an explicit state model rather than scattered booleans such as `is_down` or ad-hoc counters.
- A single backend failure is weak evidence. `SUSPECT` provides hysteresis so transient failures do not immediately eject a backend.
- `UNHEALTHY` and `RECOVERING` are intentionally non-eligible states: cooldown plus multiple recovery successes prevents rapid traffic flapping.
- Recovery is asymmetric with failure: a backend can become suspicious quickly, but it must demonstrate sustained health before being trusted again.
- Time is injected as monotonic nanoseconds. The state machine never sleeps and rejects time moving backward, making transitions deterministic and testable.
- Health classification is separated from probing and traffic-routing policy. Later phases consume this model instead of embedding probe/network logic inside it.

### Implementation

- `include/backend_health.h`: state/config/runtime contract for `UNKNOWN`, `HEALTHY`, `SUSPECT`, `UNHEALTHY`, and `RECOVERING`.
- `src/backend_health.c`: threshold, cooldown, recovery-hysteresis, monotonic-time validation, counters, transition tracking, and eligibility semantics.
- `tests/unit/test_backend_health.c`: deterministic state-transition, cooldown, hysteresis, invalid-config, and monotonic-time tests.
- `src/health_state_lab.c`: simulated transient-failure and trip/recovery scenarios without wall-clock sleeps.
- `tests/integration/test_health_state_lab.py`: validates CLI behavior and complete transition sequences.
- `Makefile`: adds build/test/sanitizer/Phase 39 gate targets and integrates the model into project-wide regressions.

### Phase Boundary

Phase 39 models health only. It does not perform active probes, remove backends from the existing selector, schedule probe timers, or implement automatic restart/remediation. Those are later Stage 5 responsibilities.

### Quality Gate

- strict C11 build: PASS on Fedora
- state transition unit tests: PASS on Fedora
- transient-failure hysteresis: PASS on Fedora
- failure threshold trip: PASS on Fedora
- cooldown enforcement: PASS on Fedora
- multi-success recovery hysteresis: PASS on Fedora
- monotonic-time rejection: PASS on Fedora
- integration state-sequence lab: PASS on Fedora
- ASan/UBSan: PASS on Fedora
- complete project `make test`: PASS inside Phase 39 gate
- complete project `make sanitize`: PASS inside Phase 39 gate
- `make phase39-gate`: PASS on Fedora

### Next Phase

Phase 40 - Active health probing: schedule bounded nonblocking probes and feed probe results into the Phase 39 health state machine.


## Phase 40 - Active Health Probing

Status: COMPLETE

### Concepts Learned

- Active health checks are evidence producers; they do not own routing policy. Probe outcomes feed the Phase 39 state model.
- TCP health probes must never use a blocking `connect()`. The probe socket is nonblocking, `EINPROGRESS` means "pending", and completion is verified with writability plus `SO_ERROR`.
- Every probe has its own monotonic deadline. A backend that never completes a handshake becomes a bounded `TIMEOUT`, not an indefinitely stuck control-plane operation.
- Probe scheduling is monotonic and interval-based. A completed result must be consumed before the next probe starts, preventing overlapping probes for one backend in this phase.
- Connection refusal, timeout, and other socket errors are distinct probe outcomes, but all failures feed the same health evidence API.
- Recovery remains asymmetric: after failures trip `UNHEALTHY`, cooldown must move the backend into `RECOVERING`, and multiple successful probes are required before `HEALTHY`.

### Implementation

- `include/health_probe.h`: active-probe state, configuration, result taxonomy, counters, scheduling, and result-consumption API.
- `src/health_probe.c`: IPv4 nonblocking TCP connect, `EINPROGRESS`, zero-time `poll()`, `SO_ERROR` completion, explicit deadlines, error classification, and cleanup.
- `src/active_health_probe_lab.c`: monotonic scheduler that feeds completed probe results into `backend_health_t`.
- `tests/unit/test_health_probe.c`: configuration, due scheduling, result consumption, deterministic deadline timeout, and result-name tests.
- `tests/integration/test_active_health_probe.py`: real loopback healthy/refused/recovery scenarios using a disposable TCP accept server.
- `Makefile`: Phase 40 build/test/sanitizer/gate targets plus project regression integration.

### Phase Boundary

Phase 40 observes backend reachability and updates health state only. It does not yet modify the production round-robin selector, eject backends from live traffic, perform HTTP application-level health requests, restart processes, or execute remediation actions.

### Quality Gate

- strict C11 health-probe build: PASS ON FEDORA
- active-probe unit tests: PASS ON FEDORA
- healthy loopback probe success: PASS ON FEDORA
- refused-port classification: PASS ON FEDORA
- deterministic timeout/deadline behavior: PASS ON FEDORA
- UNHEALTHY -> RECOVERING -> HEALTHY probe recovery: PASS ON FEDORA
- ASan/UBSan: PASS ON FEDORA
- complete project `make test`: PASS ON FEDORA
- complete project `make sanitize`: PASS ON FEDORA
- `make phase40-gate`: PASS ON FEDORA

### Next Phase

Phase 41 - Passive health evidence: combine real request-path failures/successes with active-probe evidence without double-counting or destabilizing routing.

## Phase 41 - Passive Health Evidence

**Status:** COMPLETE

### Goal
Translate request-path backend outcomes into carefully classified health evidence without allowing client-caused or proxy-local failures to poison backend state.

### Implemented
- `include/passive_health.h`
- `src/passive_health.c`
- `src/passive_health_lab.c`
- `tests/unit/test_passive_health.c`
- `tests/integration/test_passive_health_lab.py`
- strict SUCCESS / FAILURE / NEUTRAL evidence policy
- completed upstream response -> SUCCESS
- backend transport/connect/response failures -> FAILURE
- client disconnect / invalid request / local overload / local I/O -> NEUTRAL
- raw HTTP status -> NEUTRAL at transport-health layer
- per-attempt single-finalization guard (`EALREADY` on duplicate evidence)
- integration with the Phase 39 `backend_health_t` state machine
- deterministic trip/cooldown/recovery scenarios
- targeted ASan/UBSan coverage

### Phase boundary
Phase 41 classifies and applies passive evidence only. It does not yet modify production backend selection, merge active/passive confidence policy, eject backends from routing, or perform remediation.

### Local quality gate pending
Run `make phase41-gate` on Fedora. Phase completion requires the exact marker:

`Phase 41 gate passed: request-path passive health classification, single-count attempt evidence, client/local neutrality, hysteretic recovery, regressions, and sanitizers completed.`

## Phase 42 - Health-Aware Backend Selection

**Status:** COMPLETE

### Goal
Make backend selection consume the Phase 39 health eligibility contract while preserving the existing round-robin, admission-control, and retry-exclusion semantics.

### Implemented
- `include/health_aware_pool.h`: selector wrapper contract binding one `backend_health_t` to each existing backend target.
- `src/health_aware_pool.c`: round-robin scan that skips non-eligible health states and saturated targets, preserves the existing cursor semantics, and supports retry exclusion.
- `src/health_aware_selection_lab.c`: deterministic skip/all-unavailable/rejoin scenarios.
- `tests/unit/test_health_aware_pool.c`: eligibility, fairness, capacity, exclusion, no-backend, and recovered-rejoin tests.
- `tests/integration/test_health_aware_selection.py`: validates complete selection sequences and fail-closed behavior.
- `Makefile`: Phase 42 build/test/sanitizer/gate targets and project-wide regression integration.

### Routing policy
- `UNKNOWN`: selectable
- `HEALTHY`: selectable
- `SUSPECT`: selectable
- `UNHEALTHY`: skipped
- `RECOVERING`: skipped
- all backends skipped/saturated/excluded: return no target; do not silently route to an unhealthy backend

### Phase boundary
Phase 42 changes health-aware selection policy only. It does not restart processes, create replacement backends, add exponential backoff, or execute remediation.

### Local quality gate pending
Run `make phase42-gate` on Fedora. Completion requires the exact marker:

`Phase 42 gate passed: health-aware round robin skips non-eligible backends, preserves capacity/fairness, fails closed when none are eligible, reintegrates recovered backends, regressions, and sanitizers completed.`

### Next Phase
Phase 43 - Failure-window and circuit-breaker policy: turn repeated backend failures into bounded temporary isolation with explicit reopen behavior.

## Phase 43 - Circuit Breaker / Failure Window

Status: COMPLETE

Implemented:
- fixed-memory rolling failure window per backend
- CLOSED / OPEN / HALF_OPEN circuit states
- monotonic open-duration timing
- immediate rejection while OPEN
- exactly one in-flight HALF_OPEN trial
- successful trial closes and clears stale failure evidence
- failed HALF_OPEN trial reopens immediately
- circuit-aware round robin that composes Phase 39 health, Stage 2 capacity, retry exclusion, and circuit permission
- deterministic unit/integration tests with no sleeps
- ASan/UBSan target and full phase gate

Quality gate: PASS ON FEDORA (`make phase43-gate`).

Next: Phase 44 - bounded remediation policy / restart decision model.


## Phase 44 - Bounded Remediation Policy

**Status:** COMPLETE

### Goal
Decide when a backend is eligible for remediation without executing destructive actions prematurely or repeatedly.

### Implemented
- `include/remediation_policy.h`: decision/action/reason taxonomy and fixed-memory attempt history.
- `src/remediation_policy.c`: strong-evidence gate, capped exponential backoff, rolling attempt-window budget, recovery reset, backward-time rejection.
- `src/remediation_policy_lab.c`: deterministic evidence/backoff/budget/recovery scenarios.
- unit and integration coverage plus ASan/UBSan targets.

### Default policy
- require backend health `UNHEALTHY`
- require circuit state `OPEN`
- initial backoff: 10 simulated ns
- maximum backoff: 40 simulated ns
- attempt window: 100 simulated ns
- maximum remediation attempts per window: 3
- successful recovery resets the exponential backoff level but does not erase still-recent attempts from the rolling safety budget

### Phase boundary
Phase 44 returns a policy decision such as `RESTART_BACKEND`; it does not kill, signal, respawn, replace, or otherwise mutate a backend process.

### Quality gate
PASS ON FEDORA (`make phase44-gate`).

### Next Phase
Phase 45 - Remediation executor: safely turn an approved remediation decision into a supervised backend restart with identity checks and bounded lifecycle control.


## Phase 45 - Remediation Executor

**Status:** COMPLETE

### Goal
Safely execute one already-approved backend restart against the exact intended Linux process, with bounded termination and verified replacement startup.

### Implemented
- `include/remediation_executor.h`: process identity, executor configuration, statuses, and result contract.
- `src/remediation_executor.c`: `/proc/<pid>/stat` start-time identity capture, pidfd acquisition, pidfd signaling, bounded poll-based termination, SIGTERM-to-SIGKILL escalation, fork/exec replacement with exec-failure handshake, and new identity capture.
- `src/remediation_executor_lab.c`: graceful, forced-escalation, stale-identity, and spawn-failure scenarios using real child processes.
- `tests/unit/test_remediation_executor.c`: identity and validation contract coverage.
- `tests/integration/test_remediation_executor.py`: real process lifecycle assertions.
- strict C11 plus ASan/UBSan targeted coverage.

### Safety contract
- Phase 44 decides **whether** to restart; Phase 45 only executes an approved restart.
- Raw PID alone is insufficient identity. PID plus process start time must match.
- A pidfd is acquired before signaling to avoid PID-reuse races during the termination operation.
- SIGTERM is attempted first.
- SIGKILL is used only after the bounded graceful timeout.
- Replacement exec failure is synchronously observable; it is never silently reported as a successful restart.

### Phase boundary
Phase 45 restarts a single process command. It does not yet reconstruct namespace/cgroup isolation, manage dependency graphs, implement long-running supervision, or merge restart results back into all health/circuit state.

### Quality gate
PASS ON FEDORA (`make phase45-gate`).

### Next Phase
Phase 46 - supervised recovery orchestration: connect restart results back to health/circuit state and require post-restart readiness before normal traffic returns.


## Phase 46 - Supervised Recovery Orchestration

**Status:** COMPLETE

### Goal
Keep a restarted backend quarantined until a bounded supervisor observes real TCP readiness, the Phase 39 recovery hysteresis reaches `HEALTHY`, and the Phase 43 circuit safely returns to `CLOSED`.

### Implemented
- `include/recovery_orchestrator.h`: quarantine/ready/failed state contract, startup deadline, readiness counters, and explicit failure reasons.
- `src/recovery_orchestrator.c`: deterministic monotonic recovery state machine that consumes Phase 45 executor results, advances existing health/circuit state, and fails closed on startup deadline expiry.
- `src/recovery_orchestrator_lab.c`: real restart + delayed TCP-listener worker scenarios proving quarantine, eventual readiness, startup timeout, and failed-restart containment.
- `tests/unit/test_recovery_orchestrator.c`: deterministic state/timing tests including backward-time rejection and neutrality of startup probe failures.
- `tests/integration/test_recovery_orchestrator.py`: real process and TCP readiness orchestration assertions.
- `Makefile`: Phase 46 targeted build/test/sanitizer/gate integration plus inclusion in the normal regression suite.

### Recovery contract
- `exec()` success is necessary but not sufficient for traffic.
- Replacement begins `QUARANTINED` and `can_route=0`.
- Failed readiness probes during startup mean "not ready yet"; they reset the consecutive readiness streak but do not create fresh production-health failures.
- Successful readiness probes feed the existing Phase 39 `RECOVERING -> HEALTHY` hysteresis.
- A Phase 43 `HALF_OPEN` circuit receives one controlled readiness success and closes normally.
- The backend becomes `READY` only when the readiness threshold is satisfied, health is `HEALTHY`, and circuit state is `CLOSED`.
- Startup deadline expiry yields `FAILED/STARTUP_TIMEOUT` and remains unroutable.
- A failed Phase 45 restart yields `FAILED/RESTART_NOT_COMPLETED` and never enters quarantine routing.

### Phase boundary
Phase 46 supervises one replacement through readiness and reintegration. It does not coordinate many simultaneous backend recoveries, reconstruct namespace/cgroup isolation, or impose a fleet-wide restart-storm budget.

### Quality gate
PASS ON FEDORA (`make phase46-gate`).

### Next Phase
Phase 47 - fleet-wide healing coordination and restart-storm protection.


## Phase 47 - Fleet-Wide Healing Coordination and Restart-Storm Protection

**Status:** IN PROGRESS - AWAITING LOCAL FEDORA QUALITY GATE

### Goal
Coordinate remediation admission across the entire backend fleet so correlated failures cannot trigger an unbounded restart storm or allow one noisy backend to monopolize healing capacity.

### Implemented
- `include/healing_coordinator.h`: fixed-size fleet coordinator contract, admission reasons, and global safety counters.
- `src/healing_coordinator.c`: duplicate coalescing, monotonic-time validation, rolling fleet attempt budget, global concurrency cap, and round-robin pending admission.
- `src/healing_coordinator_lab.c`: deterministic concurrency, fairness, and rolling-budget scenarios.
- `tests/unit/test_healing_coordinator.c`: state, duplicate, concurrency, budget-aging, fairness, invalid-completion, and backward-time assertions.
- `tests/integration/test_healing_coordinator.py`: CLI-level fleet behavior checks.
- `Makefile`: normal/sanitized Phase 47 targets and full regression inclusion.

### Coordination contract
- A backend may be pending or in progress, never multiplied into several concurrent healing requests.
- Global `max_concurrent` bounds simultaneous remediation executions across the fleet.
- Global `max_attempts_per_window` bounds admitted remediation starts even if prior attempts finish quickly.
- Denied requests remain pending; admission is retried when concurrency or budget becomes available.
- Only admitted attempts consume the rolling budget.
- A rotating `next_index` preserves fairness among pending backends.
- Timing is deterministic and monotonic; no sleeps are embedded in the policy object.

### Phase boundary
Phase 47 coordinates *who may heal now*. It does not decide whether a backend deserves remediation (Phase 44), terminate/spawn processes (Phase 45), or supervise readiness (Phase 46). Namespace/cgroup reconstruction and final Stage 5 closure remain later phases.

### Local quality gate pending
Run `make phase47-gate` on Fedora. Completion requires the exact marker:

`Phase 47 gate passed: fleet-wide healing enforces global concurrency and rolling restart budgets, preserves fair remediation admission, coalesces duplicate requests, regressions, and sanitizers completed.`

### Next Phase
Phase 48 - self-healing observability, end-to-end fault injection, and Stage 5 closure.
