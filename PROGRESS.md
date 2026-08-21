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

