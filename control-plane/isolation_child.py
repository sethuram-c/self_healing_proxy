#!/usr/bin/env python3
"""Internal Phase 28 child helper for the Stage 3 isolation launcher.

The parent launcher starts this helper stopped, places it into the dedicated
cgroup, and then resumes it.  The helper then enters the named network
namespace, creates a private mount namespace and child PID namespace, forks the
real workload as PID 1, mounts a PID-namespace-correct /proc, drops host-root
privileges, enables no_new_privs, and execs the requested command.
"""

from __future__ import annotations

import argparse
import ctypes
import os
from pathlib import Path
import pwd
import signal
import sys

MS_REC = 16384
MS_PRIVATE = 1 << 18
MS_NOSUID = 2
MS_NODEV = 4
MS_NOEXEC = 8
PR_SET_PDEATHSIG = 1
PR_SET_NO_NEW_PRIVS = 38

libc = ctypes.CDLL(None, use_errno=True)
libc.mount.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_ulong, ctypes.c_void_p]
libc.mount.restype = ctypes.c_int
libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
libc.prctl.restype = ctypes.c_int


def fail_errno(label: str) -> None:
    err = ctypes.get_errno()
    raise OSError(err, f"{label}: {os.strerror(err)}")


def mount_private_root() -> None:
    if libc.mount(None, b"/", None, MS_REC | MS_PRIVATE, None) != 0:
        fail_errno("mount private root")


def mount_proc() -> None:
    if libc.mount(b"proc", b"/proc", b"proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, None) != 0:
        fail_errno("mount isolated /proc")


def prctl(option: int, value: int) -> None:
    if libc.prctl(option, value, 0, 0, 0) != 0:
        fail_errno(f"prctl({option})")


def safe_environment(uid: int) -> dict[str, str]:
    try:
        entry = pwd.getpwuid(uid)
        home = entry.pw_dir
        user = entry.pw_name
        shell = entry.pw_shell or "/bin/sh"
    except KeyError:
        home = "/"
        user = str(uid)
        shell = "/bin/sh"

    result = {
        "HOME": home,
        "USER": user,
        "LOGNAME": user,
        "SHELL": shell,
        "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    }
    for key in ("LANG", "LC_ALL", "LC_CTYPE", "TERM"):
        value = os.environ.get(key)
        if value:
            result[key] = value
    return result


def drop_privileges(uid: int, gid: int) -> None:
    os.setgroups([])
    os.setgid(gid)
    os.setuid(uid)
    prctl(PR_SET_NO_NEW_PRIVS, 1)
    os.umask(0o077)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--netns", required=True)
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    parser.add_argument("--notify-fd", type=int, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command and args.command[0] == "--":
        args.command = args.command[1:]
    if not args.command:
        parser.error("command required")
    return args


def main() -> int:
    args = parse_args()
    if os.geteuid() != 0:
        print("isolation_child must start as root", file=sys.stderr)
        return 1

    # The parent must place this exact process into the cgroup before resume.
    os.kill(os.getpid(), signal.SIGSTOP)

    netns_path = Path("/run/netns") / args.netns
    with netns_path.open("rb") as handle:
        os.setns(handle.fileno(), os.CLONE_NEWNET)

    os.unshare(os.CLONE_NEWNS | os.CLONE_NEWPID)
    mount_private_root()

    child = os.fork()
    if child < 0:
        raise RuntimeError("fork failed")
    if child > 0:
        # The helper remains outside the child PID namespace, so this is the
        # host-visible PID of the workload that becomes PID 1 inside it.
        os.write(args.notify_fd, f"{child}\n".encode("ascii"))
        os.close(args.notify_fd)
        _, status = os.waitpid(child, 0)
        if os.WIFEXITED(status):
            return os.WEXITSTATUS(status)
        if os.WIFSIGNALED(status):
            return 128 + os.WTERMSIG(status)
        return 1

    # Child: PID 1 inside the new namespace.  It shares the new mount
    # namespace with the helper but mounts /proc from inside the child PID
    # namespace so /proc reflects the isolated process view.
    try:
        os.close(args.notify_fd)
    except OSError:
        pass
    mount_proc()
    prctl(PR_SET_PDEATHSIG, signal.SIGKILL)
    drop_privileges(args.uid, args.gid)
    env = safe_environment(args.uid)
    os.execvpe(args.command[0], args.command, env)
    return 127


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - surfaced in workload log
        print(f"isolation_child: {exc}", file=sys.stderr)
        raise SystemExit(1)
