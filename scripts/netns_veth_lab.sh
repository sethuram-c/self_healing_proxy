#!/usr/bin/env bash
set -euo pipefail

SKIP_EXIT=77

NS_EDGE="${PHASE23_NS_EDGE:-shp23-edge}"
NS_BACKEND="${PHASE23_NS_BACKEND:-shp23-backend}"
VETH_EDGE="${PHASE23_VETH_EDGE:-p23-edge0}"
VETH_BACKEND="${PHASE23_VETH_BACKEND:-p23-back0}"
EDGE_CIDR="${PHASE23_EDGE_CIDR:-10.203.0.1/30}"
BACKEND_CIDR="${PHASE23_BACKEND_CIDR:-10.203.0.2/30}"
EDGE_IP="${EDGE_CIDR%/*}"
BACKEND_IP="${BACKEND_CIDR%/*}"

usage() {
    cat <<'USAGE'
Usage: scripts/netns_veth_lab.sh <setup|verify|status|cleanup|help>

Creates a disposable Phase 23 topology:

    namespace shp23-edge                 namespace shp23-backend
    10.203.0.1/30                        10.203.0.2/30
          p23-edge0 <======================> p23-back0
                         veth pair

Commands:
  setup    create namespaces, veth pair, addresses, and bring links up
  verify   prove namespace identity, interface placement, and bidirectional ping
  status   print namespace/link/address state without changing it
  cleanup  delete the disposable topology; safe to run repeatedly
  help     show this help

Environment overrides are available for tests:
  PHASE23_NS_EDGE, PHASE23_NS_BACKEND
  PHASE23_VETH_EDGE, PHASE23_VETH_BACKEND
  PHASE23_EDGE_CIDR, PHASE23_BACKEND_CIDR

setup/verify/cleanup require root (run with sudo). Exit 77 means required
kernel/tooling privileges are unavailable; it is not a successful verification.
USAGE
}

require_tools() {
    local tool
    for tool in ip ping readlink; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "SKIP: required tool '$tool' is unavailable" >&2
            exit "$SKIP_EXIT"
        fi
    done
}

require_root() {
    if [[ ${EUID} -ne 0 ]]; then
        echo "SKIP: Phase 23 network namespace operations require root/CAP_NET_ADMIN" >&2
        exit "$SKIP_EXIT"
    fi
}

validate_names() {
    if (( ${#VETH_EDGE} > 15 || ${#VETH_BACKEND} > 15 )); then
        echo "veth interface names must be at most 15 characters" >&2
        exit 2
    fi
    if [[ "$NS_EDGE" == "$NS_BACKEND" || "$VETH_EDGE" == "$VETH_BACKEND" ]]; then
        echo "namespace and veth endpoint names must be distinct" >&2
        exit 2
    fi
}

namespace_exists() {
    local ns="$1"
    ip netns list | awk '{print $1}' | grep -Fxq -- "$ns"
}

host_link_exists() {
    local link="$1"
    ip link show dev "$link" >/dev/null 2>&1
}

cleanup_topology() {
    # Deleting a network namespace also destroys interfaces that live only there.
    if namespace_exists "$NS_EDGE"; then
        ip netns delete "$NS_EDGE"
    fi
    if namespace_exists "$NS_BACKEND"; then
        ip netns delete "$NS_BACKEND"
    fi

    # Handle a partially-created topology where a veth endpoint never moved.
    if host_link_exists "$VETH_EDGE"; then
        ip link delete "$VETH_EDGE"
    elif host_link_exists "$VETH_BACKEND"; then
        ip link delete "$VETH_BACKEND"
    fi
}

assert_absent() {
    if namespace_exists "$NS_EDGE" || namespace_exists "$NS_BACKEND" \
        || host_link_exists "$VETH_EDGE" || host_link_exists "$VETH_BACKEND"; then
        echo "refusing setup: Phase 23 topology already exists; run cleanup first" >&2
        return 1
    fi
}

setup_topology() {
    require_root
    assert_absent

    local created=0
    rollback() {
        local code=$?
        if (( created != 0 )); then
            cleanup_topology || true
        fi
        exit "$code"
    }
    trap rollback ERR INT TERM

    local ns_error
    if ! ns_error=$(ip netns add "$NS_EDGE" 2>&1); then
        if grep -Eq "Operation not permitted|Permission denied" <<<"$ns_error"; then
            echo "SKIP: named network namespaces unavailable: $ns_error" >&2
            trap - ERR INT TERM
            return "$SKIP_EXIT"
        fi
        echo "$ns_error" >&2
        return 1
    fi
    created=1
    ip netns add "$NS_BACKEND"

    ip link add "$VETH_EDGE" type veth peer name "$VETH_BACKEND"
    ip link set "$VETH_EDGE" netns "$NS_EDGE"
    ip link set "$VETH_BACKEND" netns "$NS_BACKEND"

    ip -n "$NS_EDGE" addr add "$EDGE_CIDR" dev "$VETH_EDGE"
    ip -n "$NS_BACKEND" addr add "$BACKEND_CIDR" dev "$VETH_BACKEND"

    ip -n "$NS_EDGE" link set lo up
    ip -n "$NS_BACKEND" link set lo up
    ip -n "$NS_EDGE" link set "$VETH_EDGE" up
    ip -n "$NS_BACKEND" link set "$VETH_BACKEND" up

    trap - ERR INT TERM

    echo "PHASE23_SETUP_OK"
    echo "EDGE_NAMESPACE=$NS_EDGE EDGE_IF=$VETH_EDGE EDGE_ADDR=$EDGE_CIDR"
    echo "BACKEND_NAMESPACE=$NS_BACKEND BACKEND_IF=$VETH_BACKEND BACKEND_ADDR=$BACKEND_CIDR"
}

require_topology() {
    if ! namespace_exists "$NS_EDGE" || ! namespace_exists "$NS_BACKEND"; then
        echo "Phase 23 topology is incomplete: namespace missing" >&2
        return 1
    fi
    if ! ip -n "$NS_EDGE" link show dev "$VETH_EDGE" >/dev/null 2>&1; then
        echo "Phase 23 topology is incomplete: $VETH_EDGE missing from $NS_EDGE" >&2
        return 1
    fi
    if ! ip -n "$NS_BACKEND" link show dev "$VETH_BACKEND" >/dev/null 2>&1; then
        echo "Phase 23 topology is incomplete: $VETH_BACKEND missing from $NS_BACKEND" >&2
        return 1
    fi
}

print_status() {
    require_root
    require_topology

    echo "== namespaces =="
    ip netns list
    echo "== $NS_EDGE =="
    ip -n "$NS_EDGE" -brief link
    ip -n "$NS_EDGE" -brief address
    ip -n "$NS_EDGE" route
    echo "== $NS_BACKEND =="
    ip -n "$NS_BACKEND" -brief link
    ip -n "$NS_BACKEND" -brief address
    ip -n "$NS_BACKEND" route
}

verify_topology() {
    require_root
    require_topology

    local host_net edge_net backend_net
    host_net=$(readlink /proc/self/ns/net)
    edge_net=$(ip netns exec "$NS_EDGE" readlink /proc/self/ns/net)
    backend_net=$(ip netns exec "$NS_BACKEND" readlink /proc/self/ns/net)

    echo "HOST_NETNS=$host_net"
    echo "EDGE_NETNS=$edge_net"
    echo "BACKEND_NETNS=$backend_net"

    if [[ "$host_net" == "$edge_net" || "$host_net" == "$backend_net" \
        || "$edge_net" == "$backend_net" ]]; then
        echo "network namespace identity verification failed" >&2
        return 1
    fi

    if host_link_exists "$VETH_EDGE" || host_link_exists "$VETH_BACKEND"; then
        echo "veth endpoint leaked into host namespace" >&2
        return 1
    fi

    ip -n "$NS_EDGE" addr show dev "$VETH_EDGE" | grep -Fq "$EDGE_CIDR"
    ip -n "$NS_BACKEND" addr show dev "$VETH_BACKEND" | grep -Fq "$BACKEND_CIDR"

    ip netns exec "$NS_EDGE" ping -c 2 -W 1 "$BACKEND_IP" >/dev/null
    echo "PING_EDGE_TO_BACKEND=PASS"
    ip netns exec "$NS_BACKEND" ping -c 2 -W 1 "$EDGE_IP" >/dev/null
    echo "PING_BACKEND_TO_EDGE=PASS"

    echo "PHASE23_NETNS_VERIFIED"
}

main() {
    require_tools
    validate_names

    case "${1:-}" in
        setup)
            [[ $# -eq 1 ]] || { usage >&2; exit 2; }
            setup_topology
            ;;
        verify)
            [[ $# -eq 1 ]] || { usage >&2; exit 2; }
            verify_topology
            ;;
        status)
            [[ $# -eq 1 ]] || { usage >&2; exit 2; }
            print_status
            ;;
        cleanup)
            [[ $# -eq 1 ]] || { usage >&2; exit 2; }
            require_root
            cleanup_topology
            echo "PHASE23_CLEANUP_OK"
            ;;
        help|--help|-h)
            [[ $# -eq 1 ]] || { usage >&2; exit 2; }
            usage
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
}

main "$@"
