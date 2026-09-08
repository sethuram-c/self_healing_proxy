#!/usr/bin/env python3
"""Stage 3 hardened Linux isolation launcher (Phase 28).

Composes and verifies:
- a named network namespace plus host<->namespace veth pair
- a cgroup v2 leaf with CPU, memory, and PID ceilings
- a distinct mount namespace with private propagation and fresh /proc
- a child PID namespace whose workload is PID 1
- host-root privilege drop to the invoking sudo user (or explicit UID/GID)
- PR_SET_NO_NEW_PRIVS for the final workload
- PID-reuse-safe cleanup based on cgroup ownership, not blind numeric PID kills
- stale-state diagnosis and explicit recovery

This remains an educational isolation harness, not a production container
runtime: it does not construct a private root filesystem or seccomp policy.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import select
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
ROOT = Path(__file__).resolve().parents[1]
CHILD_HELPER = ROOT / "control-plane" / "isolation_child.py"


class LauncherError(RuntimeError):
    pass


class RestrictedError(LauncherError):
    pass


def env(name: str, default: str) -> str:
    return os.environ.get(name, default)


def parse_positive_int(raw: str, label: str) -> int:
    try:
        value = int(raw, 10)
    except ValueError as exc:
        raise LauncherError(f"{label} must be a positive integer") from exc
    if value <= 0:
        raise LauncherError(f"{label} must be a positive integer")
    return value


def target_identity() -> tuple[int, int]:
    uid_raw = os.environ.get("PHASE28_RUN_UID") or os.environ.get("SUDO_UID")
    gid_raw = os.environ.get("PHASE28_RUN_GID") or os.environ.get("SUDO_GID")
    # If root invokes the launcher without sudo metadata, do not leave the
    # workload as host root.  Nobody is a conservative fallback for the lab.
    uid = int(uid_raw, 10) if uid_raw is not None else 65534
    gid = int(gid_raw, 10) if gid_raw is not None else 65534
    if uid < 0 or gid < 0:
        raise LauncherError("Phase 28 workload UID/GID must be non-negative")
    if uid == 0:
        raise LauncherError(
            "Phase 28 refuses a host-root workload; set PHASE28_RUN_UID/GID to a non-root identity"
        )
    return uid, gid


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
    run_uid, run_gid = target_identity()

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
        "run_uid": run_uid,
        "run_gid": run_gid,
        "state_dir": state_dir,
        "state_file": state_dir / f"{cgroup_name}.json",
        "log_file": state_dir / f"{cgroup_name}.log",
        "cgroup_path": CGROUP_ROOT / cgroup_name,
    }


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
        raise LauncherError("Stage 3 launcher currently requires IPv4 addresses")
    if host.network != ns.network or host.ip == ns.ip:
        raise LauncherError("host and namespace addresses must be distinct members of the same subnet")


def validate_cpu_max(raw: str) -> None:
    fields = raw.split()
    if len(fields) != 2:
        raise LauncherError("PHASE27_CPU_MAX must be '<quota> <period>'")
    quota = parse_positive_int(fields[0], "CPU quota")
    period = parse_positive_int(fields[1], "CPU period")
    if quota > period:
        raise LauncherError("CPU quota must not exceed its period")


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
        raise RestrictedError("real Stage 3 isolation operations require root")


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


def proc_starttime(pid: int) -> int | None:
    try:
        text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    # Field 2 is parenthesized comm and may contain spaces; split after final ')'.
    end = text.rfind(")")
    if end < 0:
        return None
    fields = text[end + 2 :].split()
    # fields[0] is stat field 3 (state); starttime is field 22 => index 19 here.
    if len(fields) <= 19:
        return None
    try:
        return int(fields[19], 10)
    except ValueError:
        return None


def same_process(pid: int, starttime: object) -> bool:
    try:
        expected = int(starttime)
    except (TypeError, ValueError):
        return False
    actual = proc_starttime(pid)
    return actual is not None and actual == expected


def wait_for_state(pid: int, expected: str, timeout: float = 3.0) -> None:
    deadline = time.monotonic() + timeout
    status = Path(f"/proc/{pid}/status")
    while time.monotonic() < deadline:
        try:
            for line in status.read_text(encoding="utf-8").splitlines():
                if line.startswith("State:"):
                    if line.split()[1] == expected:
                        return
                    break
        except FileNotFoundError:
            break
        time.sleep(0.02)
    raise LauncherError(f"process {pid} did not enter state {expected!r}")


def wait_cgroup_empty(path: Path, timeout: float = 3.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if not (path / "cgroup.procs").read_text(encoding="utf-8").split():
                return True
        except FileNotFoundError:
            return True
        time.sleep(0.05)
    return False


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
            "child cgroup is missing required controller files: " + ", ".join(missing)
            + "; inspect cgroup.subtree_control"
        )


def setup_network(cfg: dict[str, object]) -> None:
    ns = str(cfg["ns_name"])
    host_if = str(cfg["host_if"])
    ns_if = str(cfg["ns_if"])
    run(["ip", "netns", "add", ns])
    run(["ip", "link", "add", host_if, "type", "veth", "peer", "name", ns_if])
    run(["ip", "link", "set", ns_if, "netns", ns])
    run(["ip", "addr", "add", str(cfg["host_addr"]), "dev", host_if])
    run(["ip", "link", "set", host_if, "up"])
    run(["ip", "-n", ns, "addr", "add", str(cfg["ns_addr"]), "dev", ns_if])
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


def parse_cgroup_path(pid: int) -> str:
    for line in Path(f"/proc/{pid}/cgroup").read_text(encoding="utf-8").splitlines():
        if line.startswith("0::"):
            return line[3:]
    raise LauncherError(f"no cgroup v2 membership found for PID {pid}")


def process_in_cgroup(pid: int, cgroup_name: str) -> bool:
    try:
        return parse_cgroup_path(pid) == f"/{cgroup_name}"
    except (FileNotFoundError, LauncherError):
        return False


def ns_identity(path: str) -> str:
    return os.readlink(path)


def status_fields(pid: int) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            result[key] = value.strip()
    return result


def read_state(cfg: dict[str, object]) -> dict[str, object]:
    path = Path(cfg["state_file"])
    if not path.exists():
        raise LauncherError(f"Stage 3 state file is absent: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise LauncherError(f"invalid Stage 3 state file: {path}") from exc
    if not isinstance(value, dict):
        raise LauncherError(f"invalid Stage 3 state object: {path}")
    return value


def validate_state_ownership(cfg: dict[str, object], state: dict[str, object]) -> None:
    pairs = {
        "ns_name": cfg["ns_name"],
        "cgroup_name": cfg["cgroup_name"],
        "host_if": cfg["host_if"],
        "ns_if": cfg["ns_if"],
    }
    for key, expected in pairs.items():
        if state.get(key) != expected:
            raise LauncherError(
                f"state ownership mismatch for {key}: expected {expected!r}, got {state.get(key)!r}; refusing destructive cleanup"
            )


def save_state(
    cfg: dict[str, object], leader_pid: int, workload_pid: int, command: Sequence[str]
) -> None:
    state_dir = Path(cfg["state_dir"])
    state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    state = {
        "format_version": 2,
        "leader_pid": leader_pid,
        "leader_starttime": proc_starttime(leader_pid),
        "workload_pid": workload_pid,
        "pid": workload_pid,  # Phase 27 state compatibility alias
        "workload_starttime": proc_starttime(workload_pid),
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
        "run_uid": cfg["run_uid"],
        "run_gid": cfg["run_gid"],
        "started_monotonic": time.monotonic(),
    }
    Path(cfg["state_file"]).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    os.chmod(Path(cfg["state_file"]), 0o600)


def resource_presence(cfg: dict[str, object]) -> dict[str, bool]:
    return {
        "state": Path(cfg["state_file"]).exists(),
        "cgroup": Path(cfg["cgroup_path"]).is_dir(),
        "netns": netns_exists(str(cfg["ns_name"])),
        "host_if": link_exists(str(cfg["host_if"])),
    }


def classify_instance(cfg: dict[str, object]) -> tuple[str, str]:
    present = resource_presence(cfg)
    if not any(present.values()):
        return "CLEAN", "no Stage 3 resources are present"
    if not present["state"]:
        return "PARTIAL", "kernel resources exist without an ownership state file"
    try:
        state = read_state(cfg)
        validate_state_ownership(cfg, state)
    except LauncherError as exc:
        return "PARTIAL", str(exc)

    try:
        leader_pid = int(state["leader_pid"])
        workload_pid = int(state["workload_pid"])
    except (KeyError, TypeError, ValueError):
        return "STALE", "state lacks valid Phase 28 process identity fields"

    leader_ok = same_process(leader_pid, state.get("leader_starttime")) and process_in_cgroup(
        leader_pid, str(cfg["cgroup_name"])
    )
    workload_ok = same_process(workload_pid, state.get("workload_starttime")) and process_in_cgroup(
        workload_pid, str(cfg["cgroup_name"])
    )
    kernel_ok = present["cgroup"] and present["netns"] and present["host_if"]
    if leader_ok and workload_ok and kernel_ok:
        return "LIVE", "tracked leader/workload identities and kernel resources agree"
    return "STALE", "state exists but tracked process identity or kernel resources no longer form a live instance"


def doctor(cfg: dict[str, object]) -> str:
    require_binary("ip")
    state, reason = classify_instance(cfg)
    present = resource_presence(cfg)
    print(f"INSTANCE_STATE={state}")
    print(f"STATE_PRESENT={int(present['state'])}")
    print(f"CGROUP_PRESENT={int(present['cgroup'])}")
    print(f"NETNS_PRESENT={int(present['netns'])}")
    print(f"HOST_IF_PRESENT={int(present['host_if'])}")
    print(f"DIAGNOSIS={reason}")
    if present["state"]:
        try:
            saved = read_state(cfg)
            print(f"RECORDED_LEADER_PID={saved.get('leader_pid', '')}")
            print(f"RECORDED_WORKLOAD_PID={saved.get('workload_pid', '')}")
        except LauncherError as exc:
            print(f"STATE_ERROR={exc}")
    return state


def kill_owned_cgroup(cfg: dict[str, object]) -> None:
    path = Path(cfg["cgroup_path"])
    if not path.is_dir():
        return
    kill_file = path / "cgroup.kill"
    if kill_file.exists():
        write_text(kill_file, "1")
        if not wait_cgroup_empty(path):
            raise LauncherError("cgroup.kill did not empty the owned cgroup")
        return

    # Fallback for kernels without cgroup.kill. Never signal a PID merely
    # because it is recorded in JSON; first re-read exact cgroup membership.
    for raw in (path / "cgroup.procs").read_text(encoding="utf-8").split():
        try:
            pid = int(raw)
        except ValueError:
            continue
        if process_in_cgroup(pid, str(cfg["cgroup_name"])):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    if not wait_cgroup_empty(path):
        raise LauncherError("owned cgroup still contains tasks after cleanup")


def remove_resources(cfg: dict[str, object], *, require_state: bool) -> None:
    state_file = Path(cfg["state_file"])
    if state_file.exists():
        state = read_state(cfg)
        validate_state_ownership(cfg, state)
    elif require_state and any(resource_presence(cfg).values()):
        raise LauncherError(
            "refusing destructive cleanup: Stage 3 kernel resources exist without the ownership state file"
        )

    kill_owned_cgroup(cfg)

    if link_exists(str(cfg["host_if"])):
        run(["ip", "link", "delete", str(cfg["host_if"])], check=False)
    if netns_exists(str(cfg["ns_name"])):
        run(["ip", "netns", "delete", str(cfg["ns_name"])], check=False)

    cgroup_path = Path(cfg["cgroup_path"])
    if cgroup_path.is_dir():
        if (cgroup_path / "cgroup.procs").read_text(encoding="utf-8").split():
            raise LauncherError("refusing to remove nonempty Stage 3 cgroup")
        cgroup_path.rmdir()

    for path in (Path(cfg["state_file"]), Path(cfg["log_file"])):
        try:
            path.unlink()
        except FileNotFoundError:
            pass
    try:
        Path(cfg["state_dir"]).rmdir()
    except (FileNotFoundError, OSError):
        pass


def rollback_start(cfg: dict[str, object]) -> None:
    # Internal rollback is allowed without a state file because this invocation
    # has just created the known, validated resources.
    try:
        kill_owned_cgroup(cfg)
    except Exception:
        pass
    if link_exists(str(cfg["host_if"])):
        run(["ip", "link", "delete", str(cfg["host_if"])], check=False)
    if netns_exists(str(cfg["ns_name"])):
        run(["ip", "netns", "delete", str(cfg["ns_name"])], check=False)
    path = Path(cfg["cgroup_path"])
    if path.is_dir():
        try:
            if not (path / "cgroup.procs").read_text(encoding="utf-8").split():
                path.rmdir()
        except OSError:
            pass
    for item in (Path(cfg["state_file"]), Path(cfg["log_file"])):
        try:
            item.unlink()
        except FileNotFoundError:
            pass


def read_workload_pid(fd: int, timeout: float = 5.0) -> int:
    ready, _, _ = select.select([fd], [], [], timeout)
    if not ready:
        raise LauncherError("isolated child did not report its host-visible workload PID")
    data = os.read(fd, 64).decode("ascii", errors="strict").strip()
    try:
        pid = int(data, 10)
    except ValueError as exc:
        raise LauncherError(f"invalid workload PID from isolation child: {data!r}") from exc
    if pid <= 0:
        raise LauncherError("invalid non-positive workload PID")
    return pid


def start(cfg: dict[str, object], command: Sequence[str]) -> None:
    if not command:
        raise LauncherError("start requires a command after '--'")
    if shutil.which(command[0]) is None and not Path(command[0]).is_file():
        raise LauncherError(f"command is unavailable: {command[0]}")
    require_root()
    require_binary("ip")
    require_binary("ping")
    require_binary("nsenter")
    require_cgroup_v2()
    if not CHILD_HELPER.is_file():
        raise LauncherError(f"missing Phase 28 child helper: {CHILD_HELPER}")

    instance_state, reason = classify_instance(cfg)
    if instance_state != "CLEAN":
        raise LauncherError(
            f"Stage 3 isolation instance is {instance_state}: {reason}; run 'doctor' and then cleanup/recover as appropriate"
        )

    proc: subprocess.Popen[str] | None = None
    log_handle = None
    read_fd = -1
    write_fd = -1
    try:
        setup_cgroup(cfg)
        setup_network(cfg)
        state_dir = Path(cfg["state_dir"])
        state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        log_handle = Path(cfg["log_file"]).open("w", encoding="utf-8")
        os.chmod(Path(cfg["log_file"]), 0o600)
        read_fd, write_fd = os.pipe()
        proc = subprocess.Popen(
            [
                sys.executable,
                str(CHILD_HELPER),
                "--netns", str(cfg["ns_name"]),
                "--uid", str(cfg["run_uid"]),
                "--gid", str(cfg["run_gid"]),
                "--notify-fd", str(write_fd),
                "--",
                *command,
            ],
            stdin=subprocess.DEVNULL,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
            pass_fds=(write_fd,),
        )
        os.close(write_fd)
        write_fd = -1
        wait_for_state(proc.pid, "T")
        write_text(Path(cfg["cgroup_path"]) / "cgroup.procs", str(proc.pid))
        if not process_in_cgroup(proc.pid, str(cfg["cgroup_name"])):
            raise LauncherError("failed to verify isolation helper membership after writing cgroup.procs")
        os.kill(proc.pid, signal.SIGCONT)
        workload_pid = read_workload_pid(read_fd)
        os.close(read_fd)
        read_fd = -1
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline and not process_in_cgroup(workload_pid, str(cfg["cgroup_name"])):
            time.sleep(0.02)
        if not process_in_cgroup(workload_pid, str(cfg["cgroup_name"])):
            raise LauncherError("workload did not inherit the owned cgroup")
        if proc_starttime(workload_pid) is None:
            raise LauncherError("workload exited before Phase 28 could record its process identity")
        save_state(cfg, proc.pid, workload_pid, command)
        print("PHASE27_START_OK")
        print("PHASE28_START_OK")
        print(f"LEADER_PID={proc.pid}")
        print(f"WORKLOAD_PID={workload_pid}")
        print(f"NETNS={cfg['ns_name']}")
        print(f"CGROUP_PATH={cfg['cgroup_path']}")
        print(f"RUN_UID={cfg['run_uid']}")
        print(f"RUN_GID={cfg['run_gid']}")
        print(f"CPU_MAX={cfg['cpu_max']}")
        print(f"MEMORY_MAX_BYTES={cfg['memory_max_bytes']}")
        print(f"PIDS_MAX={cfg['pids_max']}")
    except Exception:
        rollback_start(cfg)
        raise
    finally:
        if read_fd >= 0:
            os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)
        if log_handle is not None:
            log_handle.close()


def verify(cfg: dict[str, object]) -> None:
    require_root()
    require_binary("ip")
    require_binary("ping")
    require_binary("nsenter")
    state = read_state(cfg)
    validate_state_ownership(cfg, state)
    leader_pid = int(state["leader_pid"])
    workload_pid = int(state["workload_pid"])
    if not same_process(leader_pid, state.get("leader_starttime")):
        raise LauncherError(f"isolation leader PID {leader_pid} identity is stale")
    if not same_process(workload_pid, state.get("workload_starttime")):
        raise LauncherError(f"workload PID {workload_pid} identity is stale")

    cgroup_path = Path(cfg["cgroup_path"])
    if not cgroup_path.is_dir():
        raise LauncherError(f"cgroup is missing: {cgroup_path}")
    if not netns_exists(str(cfg["ns_name"])):
        raise LauncherError(f"network namespace is missing: {cfg['ns_name']}")

    expected_cgroup = f"/{cfg['cgroup_name']}"
    actual_cgroup = parse_cgroup_path(workload_pid)
    if actual_cgroup != expected_cgroup:
        raise LauncherError(f"workload cgroup mismatch: expected {expected_cgroup}, got {actual_cgroup}")

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
    workload_net = ns_identity(f"/proc/{workload_pid}/ns/net")
    named_net = run(["ip", "netns", "exec", str(cfg["ns_name"]), "readlink", "/proc/self/ns/net"]).stdout.strip()
    if workload_net == host_net or workload_net != named_net:
        raise LauncherError("workload network namespace identity is incorrect")

    host_pidns = ns_identity("/proc/self/ns/pid")
    workload_pidns = ns_identity(f"/proc/{workload_pid}/ns/pid")
    if workload_pidns == host_pidns:
        raise LauncherError("workload is still in the host PID namespace")

    host_mnt = ns_identity("/proc/self/ns/mnt")
    workload_mnt = ns_identity(f"/proc/{workload_pid}/ns/mnt")
    if workload_mnt == host_mnt:
        raise LauncherError("workload is still in the host mount namespace")

    fields = status_fields(workload_pid)
    uid_fields = fields.get("Uid", "").split()
    if len(uid_fields) < 2 or int(uid_fields[0]) != int(cfg["run_uid"]) or int(uid_fields[1]) != int(cfg["run_uid"]):
        raise LauncherError(f"workload UID was not dropped to {cfg['run_uid']}")
    if fields.get("NoNewPrivs") != "1":
        raise LauncherError("workload does not have no_new_privs enabled")
    if int(fields.get("CapEff", "0"), 16) != 0:
        raise LauncherError("workload still has effective Linux capabilities")
    nspid = fields.get("NSpid", "").split()
    if len(nspid) < 2 or nspid[-1] != "1":
        raise LauncherError(f"workload is not PID 1 in a child PID namespace: NSpid={fields.get('NSpid', '')!r}")

    proc1 = run([
    "nsenter",
    "--target",
    str(workload_pid),
    "--mount",
    "--pid",
    "cat",
    "/proc/1/status",
    ]).stdout
    if "NSpid:" not in proc1:
        raise LauncherError("fresh isolated /proc verification failed")

    host_addr_text = run(["ip", "-brief", "addr", "show", "dev", str(cfg["host_if"])]).stdout
    ns_addr_text = run(["ip", "-n", str(cfg["ns_name"]), "-brief", "addr", "show", "dev", str(cfg["ns_if"])]).stdout
    if str(cfg["host_addr"]) not in host_addr_text or str(cfg["ns_addr"]) not in ns_addr_text:
        raise LauncherError("veth address verification failed")

    ns_ip = str(ipaddress.ip_interface(str(cfg["ns_addr"])).ip)
    host_ip = str(ipaddress.ip_interface(str(cfg["host_addr"])).ip)
    if run(["ping", "-c", "1", "-W", "1", ns_ip], check=False).returncode != 0:
        raise LauncherError("host -> isolated namespace ping failed")
    if run(["ip", "netns", "exec", str(cfg["ns_name"]), "ping", "-c", "1", "-W", "1", host_ip], check=False).returncode != 0:
        raise LauncherError("isolated namespace -> host ping failed")

    print(f"WORKLOAD_PID={workload_pid}")
    print(f"WORKLOAD_CGROUP={actual_cgroup}")
    print(f"WORKLOAD_UID={cfg['run_uid']}")
    print("NO_NEW_PRIVS=1")
    print("EFFECTIVE_CAPABILITIES=0")
    print(f"HOST_NETNS={host_net}")
    print(f"WORKLOAD_NETNS={workload_net}")
    print(f"HOST_PIDNS={host_pidns}")
    print(f"WORKLOAD_PIDNS={workload_pidns}")
    print(f"HOST_MNTNS={host_mnt}")
    print(f"WORKLOAD_MNTNS={workload_mnt}")
    print("WORKLOAD_NAMESPACE_PID=1")
    print("FRESH_PROC=PASS")
    print(f"CPU_MAX={expected_limits['cpu.max']}")
    print(f"MEMORY_MAX={expected_limits['memory.max']}")
    print(f"PIDS_MAX={expected_limits['pids.max']}")
    print("HOST_TO_NAMESPACE_PING=PASS")
    print("NAMESPACE_TO_HOST_PING=PASS")
    print("PHASE27_ISOLATION_VERIFIED")
    print("PHASE28_HARDENED_ISOLATION_VERIFIED")


def status(cfg: dict[str, object]) -> None:
    require_binary("ip")
    doctor_state, reason = classify_instance(cfg)
    print(f"INSTANCE_STATE={doctor_state}")
    print(f"DIAGNOSIS={reason}")
    print(f"STATE_FILE={cfg['state_file']}")
    print(f"NETNS={cfg['ns_name']}")
    print(f"CGROUP_PATH={cfg['cgroup_path']}")
    print(f"HOST_IF={cfg['host_if']}")
    if Path(cfg["state_file"]).exists():
        try:
            state = read_state(cfg)
            print(f"LEADER_PID={state.get('leader_pid', '')}")
            print(f"WORKLOAD_PID={state.get('workload_pid', '')}")
            print(f"COMMAND={json.dumps(state.get('command', []))}")
            workload_pid = int(state.get("workload_pid", -1))
            if workload_pid > 0 and Path(f"/proc/{workload_pid}").exists():
                print(f"WORKLOAD_CGROUP={parse_cgroup_path(workload_pid)}")
                print(f"WORKLOAD_NETNS={ns_identity(f'/proc/{workload_pid}/ns/net')}")
                print(f"WORKLOAD_PIDNS={ns_identity(f'/proc/{workload_pid}/ns/pid')}")
                print(f"WORKLOAD_MNTNS={ns_identity(f'/proc/{workload_pid}/ns/mnt')}")
                fields = status_fields(workload_pid)
                print(f"WORKLOAD_UIDS={fields.get('Uid', '')}")
                print(f"WORKLOAD_NO_NEW_PRIVS={fields.get('NoNewPrivs', '')}")
        except Exception as exc:
            print(f"STATE_INSPECTION_ERROR={exc}")
    path = Path(cfg["cgroup_path"])
    if path.is_dir():
        for filename in ("cpu.max", "memory.max", "pids.max", "memory.current", "pids.current", "cpu.stat"):
            fpath = path / filename
            if fpath.exists():
                print(f"{filename.upper().replace('.', '_')}_BEGIN")
                print(fpath.read_text(encoding="utf-8").strip())
                print(f"{filename.upper().replace('.', '_')}_END")
    log = Path(cfg["log_file"])
    if log.exists():
        print("WORKLOAD_LOG_BEGIN")
        print(log.read_text(encoding="utf-8"), end="")
        print("WORKLOAD_LOG_END")


def cleanup(cfg: dict[str, object], *, quiet: bool = False) -> None:
    require_root()
    require_binary("ip")
    present = resource_presence(cfg)
    if not any(present.values()):
        if not quiet:
            print("PHASE27_CLEANUP_OK")
            print("PHASE28_CLEANUP_OK")
        return
    remove_resources(cfg, require_state=True)
    if not quiet:
        print("PHASE27_CLEANUP_OK")
        print("PHASE28_CLEANUP_OK")


def recover(cfg: dict[str, object]) -> None:
    require_root()
    require_binary("ip")
    state, reason = classify_instance(cfg)
    if state == "CLEAN":
        print("PHASE28_RECOVERY_NOT_NEEDED")
        return
    if state == "LIVE":
        raise LauncherError("instance is LIVE; use cleanup for an intentional shutdown")
    if state == "PARTIAL" and not Path(cfg["state_file"]).exists():
        raise LauncherError(
            f"instance is PARTIAL without ownership state: {reason}; refusing automatic recovery"
        )
    remove_resources(cfg, require_state=True)
    print("PHASE28_STALE_RECOVERY_OK")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Stage 3 hardened Linux isolation launcher: network namespace plus PID/mount namespaces, "
            "cgroup v2 resource ceilings, privilege drop, and stale-state recovery."
        )
    )
    sub = parser.add_subparsers(dest="action", required=True)
    start_parser = sub.add_parser("start", help="create hardened isolation and start a command")
    start_parser.add_argument("command", nargs=argparse.REMAINDER)
    sub.add_parser("verify", help="verify namespaces, privilege drop, cgroup limits, and reachability")
    sub.add_parser("status", help="show current launcher state and kernel evidence")
    sub.add_parser("doctor", help="classify the instance as CLEAN, LIVE, STALE, or PARTIAL")
    sub.add_parser("recover", help="remove an owned STALE instance without trusting recorded PIDs")
    sub.add_parser("cleanup", help="terminate the owned live/stale cgroup and remove disposable objects")
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
        elif args.action == "doctor":
            doctor(cfg)
        elif args.action == "recover":
            recover(cfg)
        elif args.action == "cleanup":
            cleanup(cfg)
        else:  # pragma: no cover
            raise LauncherError(f"unsupported action: {args.action}")
        return 0
    except RestrictedError as exc:
        print(f"SKIP: {exc}", file=sys.stderr)
        return EXIT_RESTRICTED
    except (LauncherError, OSError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        if isinstance(exc, subprocess.CalledProcessError) and exc.stderr:
            print(exc.stderr.rstrip(), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
