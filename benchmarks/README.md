# Benchmarks

This directory keeps benchmark output that is useful for comparing changes to the proxy.

## Stage 2 baseline

Run:

```bash
make benchmark
```

The harness starts two local mock backends and the optimized benchmark build of the proxy, warms both paths, then measures the same HTTP request workload directly against the backend pool and through the proxy.

Current workload:

- 1,000 measured requests per path
- concurrency of 32
- 100 warm-up requests per path
- one TCP connection per HTTP request
- two local backends on ports 9001 and 9002
- proxy on port 8081

Results are written to:

```text
benchmarks/phase20_baseline.json
```

The JSON includes success/failure counts, elapsed time, requests per second, mean/min/max latency, and backend distribution.

The checked-in result is only a development baseline. Hardware, kernel version, CPU power state, compiler flags, virtualization, and background load can all move these numbers significantly. Compare runs only when the test environment and workload are reasonably similar.

Do not compare sanitizer builds with optimized benchmark builds; sanitizer instrumentation adds substantial overhead.
