#!/usr/bin/env bash
set -euo pipefail

CGROUP_ROOT=${PHASE26_CGROUP_ROOT:-/sys/fs/cgroup}
CGROUP_NAME=${PHASE26_CGROUP_NAME:-shp26-cpu}
CPU_QUOTA_US=${PHASE26_CPU_QUOTA_US:-20000}
CPU_PERIOD_US=${PHASE26_CPU_PERIOD_US:-100000}
WORKER_DURATION_MS=${PHASE26_WORKER_DURATION_MS:-30000}
CPU_PRESSURE_BIN=${CPU_PRESSURE_BIN:-build/cpu_pressure}
STATE_ROOT=${PHASE26_STATE_ROOT:-/run/self-healing-proxy-phase26}
VERIFY_WINDOW_SEC=${PHASE26_VERIFY_WINDOW_SEC:-2}

CGROUP_PATH="$CGROUP_ROOT/$CGROUP_NAME"
PID_FILE="$STATE_ROOT/$CGROUP_NAME.pid"
LOG_FILE="$STATE_ROOT/$CGROUP_NAME.log"

usage() {
    cat <<'USAGE'
Usage: cgroup_cpu_lab.sh <setup|verify|status|cleanup|help>

Creates a disposable cgroup v2 CPU-throttling experiment.
Defaults:
  cpu.max             = 20000 100000  (~20% of one CPU)
  worker duration     = 30000 ms
  verification window= 2 seconds

Exit status:
  0   success / verified
  2   invalid command or configuration
  77  cgroup v2 / CPU controller unavailable in this environment
  else runtime failure
USAGE
}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

skip() {
    echo "SKIP: $*" >&2
    exit 77
}

require_safe_name() {
    [[ "$CGROUP_NAME" =~ ^shp26-[A-Za-z0-9_.-]+$ ]] || {
        echo "ERROR: PHASE26_CGROUP_NAME must start with shp26- and contain only safe characters" >&2
        exit 2
    }
}

require_positive_integer() {
    local label=$1
    local value=$2
    [[ "$value" =~ ^[1-9][0-9]*$ ]] || {
        echo "ERROR: $label must be a positive integer" >&2
        exit 2
    }
}

validate_config() {
    require_safe_name
    require_positive_integer PHASE26_CPU_QUOTA_US "$CPU_QUOTA_US"
    require_positive_integer PHASE26_CPU_PERIOD_US "$CPU_PERIOD_US"
    require_positive_integer PHASE26_WORKER_DURATION_MS "$WORKER_DURATION_MS"
    require_positive_integer PHASE26_VERIFY_WINDOW_SEC "$VERIFY_WINDOW_SEC"

    if (( CPU_QUOTA_US >= CPU_PERIOD_US )); then
        echo "ERROR: CPU quota must be smaller than period for a throttling experiment" >&2
        exit 2
    fi
}

require_root() {
    if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
        skip "Phase 26 real cgroup CPU experiment requires root/CAP_SYS_ADMIN-equivalent cgroup access"
    fi
}

require_cgroup_v2() {
    [[ -d "$CGROUP_ROOT" ]] || skip "cgroup root does not exist: $CGROUP_ROOT"
    local fs_type
    fs_type=$(stat -fc %T "$CGROUP_ROOT" 2>/dev/null || true)
    [[ "$fs_type" == "cgroup2fs" ]] || skip "$CGROUP_ROOT is not a cgroup v2 filesystem"
    [[ -w "$CGROUP_ROOT" ]] || skip "cgroup v2 root is not writable in this runtime: $CGROUP_ROOT"
    [[ -r "$CGROUP_ROOT/cgroup.controllers" ]] || skip "cgroup.controllers unavailable"
    grep -qw cpu "$CGROUP_ROOT/cgroup.controllers" || skip "cpu controller unavailable at $CGROUP_ROOT"
}

worker_pid() {
    [[ -r "$PID_FILE" ]] || return 1
    cat "$PID_FILE"
}

pid_in_cgroup() {
    local pid=$1
    grep -qx "$pid" "$CGROUP_PATH/cgroup.procs" 2>/dev/null
}

wait_for_stopped() {
    local pid=$1
    local i state
    for i in $(seq 1 100); do
        [[ -r "/proc/$pid/status" ]] || return 1
        state=$(awk '/^State:/ {print $2}' "/proc/$pid/status")
        [[ "$state" == "T" || "$state" == "t" ]] && return 0
        sleep 0.02
    done
    return 1
}

stat_value() {
    local key=$1
    awk -v k="$key" '$1 == k {print $2; found=1} END {if (!found) exit 1}' "$CGROUP_PATH/cpu.stat"
}

setup_lab() {
    validate_config
    require_root
    require_cgroup_v2

    [[ ! -e "$CGROUP_PATH" ]] || fail "Phase 26 lab already exists: $CGROUP_PATH"
    [[ -x "$CPU_PRESSURE_BIN" ]] || fail "CPU pressure binary not executable: $CPU_PRESSURE_BIN (run make build/cpu_pressure)"

    mkdir -p "$STATE_ROOT"
    mkdir "$CGROUP_PATH"

    if [[ ! -e "$CGROUP_PATH/cpu.max" ]]; then
        rmdir "$CGROUP_PATH" || true
        skip "cpu.max is not available in the new child cgroup; inspect cgroup.subtree_control"
    fi

    printf '%s %s\n' "$CPU_QUOTA_US" "$CPU_PERIOD_US" > "$CGROUP_PATH/cpu.max"

    "$CPU_PRESSURE_BIN" \
        --duration-ms "$WORKER_DURATION_MS" \
        --wait-for-sigcont \
        >"$LOG_FILE" 2>&1 &
    local pid=$!

    if ! wait_for_stopped "$pid"; then
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        rmdir "$CGROUP_PATH" 2>/dev/null || true
        rm -f "$LOG_FILE"
        fail "CPU worker did not reach SIGSTOP-paused state"
    fi

    printf '%s\n' "$pid" > "$CGROUP_PATH/cgroup.procs"
    pid_in_cgroup "$pid" || fail "worker PID $pid was not moved into $CGROUP_PATH"
    printf '%s\n' "$pid" > "$PID_FILE"

    kill -CONT "$pid"

    echo "PHASE26_SETUP_OK"
    echo "CGROUP_PATH=$CGROUP_PATH"
    echo "WORKER_PID=$pid"
    echo "CPU_MAX=$CPU_QUOTA_US $CPU_PERIOD_US"
    echo "WORKER_DURATION_MS=$WORKER_DURATION_MS"
}

verify_lab() {
    validate_config
    require_root
    require_cgroup_v2
    [[ -d "$CGROUP_PATH" ]] || fail "Phase 26 cgroup does not exist: $CGROUP_PATH"
    [[ -r "$PID_FILE" ]] || fail "Phase 26 PID state missing: $PID_FILE"

    local pid before_usage before_periods before_throttled before_throttled_usec
    local after_usage after_periods after_throttled after_throttled_usec
    local delta_usage delta_periods delta_throttled delta_throttled_usec
    pid=$(worker_pid) || fail "cannot read Phase 26 worker PID"

    [[ -d "/proc/$pid" ]] || fail "CPU worker $pid is not alive before verification; rerun setup and verify promptly"
    pid_in_cgroup "$pid" || fail "CPU worker $pid is not in $CGROUP_PATH"

    before_usage=$(stat_value usage_usec) || fail "cpu.stat missing usage_usec"
    before_periods=$(stat_value nr_periods) || fail "cpu.stat missing nr_periods"
    before_throttled=$(stat_value nr_throttled) || fail "cpu.stat missing nr_throttled"
    before_throttled_usec=$(stat_value throttled_usec) || fail "cpu.stat missing throttled_usec"

    sleep "$VERIFY_WINDOW_SEC"

    [[ -d "/proc/$pid" ]] || fail "CPU worker died during throttling verification; cpu.max should throttle, not kill"
    pid_in_cgroup "$pid" || fail "CPU worker left the cgroup during verification"

    after_usage=$(stat_value usage_usec) || fail "cpu.stat missing usage_usec after sample"
    after_periods=$(stat_value nr_periods) || fail "cpu.stat missing nr_periods after sample"
    after_throttled=$(stat_value nr_throttled) || fail "cpu.stat missing nr_throttled after sample"
    after_throttled_usec=$(stat_value throttled_usec) || fail "cpu.stat missing throttled_usec after sample"

    delta_usage=$((after_usage - before_usage))
    delta_periods=$((after_periods - before_periods))
    delta_throttled=$((after_throttled - before_throttled))
    delta_throttled_usec=$((after_throttled_usec - before_throttled_usec))

    (( delta_usage > 0 )) || fail "CPU usage did not increase during verification"
    (( delta_periods > 0 )) || fail "nr_periods did not increase during verification"
    (( delta_throttled > 0 )) || fail "nr_throttled did not increase; cpu.max throttling was not observed"
    (( delta_throttled_usec > 0 )) || fail "throttled_usec did not increase"

    local proc_path
    proc_path=$(awk -F: '$1 == "0" {print $3}' "/proc/$pid/cgroup")
    [[ "$proc_path" == "/$CGROUP_NAME" ]] || fail "worker reports unexpected cgroup path: $proc_path"

    grep -q 'CPU_PRESSURE_START' "$LOG_FILE" || fail "worker log is missing CPU_PRESSURE_START"

    echo "WORKER_MEMBERSHIP=PASS pid=$pid path=$proc_path"
    echo "CPU_STAT_DELTA_USAGE_USEC=$delta_usage"
    echo "CPU_STAT_DELTA_PERIODS=$delta_periods"
    echo "CPU_STAT_DELTA_NR_THROTTLED=$delta_throttled"
    echo "CPU_STAT_DELTA_THROTTLED_USEC=$delta_throttled_usec"
    echo "WORKER_ALIVE_AFTER_THROTTLE=PASS"
    echo "PHASE26_CPU_THROTTLING_VERIFIED"
}

status_lab() {
    validate_config
    require_cgroup_v2

    echo "CGROUP_ROOT=$CGROUP_ROOT"
    echo "ROOT_CONTROLLERS=$(cat "$CGROUP_ROOT/cgroup.controllers" 2>/dev/null || true)"
    echo "ROOT_SUBTREE_CONTROL=$(cat "$CGROUP_ROOT/cgroup.subtree_control" 2>/dev/null || true)"
    echo "CGROUP_PATH=$CGROUP_PATH"

    if [[ ! -d "$CGROUP_PATH" ]]; then
        echo "CGROUP_STATE=absent"
        return 0
    fi

    echo "CGROUP_STATE=present"
    [[ -r "$CGROUP_PATH/cpu.max" ]] && { echo -n "CPU_MAX="; cat "$CGROUP_PATH/cpu.max"; }
    [[ -r "$CGROUP_PATH/cpu.stat" ]] && { echo "CPU_STAT_BEGIN"; cat "$CGROUP_PATH/cpu.stat"; echo "CPU_STAT_END"; }
    [[ -r "$CGROUP_PATH/cgroup.events" ]] && { echo "CGROUP_EVENTS_BEGIN"; cat "$CGROUP_PATH/cgroup.events"; echo "CGROUP_EVENTS_END"; }
    [[ -r "$CGROUP_PATH/cgroup.procs" ]] && { echo "CGROUP_PROCS_BEGIN"; cat "$CGROUP_PATH/cgroup.procs"; echo "CGROUP_PROCS_END"; }
    if [[ -r "$PID_FILE" ]]; then
        echo "RECORDED_PID=$(cat "$PID_FILE")"
    fi
    if [[ -r "$LOG_FILE" ]]; then
        echo "WORKER_LOG_BEGIN"
        cat "$LOG_FILE"
        echo "WORKER_LOG_END"
    fi
}

cleanup_lab() {
    validate_config
    require_root
    require_cgroup_v2

    local pid=""
    if [[ -r "$PID_FILE" ]]; then
        pid=$(cat "$PID_FILE")
    fi

    if [[ -n "$pid" && -d "/proc/$pid" ]]; then
        kill -TERM "$pid" 2>/dev/null || true
        for _ in $(seq 1 100); do
            [[ ! -d "/proc/$pid" ]] && break
            sleep 0.02
        done
        if [[ -d "/proc/$pid" ]]; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi

    rm -f "$PID_FILE" "$LOG_FILE"

    if [[ -d "$CGROUP_PATH" ]]; then
        local remaining
        remaining=$(cat "$CGROUP_PATH/cgroup.procs" 2>/dev/null || true)
        if [[ -n "$remaining" ]]; then
            echo "ERROR: refusing to remove $CGROUP_PATH; unknown tasks remain:" >&2
            echo "$remaining" >&2
            exit 1
        fi
        rmdir "$CGROUP_PATH"
    fi

    rmdir "$STATE_ROOT" 2>/dev/null || true
    echo "PHASE26_CLEANUP_OK"
}

command=${1:-help}
case "$command" in
    setup) setup_lab ;;
    verify) verify_lab ;;
    status) status_lab ;;
    cleanup) cleanup_lab ;;
    help|--help|-h) usage ;;
    *)
        echo "unknown command: $command" >&2
        usage >&2
        exit 2
        ;;
esac
