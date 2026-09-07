#!/usr/bin/env python3
"""Phase 27 disposable Linux isolation launcher.

Composes already-proven Stage 3 primitives:
- one named network namespace plus a host<->namespace veth pair
- one cgroup v2 leaf with CPU, memory, and PID ceilings
- race-free process placement using SIGSTOP before cgroup membership

Phase 28 owns security hardening and production cleanup policy. This launcher is
an explicit learning/verification harness, not a container runtime.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
from typing import Sequence

EXIT_RESTRICTED = 77
CGROUP_ROOT = Path("/sys/fs/cgroup")
DEFAULT_STATE_DIR = Path("/run/self-healing-proxy-phase27")
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
IFACE_RE = re.compile(r"^[A-Za-z0-9_.-]{1,15}$")


class LauncherError(RuntimeError):
    pass


class RestrictedError(LauncherError):
    pass


def env(name: str, default: str) -> str:
    return os.environ.get(name, default)


def config() -> dict[str, object]:
    ns_name = env("PHASE27_NS_NAME", "shp27-edge")
    cgroup_name = env("PHASE27_CGROUP_NAME", "shp27-launch")
    host_if = env("PHASE27_HOST_IF", "p27host0")
    ns_if = env("PHASE27_NS_IF", "p27ns0")
    host_addr = env("PHASE27_HOST_ADDR", "10.227.0.1/30")
    ns_addr = env("PHASE27_NS_ADDR", "10.227.0.2/30")
    cpu_max = env("PHASE27_CPU_MAX", "50000 100000")
    memory_max_mib = parse_positive_int(env("PHASE27_MEMORY_MAX_MIB", "64"), "PHASE27_MEMORY_MAX_MIB")
    pids_max = parse_positive_int(env("PHASE27_PIDS_MAX", "32"), "PHASE27_PIDS_MAX")
    state_dir = Path(env("PHASE27_STATE_DIR", str(DEFAULT_STATE_DIR)))

    validate_name(ns_name, "network namespace", "shp27-")
    validate_name(cgroup_name, "cgroup", "shp27-")
    validate_iface(host_if, "host interface")
    validate_iface(ns_if, "namespace interface")
    validate_addresses(host_addr, ns_addr)
    validate_cpu_max(cpu_max)

    return {
        "ns_name": ns_name,
        "cgroup_name": cgroup_name,
        "host_if": host_if,
        "ns_if": ns_if,
        "host_addr": host_addr,
        "ns_addr": ns_addr,
        "cpu_max": cpu_max,
        "memory_max_bytes": memory_max_mib * 1024 * 1024,
        "pids_max": pids_max,
        "state_dir": state_dir,
        "state_file": state_dir / f"{cgroup_name}.json",
        "log_file": state_dir / f"{cgroup_name}.log",
        "cgroup_path": CGROUP_ROOT / cgroup_name,
    }


def parse_positive_int(raw: str, label: str) -> int:
    try:
        value = int(raw, 10)
    except ValueError as exc:
        raise LauncherError(f"{label} must be a positive integer") from exc
    if value <= 0:
        raise LauncherError(f"{label} must be a positive integer")
    return value


def validate_name(value: str, label: str, prefix: str) -> None:
    if not value.startswith(prefix) or not NAME_RE.fullmatch(value):
        raise LauncherError(f"{label} name must start with {prefix!r} and contain only safe characters")


def validate_iface(value: str, label: str) -> None:
    if not IFACE_RE.fullmatch(value):
        raise LauncherError(f"{label} must be 1..15 safe interface-name characters")


def validate_addresses(host_addr: str, ns_addr: str) -> None:
    try:
        host = ipaddress.ip_interface(host_addr)
        ns = ipaddress.ip_interface(ns_addr)
    except ValueError as exc:
        raise LauncherError("PHASE27_HOST_ADDR and PHASE27_NS_ADDR must be valid interface CIDRs") from exc
    if host.version != 4 or ns.version != 4:
        raise LauncherError("Phase 27 currently requires IPv4 addresses")
    if host.network != ns.network or host.ip == ns.ip:
        raise LauncherError("host and namespace addresses must be distinct members of the same subnet")


def validate_cpu_max(raw: str) -> None:
    fields = raw.split()
    if len(fields) != 2:
        raise LauncherError("PHASE27_CPU_MAX must be '<quota> <period>'")
    quota = parse_positive_int(fields[0], "CPU quota")
    period = parse_positive_int(fields[1], "CPU period")
    if quota > period:
        raise LauncherError("Phase 27 CPU quota must not exceed its period")


def run(cmd: Sequence[str], *, check: bool = True, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(cmd),
        check=check,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )


def require_binary(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RestrictedError(f"required program is unavailable: {name}")
    return path


def require_root() -> None:
    if os.geteuid() != 0:
        raise RestrictedError("Phase 27 real isolation operations require root")


def require_cgroup_v2() -> None:
    try:
        fs_type = run(["stat", "-fc", "%T", str(CGROUP_ROOT)]).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RestrictedError(f"cannot inspect cgroup root: {exc}") from exc
    if fs_type != "cgroup2fs":
        raise RestrictedError(f"expected cgroup2fs at {CGROUP_ROOT}, got {fs_type!r}")
    if not os.access(CGROUP_ROOT, os.W_OK):
        raise RestrictedError(f"cgroup v2 root is not writable: {CGROUP_ROOT}")

    controllers = (CGROUP_ROOT / "cgroup.controllers").read_text(encoding="utf-8").split()
    missing = sorted({"cpu", "memory", "pids"} - set(controllers))
    if missing:
        raise RestrictedError("required cgroup v2 controllers unavailable: " + ", ".join(missing))


def write_text(path: Path, value: str) -> None:
    path.write_text(value.rstrip("\n") + "\n", encoding="utf-8")


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_for_state(pid: int, expected: str, timeout: float = 3.0) -> None:
    deadline = time.monotonic() + timeout
    status = Path(f"/proc/{pid}/status")
    while time.monotonic() < deadline:
        try:
            for line in status.read_text(encoding="utf-8").splitlines():
                if line.startswith("State:"):
                    state = line.split()[1]
                    if state == expected:
                        return
                    break
        except FileNotFoundError:
            break
        time.sleep(0.02)
    raise LauncherError(f"process {pid} did not enter state {expected!r}")


def netns_exists(name: str) -> bool:
    cp = run(["ip", "netns", "list"], check=False)
    return any(line.split()[0] == name for line in cp.stdout.splitlines() if line.strip())


def link_exists(name: str) -> bool:
    return run(["ip", "link", "show", "dev", name], check=False).returncode == 0


def cgroup_values_available(path: Path) -> None:
    required = ["cgroup.procs", "cpu.max", "memory.max", "pids.max"]
    missing = [item for item in required if not (path / item).exists()]
    if missing:
        raise RestrictedError(
            "child cgroup is missing required controller files: " + ", ".join(missing) +
            "; inspect cgroup.subtree_control"
        )


def setup_network(cfg: dict[str, object]) -> None:
    ns = str(cfg["ns_name"])
    host_if = str(cfg["host_if"])
    ns_if = str(cfg["ns_if"])
    host_addr = str(cfg["host_addr"])
    ns_addr = str(cfg["ns_addr"])

    run(["ip", "netns", "add", ns])
    run(["ip", "link", "add", host_if, "type", "veth", "peer", "name", ns_if])
    run(["ip", "link", "set", ns_if, "netns", ns])
    run(["ip", "addr", "add", host_addr, "dev", host_if])
    run(["ip", "link", "set", host_if, "up"])
    run(["ip", "-n", ns, "addr", "add", ns_addr, "dev", ns_if])
    run(["ip", "-n", ns, "link", "set", "lo", "up"])
    run(["ip", "-n", ns, "link", "set", ns_if, "up"])


def setup_cgroup(cfg: dict[str, object]) -> None:
    path = Path(cfg["cgroup_path"])
    path.mkdir(mode=0o755)
    cgroup_values_available(path)
    write_text(path / "cpu.max", str(cfg["cpu_max"]))
    write_text(path / "memory.max", str(cfg["memory_max_bytes"]))
    write_text(path / "pids.max", str(cfg["pids_max"]))
    swap_max = path / "memory.swap.max"
    if swap_max.exists():
        write_text(swap_max, "0")


def save_state(cfg: dict[str, object], pid: int, command: Sequence[str]) -> None:
    state_dir = Path(cfg["state_dir"])
    state_dir.mkdir(mode=0o755, parents=True, exist_ok=True)
    state = {
        "pid": pid,
        "command": list(command),
        "ns_name": cfg["ns_name"],
        "cgroup_name": cfg["cgroup_name"],
        "host_if": cfg["host_if"],
        "ns_if": cfg["ns_if"],
        "host_addr": cfg["host_addr"],
        "ns_addr": cfg["ns_addr"],
        "cpu_max": cfg["cpu_max"],
        "memory_max_bytes": cfg["memory_max_bytes"],
        "pids_max": cfg["pids_max"],
        "started_monotonic": time.monotonic(),
    }
    Path(cfg["state_file"]).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def read_state(cfg: dict[str, object]) -> dict[str, object]:
    path = Path(cfg["state_file"])
    if not path.exists():
        raise LauncherError(f"Phase 27 state file is absent: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise LauncherError(f"invalid Phase 27 state file: {path}") from exc


def start(cfg: dict[str, object], command: Sequence[str]) -> None:
    if not command:
        raise LauncherError("start requires a command after '--'")
    if shutil.which(command[0]) is None and not Path(command[0]).is_file():
        raise LauncherError(f"command is unavailable: {command[0]}")
    require_root()
    require_binary("ip")
    require_binary("bash")
    require_cgroup_v2()

    state_file = Path(cfg["state_file"])
    cgroup_path = Path(cfg["cgroup_path"])
    if state_file.exists() or cgroup_path.exists() or netns_exists(str(cfg["ns_name"])) or link_exists(str(cfg["host_if"])):
        raise LauncherError("Phase 27 isolation instance already exists; inspect or clean it before starting another")

    proc: subprocess.Popen[str] | None = None
    log_handle = None
    try:
        setup_cgroup(cfg)
        setup_network(cfg)
        state_dir = Path(cfg["state_dir"])
        state_dir.mkdir(mode=0o755, parents=True, exist_ok=True)
        log_handle = Path(cfg["log_file"]).open("w", encoding="utf-8")
        wrapper = 'kill -STOP $$; exec ip netns exec "$@"'
        proc = subprocess.Popen(
            ["bash", "-c", wrapper, "phase27-wrapper", str(cfg["ns_name"]), *command],
            stdin=subprocess.DEVNULL,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        wait_for_state(proc.pid, "T")
        write_text(cgroup_path / "cgroup.procs", str(proc.pid))
        members = (cgroup_path / "cgroup.procs").read_text(encoding="utf-8").split()
        if str(proc.pid) not in members:
            raise LauncherError("failed to verify process membership after writing cgroup.procs")
        save_state(cfg, proc.pid, command)
        os.kill(proc.pid, signal.SIGCONT)
        print("PHASE27_START_OK")
        print(f"WORKLOAD_PID={proc.pid}")
        print(f"NETNS={cfg['ns_name']}")
        print(f"CGROUP_PATH={cgroup_path}")
        print(f"CPU_MAX={cfg['cpu_max']}")
        print(f"MEMORY_MAX_BYTES={cfg['memory_max_bytes']}")
        print(f"PIDS_MAX={cfg['pids_max']}")
    except Exception:
        if proc is not None and pid_alive(proc.pid):
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        cleanup(cfg, quiet=True)
        raise
    finally:
        if log_handle is not None:
            log_handle.close()


def parse_cgroup_path(pid: int) -> str:
    for line in Path(f"/proc/{pid}/cgroup").read_text(encoding="utf-8").splitlines():
        if line.startswith("0::"):
            return line[3:]
    raise LauncherError(f"no cgroup v2 membership found for PID {pid}")


def ns_identity(path: str) -> str:
    return os.readlink(path)


def verify(cfg: dict[str, object]) -> None:
    require_root()
    require_binary("ip")
    state = read_state(cfg)
    pid = int(state["pid"])
    cgroup_path = Path(cfg["cgroup_path"])
    if not pid_alive(pid):
        raise LauncherError(f"workload PID {pid} is not alive")
    if not cgroup_path.is_dir():
        raise LauncherError(f"cgroup is missing: {cgroup_path}")
    if not netns_exists(str(cfg["ns_name"])):
        raise LauncherError(f"network namespace is missing: {cfg['ns_name']}")

    expected_cgroup = f"/{cfg['cgroup_name']}"
    actual_cgroup = parse_cgroup_path(pid)
    if actual_cgroup != expected_cgroup:
        raise LauncherError(f"workload cgroup mismatch: expected {expected_cgroup}, got {actual_cgroup}")

    members = (cgroup_path / "cgroup.procs").read_text(encoding="utf-8").split()
    if str(pid) not in members:
        raise LauncherError("workload PID is absent from cgroup.procs")

    expected_limits = {
        "cpu.max": str(cfg["cpu_max"]),
        "memory.max": str(cfg["memory_max_bytes"]),
        "pids.max": str(cfg["pids_max"]),
    }
    for filename, expected in expected_limits.items():
        actual = (cgroup_path / filename).read_text(encoding="utf-8").strip()
        if actual != expected:
            raise LauncherError(f"{filename} mismatch: expected {expected!r}, got {actual!r}")

    host_net = ns_identity("/proc/self/ns/net")
    workload_net = ns_identity(f"/proc/{pid}/ns/net")
    named_net = run(["ip", "netns", "exec", str(cfg["ns_name"]), "readlink", "/proc/self/ns/net"]).stdout.strip()
    if workload_net == host_net:
        raise LauncherError("workload is still in the host network namespace")
    if workload_net != named_net:
        raise LauncherError(f"workload netns {workload_net} does not match named netns {named_net}")

    host_addr = run(["ip", "-brief", "addr", "show", "dev", str(cfg["host_if"])]).stdout
    ns_addr = run(["ip", "-n", str(cfg["ns_name"]), "-brief", "addr", "show", "dev", str(cfg["ns_if"])]).stdout
    if str(cfg["host_addr"]) not in host_addr:
        raise LauncherError("host veth address is missing")
    if str(cfg["ns_addr"]) not in ns_addr:
        raise LauncherError("namespace veth address is missing")

    ns_ip = str(ipaddress.ip_interface(str(cfg["ns_addr"])).ip)
    host_ip = str(ipaddress.ip_interface(str(cfg["host_addr"])).ip)
    if run(["ping", "-c", "1", "-W", "1", ns_ip], check=False).returncode != 0:
        raise LauncherError("host -> isolated namespace ping failed")
    if run(["ip", "netns", "exec", str(cfg["ns_name"]), "ping", "-c", "1", "-W", "1", host_ip], check=False).returncode != 0:
        raise LauncherError("isolated namespace -> host ping failed")

    print(f"WORKLOAD_PID={pid}")
    print(f"WORKLOAD_CGROUP={actual_cgroup}")
    print(f"HOST_NETNS={host_net}")
    print(f"WORKLOAD_NETNS={workload_net}")
    print(f"CPU_MAX={expected_limits['cpu.max']}")
    print(f"MEMORY_MAX={expected_limits['memory.max']}")
    print(f"PIDS_MAX={expected_limits['pids.max']}")
    print("HOST_TO_NAMESPACE_PING=PASS")
    print("NAMESPACE_TO_HOST_PING=PASS")
    print("PHASE27_ISOLATION_VERIFIED")


def status(cfg: dict[str, object]) -> None:
    print(f"STATE_FILE={cfg['state_file']}")
    print(f"STATE_PRESENT={int(Path(cfg['state_file']).exists())}")
    print(f"NETNS={cfg['ns_name']}")
    print(f"NETNS_PRESENT={int(netns_exists(str(cfg['ns_name'])))}")
    print(f"CGROUP_PATH={cfg['cgroup_path']}")
    print(f"CGROUP_PRESENT={int(Path(cfg['cgroup_path']).is_dir())}")
    print(f"HOST_IF={cfg['host_if']}")
    print(f"HOST_IF_PRESENT={int(link_exists(str(cfg['host_if'])))}")

    if Path(cfg["state_file"]).exists():
        state = read_state(cfg)
        pid = int(state["pid"])
        print(f"WORKLOAD_PID={pid}")
        print(f"WORKLOAD_ALIVE={int(pid_alive(pid))}")
        print("COMMAND=" + json.dumps(state.get("command", [])))
        if pid_alive(pid):
            try:
                print(f"WORKLOAD_CGROUP={parse_cgroup_path(pid)}")
                print(f"WORKLOAD_NETNS={ns_identity(f'/proc/{pid}/ns/net')}")
            except (OSError, LauncherError) as exc:
                print(f"WORKLOAD_INSPECTION_ERROR={exc}")

    cgroup_path = Path(cfg["cgroup_path"])
    if cgroup_path.is_dir():
        for filename in ("cpu.max", "memory.max", "pids.max", "memory.current", "pids.current", "cpu.stat"):
            path = cgroup_path / filename
            if path.exists():
                print(f"{filename.upper().replace('.', '_')}_BEGIN")
                print(path.read_text(encoding="utf-8").rstrip())
                print(f"{filename.upper().replace('.', '_')}_END")

    if netns_exists(str(cfg["ns_name"])):
        print("NETNS_ADDR_BEGIN")
        print(run(["ip", "-n", str(cfg["ns_name"]), "-brief", "addr"]).stdout.rstrip())
        print("NETNS_ADDR_END")
    log_file = Path(cfg["log_file"])
    if log_file.exists():
        print("WORKLOAD_LOG_BEGIN")
        print(log_file.read_text(encoding="utf-8", errors="replace").rstrip())
        print("WORKLOAD_LOG_END")


def terminate_owned_pid(pid: int) -> None:
    if not pid_alive(pid):
        return
    try:
        os.killpg(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        if not pid_alive(pid):
            return
        time.sleep(0.05)
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline and pid_alive(pid):
        time.sleep(0.05)


def cleanup(cfg: dict[str, object], *, quiet: bool = False) -> None:
    require_binary("ip")
    if os.geteuid() != 0 and (
        Path(cfg["cgroup_path"]).exists() or netns_exists(str(cfg["ns_name"])) or link_exists(str(cfg["host_if"]))
    ):
        raise RestrictedError("cleanup requires root while Phase 27 kernel objects exist")

    state_file = Path(cfg["state_file"])
    if state_file.exists():
        try:
            state = read_state(cfg)
            terminate_owned_pid(int(state["pid"]))
        except (LauncherError, ValueError, KeyError):
            if not quiet:
                raise

    cgroup_path = Path(cfg["cgroup_path"])
    if cgroup_path.is_dir():
        members = (cgroup_path / "cgroup.procs").read_text(encoding="utf-8").split()
        if members:
            raise LauncherError("refusing to remove non-empty Phase 27 cgroup; remaining PIDs: " + " ".join(members))

    if link_exists(str(cfg["host_if"])):
        run(["ip", "link", "del", str(cfg["host_if"])], check=False)
    if netns_exists(str(cfg["ns_name"])):
        run(["ip", "netns", "del", str(cfg["ns_name"])] , check=False)
    if cgroup_path.is_dir():
        cgroup_path.rmdir()

    state_file.unlink(missing_ok=True)
    Path(cfg["log_file"]).unlink(missing_ok=True)
    state_dir = Path(cfg["state_dir"])
    try:
        state_dir.rmdir()
    except OSError:
        pass
    if not quiet:
        print("PHASE27_CLEANUP_OK")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compose a disposable network namespace and cgroup v2 sandbox around one command."
    )
    sub = parser.add_subparsers(dest="action", required=True)
    start_parser = sub.add_parser("start", help="create isolation and start a command")
    start_parser.add_argument("command", nargs=argparse.REMAINDER, help="command after '--'")
    sub.add_parser("verify", help="verify namespace, cgroup, limits, and veth reachability")
    sub.add_parser("status", help="show current launcher state and kernel evidence")
    sub.add_parser("cleanup", help="terminate the owned workload and remove disposable objects")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        cfg = config()
        if args.action == "start":
            command = list(args.command)
            if command and command[0] == "--":
                command = command[1:]
            start(cfg, command)
        elif args.action == "verify":
            verify(cfg)
        elif args.action == "status":
            status(cfg)
        elif args.action == "cleanup":
            cleanup(cfg)
        else:
            parser.error("unsupported action")
        return 0
    except RestrictedError as exc:
        print(f"SKIP: {exc}", file=sys.stderr)
        return EXIT_RESTRICTED
    except (LauncherError, OSError, subprocess.CalledProcessError) as exc:
        if isinstance(exc, subprocess.CalledProcessError):
            detail = (exc.stderr or exc.stdout or str(exc)).strip()
            print(f"ERROR: command failed: {' '.join(exc.cmd)}: {detail}", file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
