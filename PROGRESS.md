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

