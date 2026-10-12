# Scripts

Helper scripts used by the integration tests and manual labs.

## Networking and cgroup labs

- `netns_veth_lab.sh` - creates and verifies the disposable network-namespace/veth topology used by Phase 23.
- `cgroup_v2_lab.sh` - creates a cgroup v2 leaf and checks process membership/accounting.
- `cgroup_memory_lab.sh` - applies a memory limit and verifies the controlled OOM test.
- `cgroup_cpu_lab.sh` - applies `cpu.max` and verifies scheduler throttling.

These scripts create kernel state and normally need root privileges. Each has a `help` command and a cleanup path.

Example:

```bash
sudo scripts/netns_veth_lab.sh setup
sudo scripts/netns_veth_lab.sh verify
sudo scripts/netns_veth_lab.sh cleanup
```

## Observability helpers

- `otel_collector_lab.sh` - starts/stops the OpenTelemetry Collector development lab.
- `grafana_lgtm_lab.sh` - starts/stops the Grafana LGTM development stack used by the observability integration tests.
- `phase38_telemetry_benchmark.py` - measures the bounded telemetry path under normal, missing, and stalled collector conditions.

The matching Make targets are generally easier to use than invoking these scripts directly.

## Proxy benchmark

`benchmark_proxy.py` drives the Stage 2 direct-backend-versus-proxy baseline and writes `benchmarks/phase20_baseline.json`.

Run it through Make so the correct binary and arguments are used:

```bash
make benchmark
```
