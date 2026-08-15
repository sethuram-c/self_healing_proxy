# Project Progress

Total planned phases: 60

## Phase 01 — Environment & Git Foundation

Status: IN PROGRESS

### Acceptance Criteria

- [ ] Fedora development tools verified
- [ ] Repository structure created
- [ ] Strict C compilation successful
- [ ] Makefile build successful
- [ ] Automated smoke test passed
- [ ] Sanitizer build passed
- [ ] Git commit created

### Test Results

Pending verification.



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
- Syscall tracing with strace

### Tests

- Strict compile: PASS
- Process inspection test: PASS
- Regression smoke test: PASS
- ASan/UBSan: PASS

## Phase 03 — TCP/IP Fundamentals and Socket Lifecycle

Status: COMPLETE

### Concepts Learned

- IPv4 loopback networking
- TCP server socket lifecycle
- socket()
- setsockopt()
- bind()
- listen()
- accept()
- recv()
- send()
- Listening vs connected sockets
- TCP ports and ephemeral client ports
- Network byte order
- SO_REUSEADDR
- TCP socket inspection using ss
- Socket file descriptors through /proc
- Socket syscall tracing with strace

### Tests

- Strict compilation: PASS
- Manual TCP connection: PASS
- ss socket inspection: PASS
- /proc FD inspection: PASS
- strace socket lifecycle: PASS
- TCP integration test: PASS
- Regression suite: PASS
- ASan/UBSan: PASS


## Phase 04 — Robust Accept Loop and Graceful Shutdown

Status: COMPLETE

### Concepts Learned

- Long-lived listening sockets
- Repeated accept() loops
- File descriptor reuse
- errno
- EINTR
- POSIX signal handling
- sigaction()
- sig_atomic_t
- SIGINT and SIGTERM
- Graceful process shutdown
- SIGPIPE avoidance
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
- ASan/UBSan: PASS

