# Performance baselines

This directory stores repeatable benchmark results for the micro-proxy.

## Phase 20 baseline

Build and run the optimized benchmark binary and harness:

```bash
make benchmark
```

The command writes:

```text
benchmarks/phase20_baseline.json
```

The Phase 20 workload uses:

- two deterministic local mock backends on ports 9001 and 9002;
- the C proxy on port 8081;
- one TCP connection per HTTP request;
- a warm-up pass before measurements;
- the same HTTP path for direct-backend-pool and proxied measurements;
- a fixed measured request count and concurrency;
- proxy/backend stdout and stderr redirected away from the terminal.

The JSON report records successful/failed requests, elapsed time, requests per
second, mean/min/max request latency, backend distribution, and the proxy's
throughput/mean-latency delta relative to a direct backend request path.

Phase 20 is intentionally a baseline, not an optimization exercise. Formal
p50/p95/p99 analysis, CPU/memory/FD/syscall profiling, and longer soak testing
remain later roadmap phases.

## Reproducibility

Before comparing two results, keep the workload and environment as similar as
possible. Record meaningful changes such as compiler flags, CPU power mode,
virtualization, and competing workloads in your commit notes or experiment
notes.

Do not compare sanitized binaries against optimized benchmark binaries.
Sanitizers intentionally add substantial overhead.
