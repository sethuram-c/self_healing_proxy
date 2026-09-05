#!/usr/bin/env bash
set -euo pipefail

CGROUP_ROOT="${PHASE25_CGROUP_ROOT:-/sys/fs/cgroup}"
CGROUP_NAME="${PHASE25_CGROUP_NAME:-shp25-memory}"
STATE_ROOT="${PHASE25_STATE_ROOT:-/run/self-healing-proxy-phase25}"
MEMORY_MAX_MIB="${PHASE25_MEMORY_MAX_MIB:-32}"
TARGET_MIB="${PHASE25_TARGET_MIB:-128}"
VERIFY_TIMEOUT_SECONDS="${PHASE25_VERIFY_TIMEOUT_SECONDS:-15}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
WORKER_BIN="${PHASE25_WORKER_BIN:-${REPO_ROOT}/build/memory_pressure}"
CGROUP_PATH="${CGROUP_ROOT}/${CGROUP_NAME}"
STATE_FILE="${STATE_ROOT}/${CGROUP_NAME}.pid"
LOG_FILE="${STATE_ROOT}/${CGROUP_NAME}.log"

usage() {
    cat <<'USAGE'
Usage: cgroup_memory_lab.sh <setup|verify|status|cleanup|help>

Phase 25 cgroup v2 memory-limit and controlled-OOM lab.

Commands:
  setup    Create a disposable memory-limited cgroup and start pressure worker.
  verify   Prove memory.max enforcement and observe memory.events OOM counters.
  status   Display worker state and memory-controller accounting.
  cleanup  Stop any surviving owned worker and remove the disposable cgroup.
  help     Show this help.

Environment:
  PHASE25_CGROUP_NAME              Cgroup name (default: shp25-memory)
  PHASE25_CGROUP_ROOT              cgroup v2 mount (default: /sys/fs/cgroup)
  PHASE25_STATE_ROOT               State directory (default: /run/self-healing-proxy-phase25)
  PHASE25_MEMORY_MAX_MIB           Hard memory limit in MiB (default: 32)
  PHASE25_TARGET_MIB               Worker allocation target in MiB (default: 128)
  PHASE25_WORKER_BIN               Pressure binary (default: build/memory_pressure)
  PHASE25_VERIFY_TIMEOUT_SECONDS   Wait for OOM termination (default: 15)

Exit codes:
  0   success
  2   invalid command/configuration
  77  required cgroup-v2 memory-controller/privilege support unavailable
  other values indicate a real lab failure

Safety:
  The pressure worker starts SIGSTOP-paused, is moved into the cgroup first, and
  is resumed only after memory.max is installed. memory.swap.max is set to 0 when
  available so swap does not hide the hard-limit experiment. This phase does not
  change cpu.max or other controllers and never kills unknown processes.
USAGE
}

log_skip() {
    printf 'SKIP: %s\n' "$*" >&2
    exit 77
}

validate_config() {
    if [[ ! "$CGROUP_NAME" =~ ^shp25-[A-Za-z0-9_.-]+$ ]]; then
        printf 'invalid PHASE25_CGROUP_NAME: %s\n' "$CGROUP_NAME" >&2
        return 2
    fi
    if [[ ! "$MEMORY_MAX_MIB" =~ ^[0-9]+$ ]] || (( MEMORY_MAX_MIB < 8 || MEMORY_MAX_MIB > 4096 )); then
        printf 'invalid PHASE25_MEMORY_MAX_MIB: %s (expected 8..4096)\n' "$MEMORY_MAX_MIB" >&2
        return 2
    fi
    if [[ ! "$TARGET_MIB" =~ ^[0-9]+$ ]] || (( TARGET_MIB <= MEMORY_MAX_MIB || TARGET_MIB > 4096 )); then
        printf 'invalid PHASE25_TARGET_MIB: %s (must be > memory max and <= 4096)\n' "$TARGET_MIB" >&2
        return 2
    fi
    if [[ ! "$VERIFY_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || (( VERIFY_TIMEOUT_SECONDS < 1 || VERIFY_TIMEOUT_SECONDS > 120 )); then
        printf 'invalid PHASE25_VERIFY_TIMEOUT_SECONDS: %s\n' "$VERIFY_TIMEOUT_SECONDS" >&2
        return 2
    fi
}

require_root() {
    if [[ ${EUID} -ne 0 ]]; then
        log_skip "Phase 25 real memory/OOM lab requires root over the cgroup hierarchy"
    fi
}

require_cgroup_v2_memory() {
    [[ -d "$CGROUP_ROOT" ]] || log_skip "cgroup root does not exist: $CGROUP_ROOT"
    [[ -f "$CGROUP_ROOT/cgroup.controllers" ]] || log_skip "unified cgroup v2 hierarchy not detected at $CGROUP_ROOT"
    [[ -w "$CGROUP_ROOT" ]] || log_skip "cgroup v2 root is not writable in this runtime: $CGROUP_ROOT"
    grep -qw memory "$CGROUP_ROOT/cgroup.controllers" || log_skip "memory controller is not available at $CGROUP_ROOT"
}

memory_max_bytes() {
    printf '%s\n' "$(( MEMORY_MAX_MIB * 1024 * 1024 ))"
}

read_state_pid() {
    [[ -f "$STATE_FILE" ]] || return 1
    local pid
    pid="$(cat "$STATE_FILE")"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    printf '%s\n' "$pid"
}

process_is_running() {
    local pid="$1"
    [[ -d "/proc/$pid" ]] || return 1
    local stat
    stat="$(ps -o stat= -p "$pid" 2>/dev/null | tr -d '[:space:]')"
    [[ -n "$stat" && "$stat" != Z* ]]
}

wait_until_stopped() {
    local pid="$1"
    local i
    for i in $(seq 1 100); do
        [[ -d "/proc/$pid" ]] || return 1
        local stat
        stat="$(ps -o stat= -p "$pid" 2>/dev/null | tr -d '[:space:]')"
        [[ "$stat" == T* ]] && return 0
        sleep 0.02
    done
    return 1
}

wait_for_oom_completion() {
    local pid="$1"
    local loops=$(( VERIFY_TIMEOUT_SECONDS * 10 ))
    local i
    for i in $(seq 1 "$loops"); do
        if ! grep -Fxq "$pid" "$CGROUP_PATH/cgroup.procs" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

memory_event_value() {
    local key="$1"
    awk -v key="$key" '$1 == key {print $2}' "$CGROUP_PATH/memory.events"
}

setup_lab() {
    validate_config
    require_root
    require_cgroup_v2_memory

    [[ -x "$WORKER_BIN" ]] || {
        printf 'missing executable memory-pressure worker: %s\n' "$WORKER_BIN" >&2
        return 1
    }

    if [[ -e "$CGROUP_PATH" || -e "$STATE_FILE" ]]; then
        printf 'Phase 25 lab already exists: %s\n' "$CGROUP_NAME" >&2
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
        if [[ -n "$pid" ]] && process_is_running "$pid"; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
        rm -f "$STATE_FILE" "$LOG_FILE"
        rmdir "$CGROUP_PATH" 2>/dev/null || true
        rmdir "$STATE_ROOT" 2>/dev/null || true
    }
    trap cleanup_on_error ERR INT TERM

    [[ -w "$CGROUP_PATH/memory.max" ]] || log_skip "memory.max is unavailable; parent memory controller may not be delegated"

    local max_bytes
    max_bytes="$(memory_max_bytes)"
    printf '%s\n' "$max_bytes" > "$CGROUP_PATH/memory.max"

    if [[ -w "$CGROUP_PATH/memory.swap.max" ]]; then
        printf '0\n' > "$CGROUP_PATH/memory.swap.max"
    fi

    : > "$LOG_FILE"
    "$WORKER_BIN" --mib "$TARGET_MIB" --wait-for-sigcont >"$LOG_FILE" 2>&1 &
    pid=$!

    if ! wait_until_stopped "$pid"; then
        printf 'memory-pressure worker did not enter SIGSTOP state\n' >&2
        return 1
    fi

    printf '%s\n' "$pid" > "$CGROUP_PATH/cgroup.procs"
    printf '%s\n' "$pid" > "$STATE_FILE"

    grep -Fxq "$pid" "$CGROUP_PATH/cgroup.procs" || {
        printf 'worker PID %s was not moved into %s before resume\n' "$pid" "$CGROUP_PATH" >&2
        return 1
    }

    kill -CONT "$pid"
    trap - ERR INT TERM

    printf 'PHASE25_SETUP_OK\n'
    printf 'CGROUP_PATH=%s\n' "$CGROUP_PATH"
    printf 'WORKER_PID=%s\n' "$pid"
    printf 'MEMORY_MAX_BYTES=%s\n' "$max_bytes"
    printf 'TARGET_MIB=%s\n' "$TARGET_MIB"
}

verify_lab() {
    validate_config
    require_root
    require_cgroup_v2_memory

    [[ -d "$CGROUP_PATH" ]] || {
        printf 'missing Phase 25 cgroup: %s\n' "$CGROUP_PATH" >&2
        return 1
    }

    local pid
    pid="$(read_state_pid)" || {
        printf 'missing Phase 25 PID state: %s\n' "$STATE_FILE" >&2
        return 1
    }

    local expected_max actual_max
    expected_max="$(memory_max_bytes)"
    actual_max="$(cat "$CGROUP_PATH/memory.max")"
    [[ "$actual_max" == "$expected_max" ]] || {
        printf 'memory.max mismatch: expected %s got %s\n' "$expected_max" "$actual_max" >&2
        return 1
    }

    if [[ -r "$CGROUP_PATH/memory.swap.max" ]]; then
        local swap_max
        swap_max="$(cat "$CGROUP_PATH/memory.swap.max")"
        [[ "$swap_max" == "0" ]] || {
            printf 'memory.swap.max expected 0, got %s\n' "$swap_max" >&2
            return 1
        }
    fi

    if ! wait_for_oom_completion "$pid"; then
        printf 'worker PID %s still belongs to cgroup after %ss; controlled OOM did not complete\n' \
            "$pid" "$VERIFY_TIMEOUT_SECONDS" >&2
        printf '%s\n' '--- worker log ---' >&2
        cat "$LOG_FILE" >&2 || true
        printf '%s\n' '--- memory.events ---' >&2
        cat "$CGROUP_PATH/memory.events" >&2 || true
        return 1
    fi

    local max_events oom_events oom_kill_events memory_current populated
    max_events="$(memory_event_value max)"
    oom_events="$(memory_event_value oom)"
    oom_kill_events="$(memory_event_value oom_kill)"
    memory_current="$(cat "$CGROUP_PATH/memory.current")"
    populated="$(awk '$1 == "populated" {print $2}' "$CGROUP_PATH/cgroup.events")"

    [[ "$max_events" =~ ^[0-9]+$ ]] && (( max_events >= 1 )) || {
        printf 'expected memory.events max >= 1, got %s\n' "$max_events" >&2
        return 1
    }
    [[ "$oom_events" =~ ^[0-9]+$ ]] && (( oom_events >= 1 )) || {
        printf 'expected memory.events oom >= 1, got %s\n' "$oom_events" >&2
        return 1
    }
    [[ "$oom_kill_events" =~ ^[0-9]+$ ]] && (( oom_kill_events >= 1 )) || {
        printf 'expected memory.events oom_kill >= 1, got %s\n' "$oom_kill_events" >&2
        return 1
    }
    [[ "$memory_current" =~ ^[0-9]+$ ]] || {
        printf 'memory.current is not numeric: %s\n' "$memory_current" >&2
        return 1
    }
    [[ "$populated" == "0" ]] || {
        printf 'expected cgroup.events populated 0 after OOM, got %s\n' "$populated" >&2
        return 1
    }

    grep -q '^MEMORY_PRESSURE_READY ' "$LOG_FILE" || {
        printf 'worker log missing READY marker\n' >&2
        return 1
    }
    grep -q '^MEMORY_PRESSURE_START ' "$LOG_FILE" || {
        printf 'worker log missing START marker\n' >&2
        return 1
    }
    if grep -q '^MEMORY_PRESSURE_COMPLETE ' "$LOG_FILE"; then
        printf 'worker completed allocation instead of being OOM-killed\n' >&2
        return 1
    fi

    printf 'MEMORY_MAX_BYTES=%s\n' "$actual_max"
    printf 'MEMORY_CURRENT_AFTER_OOM=%s\n' "$memory_current"
    printf 'MEMORY_EVENTS_MAX=%s\n' "$max_events"
    printf 'MEMORY_EVENTS_OOM=%s\n' "$oom_events"
    printf 'MEMORY_EVENTS_OOM_KILL=%s\n' "$oom_kill_events"
    printf 'CGROUP_POPULATED=%s\n' "$populated"
    printf 'WORKER_OOM_TERMINATION=PASS pid=%s\n' "$pid"
    printf 'PHASE25_MEMORY_OOM_VERIFIED\n'
}

status_lab() {
    validate_config

    printf 'CGROUP_ROOT=%s\n' "$CGROUP_ROOT"
    printf 'CGROUP_PATH=%s\n' "$CGROUP_PATH"
    printf 'WORKER_BIN=%s\n' "$WORKER_BIN"
    printf 'CONFIG_MEMORY_MAX_MIB=%s\n' "$MEMORY_MAX_MIB"
    printf 'CONFIG_TARGET_MIB=%s\n' "$TARGET_MIB"

    if [[ ! -d "$CGROUP_PATH" ]]; then
        printf 'CGROUP_STATE=absent\n'
        return 0
    fi

    printf 'CGROUP_STATE=present\n'
    local pid
    if pid="$(read_state_pid 2>/dev/null)"; then
        printf 'WORKER_PID=%s\n' "$pid"
        if process_is_running "$pid"; then
            printf 'WORKER_STATE=running\n'
        else
            printf 'WORKER_STATE=exited-or-zombie\n'
        fi
    else
        printf 'WORKER_PID=<missing>\n'
    fi

    local file
    for file in memory.current memory.peak memory.max memory.swap.current memory.swap.max memory.events cgroup.events cgroup.procs; do
        if [[ -r "$CGROUP_PATH/$file" ]]; then
            printf '%s\n' "--- ${file} ---"
            cat "$CGROUP_PATH/$file"
        fi
    done

    if [[ -f "$LOG_FILE" ]]; then
        printf '%s\n' '--- worker log ---'
        cat "$LOG_FILE"
    fi
}

cleanup_lab() {
    validate_config
    require_root

    local pid=""
    if pid="$(read_state_pid 2>/dev/null)"; then
        if process_is_running "$pid"; then
            kill -TERM "$pid" 2>/dev/null || true
            sleep 0.1
            if process_is_running "$pid"; then
                kill -KILL "$pid" 2>/dev/null || true
            fi
        fi
    fi

    if [[ -d "$CGROUP_PATH" ]]; then
        local i
        for i in $(seq 1 50); do
            [[ ! -s "$CGROUP_PATH/cgroup.procs" ]] && break
            sleep 0.02
        done

        if [[ -s "$CGROUP_PATH/cgroup.procs" ]]; then
            printf 'refusing to remove non-empty cgroup %s; remaining tasks:\n' "$CGROUP_PATH" >&2
            cat "$CGROUP_PATH/cgroup.procs" >&2
            return 1
        fi
        rmdir "$CGROUP_PATH"
    fi

    rm -f "$STATE_FILE" "$LOG_FILE"
    if [[ -d "$STATE_ROOT" ]]; then
        rmdir "$STATE_ROOT" 2>/dev/null || true
    fi

    printf 'PHASE25_CLEANUP_OK\n'
}

command="${1:-help}"
case "$command" in
    setup) setup_lab ;;
    verify) verify_lab ;;
    status) status_lab ;;
    cleanup) cleanup_lab ;;
    help|--help|-h) usage ;;
    *)
        printf 'unknown command: %s\n' "$command" >&2
        usage >&2
        exit 2
        ;;
esac
