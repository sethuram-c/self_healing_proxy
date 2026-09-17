#!/usr/bin/env bash
set -euo pipefail

IMAGE="${PHASE37_LGTM_IMAGE:-docker.io/grafana/otel-lgtm:0.36.0}"
NAME="${PHASE37_LGTM_NAME:-shp37-lgtm}"
GRAFANA_PORT="${PHASE37_GRAFANA_PORT:-13000}"
OTLP_HTTP_PORT="${PHASE37_OTLP_HTTP_PORT:-14319}"
PROM_PORT="${PHASE37_PROM_PORT:-19090}"
TEMPO_PORT="${PHASE37_TEMPO_PORT:-13200}"
LOKI_PORT="${PHASE37_LOKI_PORT:-13100}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DASH_PROVIDER="$ROOT_DIR/observability/grafana/provisioning/dashboards/shp.yaml"
DASH_JSON="$ROOT_DIR/observability/grafana/dashboards/self-healing-proxy-overview.json"

runtime() {
    if command -v podman >/dev/null 2>&1; then
        printf '%s\n' podman
    elif command -v docker >/dev/null 2>&1; then
        printf '%s\n' docker
    else
        echo "ERROR: Podman or Docker is required for Phase 37" >&2
        return 1
    fi
}

container_exists() {
    local rt="$1"
    if [[ "$rt" == "podman" ]]; then
        "$rt" container exists "$NAME" >/dev/null 2>&1
    else
        "$rt" container inspect "$NAME" >/dev/null 2>&1
    fi
}

wait_http() {
    local url="$1"
    local attempts="${2:-90}"
    local i
    for ((i=0; i<attempts; i++)); do
        if python3 - "$url" <<'PY' >/dev/null 2>&1
import sys, urllib.request
try:
    with urllib.request.urlopen(sys.argv[1], timeout=0.5) as r:
        raise SystemExit(0 if 200 <= r.status < 500 else 1)
except Exception:
    raise SystemExit(1)
PY
        then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

pull() {
    local rt
    rt="$(runtime)"
    "$rt" pull "$IMAGE"
}

stop() {
    local rt
    rt="$(runtime)"
    if container_exists "$rt"; then
        "$rt" rm -f "$NAME" >/dev/null
    fi
    echo "PHASE37_LGTM_STOPPED"
}

start() {
    local rt
    rt="$(runtime)"

    [[ -f "$DASH_PROVIDER" ]] || { echo "ERROR: missing dashboard provider: $DASH_PROVIDER" >&2; exit 1; }
    [[ -f "$DASH_JSON" ]] || { echo "ERROR: missing dashboard JSON: $DASH_JSON" >&2; exit 1; }

    if container_exists "$rt"; then
        "$rt" rm -f "$NAME" >/dev/null
    fi

    local selinux_suffix=""
    if [[ "$rt" == "podman" ]]; then
        selinux_suffix=",Z"
    fi

    "$rt" run -d --name "$NAME" \
        -p "127.0.0.1:${GRAFANA_PORT}:3000" \
        -p "127.0.0.1:${OTLP_HTTP_PORT}:4318" \
        -p "127.0.0.1:${PROM_PORT}:9090" \
        -p "127.0.0.1:${TEMPO_PORT}:3200" \
        -p "127.0.0.1:${LOKI_PORT}:3100" \
        -e GF_SECURITY_ADMIN_USER=admin \
        -e GF_SECURITY_ADMIN_PASSWORD=admin \
        -e GF_USERS_ALLOW_SIGN_UP=false \
        -e GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH=/otel-lgtm/grafana/conf/provisioning/dashboards/custom/self-healing-proxy-overview.json \
        -v "$DASH_PROVIDER:/otel-lgtm/grafana/conf/provisioning/dashboards/shp.yaml:ro${selinux_suffix}" \
        -v "$ROOT_DIR/observability/grafana/dashboards:/otel-lgtm/grafana/conf/provisioning/dashboards/custom:ro${selinux_suffix}" \
        "$IMAGE" >/dev/null

    if ! wait_http "http://127.0.0.1:${GRAFANA_PORT}/api/health" 120; then
        echo "ERROR: Grafana did not become ready" >&2
        "$rt" logs "$NAME" >&2 || true
        exit 1
    fi

    echo "PHASE37_GRAFANA_READY grafana=http://127.0.0.1:${GRAFANA_PORT} otlp=http://127.0.0.1:${OTLP_HTTP_PORT} prometheus=http://127.0.0.1:${PROM_PORT} tempo=http://127.0.0.1:${TEMPO_PORT} loki=http://127.0.0.1:${LOKI_PORT} image=${IMAGE}"
}

status() {
    local rt
    rt="$(runtime)"
    "$rt" ps --filter "name=^${NAME}$"
}

logs() {
    local rt
    rt="$(runtime)"
    "$rt" logs "$NAME"
}

case "${1:-}" in
    pull) pull ;;
    start) start ;;
    stop) stop ;;
    status) status ;;
    logs) logs ;;
    *)
        echo "usage: $0 {pull|start|stop|status|logs}" >&2
        exit 2
        ;;
esac
