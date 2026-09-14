#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${PHASE34_COLLECTOR_CONFIG:-$ROOT_DIR/observability/otel-collector-config.yaml}"
IMAGE="${PHASE34_COLLECTOR_IMAGE:-otel/opentelemetry-collector:0.156.0}"
NAME="${PHASE34_COLLECTOR_NAME:-shp34-otelcol}"
HOST_PORT="${PHASE34_OTLP_HTTP_PORT:-14318}"
RUNTIME="${PHASE34_CONTAINER_RUNTIME:-}"

usage() {
  cat <<EOF
Usage: $0 {pull|start|status|logs|stop}

Environment overrides:
  PHASE34_CONTAINER_RUNTIME   podman or docker
  PHASE34_COLLECTOR_IMAGE     default: $IMAGE
  PHASE34_COLLECTOR_NAME      default: $NAME
  PHASE34_OTLP_HTTP_PORT      default: $HOST_PORT
  PHASE34_COLLECTOR_CONFIG    default: $CONFIG
EOF
}

choose_runtime() {
  if [[ -n "$RUNTIME" ]]; then
    command -v "$RUNTIME" >/dev/null 2>&1 || {
      echo "ERROR: requested container runtime not found: $RUNTIME" >&2
      exit 1
    }
    return
  fi
  if command -v podman >/dev/null 2>&1; then
    RUNTIME=podman
  elif command -v docker >/dev/null 2>&1; then
    RUNTIME=docker
  else
    echo "ERROR: Phase 34 real Collector test requires podman or docker" >&2
    exit 1
  fi
}

image_present() {
  if [[ "$RUNTIME" == "podman" ]]; then
    "$RUNTIME" image exists "$IMAGE"
  else
    "$RUNTIME" image inspect "$IMAGE" >/dev/null 2>&1
  fi
}

remove_existing() {
  "$RUNTIME" rm -f "$NAME" >/dev/null 2>&1 || true
}

wait_ready() {
  local i
  for i in $(seq 1 60); do
    if python3 - "$HOST_PORT" <<'PY' >/dev/null 2>&1
import socket, sys
s = socket.socket()
s.settimeout(0.1)
try:
    s.connect(("127.0.0.1", int(sys.argv[1])))
finally:
    s.close()
PY
    then
      echo "PHASE34_COLLECTOR_READY endpoint=http://127.0.0.1:${HOST_PORT}/v1/logs image=${IMAGE}"
      return 0
    fi
    if ! "$RUNTIME" inspect "$NAME" >/dev/null 2>&1; then
      echo "ERROR: Collector container exited before readiness" >&2
      return 1
    fi
    sleep 0.1
  done
  echo "ERROR: Collector did not become ready on port ${HOST_PORT}" >&2
  return 1
}

choose_runtime

case "${1:-}" in
  pull)
    "$RUNTIME" pull "$IMAGE"
    ;;
  start)
    [[ -f "$CONFIG" ]] || { echo "ERROR: missing config: $CONFIG" >&2; exit 1; }
    if ! image_present; then
      echo "ERROR: Collector image is not local: $IMAGE" >&2
      echo "Run: $0 pull" >&2
      exit 1
    fi
    remove_existing
    "$RUNTIME" run -d \
      --name "$NAME" \
      -p "127.0.0.1:${HOST_PORT}:4318" \
      -v "${CONFIG}:/etc/otelcol/config.yaml:ro,Z" \
      "$IMAGE" \
      --config=/etc/otelcol/config.yaml >/dev/null
    wait_ready
    ;;
  status)
    if "$RUNTIME" inspect "$NAME" >/dev/null 2>&1; then
      "$RUNTIME" ps --filter "name=${NAME}"
    else
      echo "PHASE34_COLLECTOR_STATE=ABSENT"
      exit 1
    fi
    ;;
  logs)
    "$RUNTIME" logs "$NAME"
    ;;
  stop)
    remove_existing
    echo "PHASE34_COLLECTOR_STOPPED"
    ;;
  *)
    usage
    exit 2
    ;;
esac
