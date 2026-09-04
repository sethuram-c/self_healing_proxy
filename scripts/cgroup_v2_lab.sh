#!/usr/bin/env bash
set -euo pipefail

CGROUP_ROOT="${PHASE24_CGROUP_ROOT:-/sys/fs/cgroup}"
CGROUP_NAME="${PHASE24_CGROUP_NAME:-shp24-lab}"
STATE_ROOT="${PHASE24_STATE_ROOT:-/run/self-healing-proxy-phase24}"
WORKER_SECONDS="${PHASE24_WORKER_SECONDS:-300}"
CGROUP_PATH="${CGROUP_ROOT}/${CGROUP_NAME}"
STATE_FILE="${STATE_ROOT}/${CGROUP_NAME}.pid"

usage() {
    cat <<'USAGE'
Usage: cgroup_v2_lab.sh <setup|verify|status|cleanup|help>

Phase 24 cgroups v2 accounting lab.

Commands:
  setup    Create one disposable cgroup v2 leaf and move a sleeper into it.
  verify   Verify cgroup membership and resource-accounting files.
  status   Display membership and cgroup v2 accounting state.
  cleanup  Stop the lab worker and remove the disposable cgroup. Idempotent.
  help     Show this help.

Environment:
  PHASE24_CGROUP_NAME       Disposable cgroup name (default: shp24-lab)
  PHASE24_CGROUP_ROOT       cgroup v2 mount (default: /sys/fs/cgroup)
  PHASE24_STATE_ROOT        PID state directory (default: /run/self-healing-proxy-phase24)
  PHASE24_WORKER_SECONDS    Sleeper lifetime (default: 300)

Exit codes:
  0   success
  2   invalid command/argument
  77  cgroup v2 or required privilege/runtime support unavailable
  other values indicate a real lab failure

Safety:
  This phase does NOT modify cgroup.subtree_control and does NOT write resource
  limits such as memory.max, cpu.max, or pids.max. It only creates one leaf,
  moves one disposable process into it, reads accounting, and removes the leaf.
USAGE
}

log_skip() {
    printf 'SKIP: %s\n' "$*" >&2
    exit 77
}

validate_name() {
    if [[ ! "$CGROUP_NAME" =~ ^shp24-[A-Za-z0-9_.-]+$ ]]; then
        printf 'invalid PHASE24_CGROUP_NAME: %s\n' "$CGROUP_NAME" >&2
        return 2
    fi
}

require_root() {
    if [[ ${EUID} -ne 0 ]]; then
        log_skip "Phase 24 real cgroup setup requires root/CAP_SYS_ADMIN over the cgroup hierarchy"
    fi
}

require_cgroup_v2() {
    [[ -d "$CGROUP_ROOT" ]] || log_skip "cgroup root does not exist: $CGROUP_ROOT"
    [[ -f "$CGROUP_ROOT/cgroup.controllers" ]] || log_skip "unified cgroup v2 hierarchy not detected at $CGROUP_ROOT"
    [[ -w "$CGROUP_ROOT" ]] || log_skip "cgroup v2 root is not writable in this runtime: $CGROUP_ROOT"
}

read_state_pid() {
    if [[ ! -f "$STATE_FILE" ]]; then
        return 1
    fi

    local pid
    pid="$(cat "$STATE_FILE")"
    if [[ ! "$pid" =~ ^[0-9]+$ ]]; then
        printf 'invalid PID state in %s\n' "$STATE_FILE" >&2
        return 1
    fi

    printf '%s\n' "$pid"
}

wait_for_exit() {
    local pid="$1"
    local i

    for i in $(seq 1 50); do
        if [[ ! -d "/proc/$pid" ]]; then
            return 0
        fi
        sleep 0.02
    done

    return 1
}

setup_lab() {
    validate_name
    require_root
    require_cgroup_v2

    if [[ -e "$CGROUP_PATH" || -e "$STATE_FILE" ]]; then
        printf 'Phase 24 lab already exists: %s\n' "$CGROUP_NAME" >&2
        return 1
    fi

    mkdir -p "$STATE_ROOT"

    local mkdir_err
    mkdir_err="$(mktemp)"
    if ! mkdir "$CGROUP_PATH" 2>"$mkdir_err"; then
        local message
        message="$(cat "$mkdir_err" 2>/dev/null || true)"
        rm -f "$mkdir_err"
        log_skip "cannot create disposable cgroup under $CGROUP_ROOT: ${message:-unknown error}"
    fi
    rm -f "$mkdir_err"

    local pid=""
    cleanup_on_error() {
        if [[ -n "$pid" && -d "/proc/$pid" ]]; then
            kill "$pid" 2>/dev/null || true
            wait_for_exit "$pid" || true
        fi
        rmdir "$CGROUP_PATH" 2>/dev/null || true
        rm -f "$STATE_FILE"
    }
    trap cleanup_on_error ERR INT TERM

    sleep "$WORKER_SECONDS" &
    pid=$!

    if ! printf '%s\n' "$pid" > "$CGROUP_PATH/cgroup.procs"; then
        log_skip "cannot move worker PID $pid into $CGROUP_PATH"
    fi

    printf '%s\n' "$pid" > "$STATE_FILE"

    trap - ERR INT TERM

    printf 'PHASE24_SETUP_OK\n'
    printf 'CGROUP_PATH=%s\n' "$CGROUP_PATH"
    printf 'WORKER_PID=%s\n' "$pid"
    printf 'CGROUP_TYPE=%s\n' "$(cat "$CGROUP_PATH/cgroup.type")"
}

verify_lab() {
    validate_name
    require_root
    require_cgroup_v2

    [[ -d "$CGROUP_PATH" ]] || {
        printf 'missing cgroup: %s\n' "$CGROUP_PATH" >&2
        return 1
    }

    local pid
    pid="$(read_state_pid)" || {
        printf 'missing Phase 24 PID state: %s\n' "$STATE_FILE" >&2
        return 1
    }

    [[ -d "/proc/$pid" ]] || {
        printf 'Phase 24 worker is not alive: %s\n' "$pid" >&2
        return 1
    }

    grep -Fxq "$pid" "$CGROUP_PATH/cgroup.procs" || {
        printf 'worker PID %s is not listed in %s/cgroup.procs\n' "$pid" "$CGROUP_PATH" >&2
        return 1
    }

    local expected_path="/${CGROUP_NAME}"
    grep -Fxq "0::${expected_path}" "/proc/$pid/cgroup" || {
        printf 'worker /proc/%s/cgroup does not report %s\n' "$pid" "$expected_path" >&2
        cat "/proc/$pid/cgroup" >&2 || true
        return 1
    }

    local required_file
    for required_file in cgroup.events cgroup.stat cpu.stat memory.current pids.current; do
        [[ -r "$CGROUP_PATH/$required_file" ]] || log_skip "required cgroup v2 accounting file unavailable: $CGROUP_PATH/$required_file"
    done

    local pids_current memory_current cpu_usage
    pids_current="$(cat "$CGROUP_PATH/pids.current")"
    memory_current="$(cat "$CGROUP_PATH/memory.current")"
    cpu_usage="$(awk '$1 == "usage_usec" {print $2}' "$CGROUP_PATH/cpu.stat")"

    [[ "$pids_current" =~ ^[0-9]+$ ]] || {
        printf 'invalid pids.current value: %s\n' "$pids_current" >&2
        return 1
    }
    (( pids_current >= 1 )) || {
        printf 'expected at least one task in pids.current, got %s\n' "$pids_current" >&2
        return 1
    }

    [[ "$memory_current" =~ ^[0-9]+$ ]] || {
        printf 'invalid memory.current value: %s\n' "$memory_current" >&2
        return 1
    }

    [[ "$cpu_usage" =~ ^[0-9]+$ ]] || {
        printf 'cpu.stat did not expose numeric usage_usec\n' >&2
        return 1
    }

    printf 'WORKER_MEMBERSHIP=PASS pid=%s path=%s\n' "$pid" "$expected_path"
    printf 'PIDS_CURRENT=%s\n' "$pids_current"
    printf 'MEMORY_CURRENT=%s\n' "$memory_current"
    printf 'CPU_USAGE_USEC=%s\n' "$cpu_usage"
    printf 'PHASE24_CGROUP_VERIFIED\n'
}

status_lab() {
    validate_name

    printf 'CGROUP_ROOT=%s\n' "$CGROUP_ROOT"
    if [[ -f "$CGROUP_ROOT/cgroup.controllers" ]]; then
        printf 'ROOT_CONTROLLERS=%s\n' "$(cat "$CGROUP_ROOT/cgroup.controllers")"
    else
        printf 'ROOT_CONTROLLERS=<cgroup-v2-not-detected>\n'
    fi

    if [[ -f "$CGROUP_ROOT/cgroup.subtree_control" ]]; then
        printf 'ROOT_SUBTREE_CONTROL=%s\n' "$(cat "$CGROUP_ROOT/cgroup.subtree_control")"
    fi

    if [[ ! -d "$CGROUP_PATH" ]]; then
        printf 'CGROUP_STATE=absent\n'
        return 0
    fi

    printf 'CGROUP_STATE=present\n'
    printf 'CGROUP_PATH=%s\n' "$CGROUP_PATH"
    printf 'CGROUP_TYPE=%s\n' "$(cat "$CGROUP_PATH/cgroup.type" 2>/dev/null || printf '<unavailable>')"
    printf '%s\n' '--- cgroup.procs ---'
    cat "$CGROUP_PATH/cgroup.procs" 2>/dev/null || true

    local pid
    if pid="$(read_state_pid 2>/dev/null)"; then
        printf 'WORKER_PID=%s\n' "$pid"
        printf '%s\n' "--- /proc/${pid}/cgroup ---"
        cat "/proc/$pid/cgroup" 2>/dev/null || printf '<worker-not-running>\n'
    else
        printf 'WORKER_PID=<missing>\n'
    fi

    for file in pids.current memory.current cpu.stat cgroup.events cgroup.stat; do
        if [[ -r "$CGROUP_PATH/$file" ]]; then
            printf '%s\n' "--- ${file} ---"
            cat "$CGROUP_PATH/$file"
        fi
    done
}

cleanup_lab() {
    validate_name
    require_root

    local pid=""
    if pid="$(read_state_pid 2>/dev/null)"; then
        if [[ -d "/proc/$pid" ]]; then
            kill "$pid" 2>/dev/null || true
            if ! wait_for_exit "$pid"; then
                kill -KILL "$pid" 2>/dev/null || true
                wait_for_exit "$pid" || {
                    printf 'unable to stop Phase 24 worker PID %s\n' "$pid" >&2
                    return 1
                }
            fi
        fi
    fi

    rm -f "$STATE_FILE"

    if [[ -d "$CGROUP_PATH" ]]; then
        if [[ -s "$CGROUP_PATH/cgroup.procs" ]]; then
            printf 'refusing to remove non-empty cgroup %s; remaining tasks:\n' "$CGROUP_PATH" >&2
            cat "$CGROUP_PATH/cgroup.procs" >&2
            return 1
        fi
        rmdir "$CGROUP_PATH"
    fi

    if [[ -d "$STATE_ROOT" ]]; then
        rmdir "$STATE_ROOT" 2>/dev/null || true
    fi

    printf 'PHASE24_CLEANUP_OK\n'
}

command="${1:-help}"
case "$command" in
    setup)
        setup_lab
        ;;
    verify)
        verify_lab
        ;;
    status)
        status_lab
        ;;
    cleanup)
        cleanup_lab
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        printf 'unknown command: %s\n' "$command" >&2
        usage >&2
        exit 2
        ;;
esac
