CC := gcc

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
BENCH_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -DNDEBUG
PERF_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -DNDEBUG

TARGET := build/microproxy
SOURCE := src/main.c

PROCESS_INSPECT := build/process_inspect
NAMESPACE_INSPECT := build/namespace_inspect
PID_MOUNT_LAB := build/pid_mount_lab
NETNS_VETH_LAB := scripts/netns_veth_lab.sh
CGROUP_V2_LAB := scripts/cgroup_v2_lab.sh
MEMORY_PRESSURE := build/memory_pressure
CGROUP_MEMORY_LAB := scripts/cgroup_memory_lab.sh
CPU_PRESSURE := build/cpu_pressure
CGROUP_CPU_LAB := scripts/cgroup_cpu_lab.sh
ISOLATION_LAUNCHER := control-plane/isolation_launcher.py
UDS_SERVER := build/uds_server
UDS_TEST := build/test_uds
SANITIZED_UDS_SERVER := build/uds_server-sanitized
IPC_FRAMED_SERVER := build/ipc_framed_server
IPC_FRAME_TEST := build/test_ipc_frame
SANITIZED_IPC_FRAMED_SERVER := build/ipc_framed_server-sanitized
TELEMETRY_LAB := build/telemetry_nonblocking_lab
TELEMETRY_CHANNEL_TEST := build/test_telemetry_channel
SANITIZED_TELEMETRY_LAB := build/telemetry_nonblocking_lab-sanitized
TRACE_CONTEXT_LAB := build/trace_context_telemetry_lab
TRACE_CONTEXT_TEST := build/test_trace_context
SANITIZED_TRACE_CONTEXT_LAB := build/trace_context_telemetry_lab-sanitized
JSON_LOG_LAB := build/json_log_telemetry_lab
JSON_LOG_TEST := build/test_json_log
SANITIZED_JSON_LOG_LAB := build/json_log_telemetry_lab-sanitized
TRACE_SPAN_LAB := build/trace_span_telemetry_lab
SANITIZED_TRACE_SPAN_LAB := build/trace_span_telemetry_lab-sanitized
TELEMETRY_OVERHEAD_LAB := build/telemetry_overhead_lab
HEALTH_STATE_LAB := build/health_state_lab
BACKEND_HEALTH_TEST := build/test_backend_health
SANITIZED_HEALTH_STATE_LAB := build/health_state_lab-sanitized
SANITIZED_BACKEND_HEALTH_TEST := build/test_backend_health-sanitized
ACTIVE_HEALTH_PROBE_LAB := build/active_health_probe_lab
HEALTH_PROBE_TEST := build/test_health_probe
SANITIZED_ACTIVE_HEALTH_PROBE_LAB := build/active_health_probe_lab-sanitized
SANITIZED_HEALTH_PROBE_TEST := build/test_health_probe-sanitized
PASSIVE_HEALTH_LAB := build/passive_health_lab
PASSIVE_HEALTH_TEST := build/test_passive_health
SANITIZED_PASSIVE_HEALTH_LAB := build/passive_health_lab-sanitized
SANITIZED_PASSIVE_HEALTH_TEST := build/test_passive_health-sanitized
HEALTH_AWARE_SELECTION_LAB := build/health_aware_selection_lab
HEALTH_AWARE_POOL_TEST := build/test_health_aware_pool
SANITIZED_HEALTH_AWARE_SELECTION_LAB := build/health_aware_selection_lab-sanitized
SANITIZED_HEALTH_AWARE_POOL_TEST := build/test_health_aware_pool-sanitized
CIRCUIT_BREAKER_LAB := build/circuit_breaker_lab
CIRCUIT_BREAKER_TEST := build/test_circuit_breaker
SANITIZED_CIRCUIT_BREAKER_LAB := build/circuit_breaker_lab-sanitized
SANITIZED_CIRCUIT_BREAKER_TEST := build/test_circuit_breaker-sanitized
REMEDIATION_POLICY_LAB := build/remediation_policy_lab
REMEDIATION_POLICY_TEST := build/test_remediation_policy
SANITIZED_REMEDIATION_POLICY_LAB := build/remediation_policy_lab-sanitized
SANITIZED_REMEDIATION_POLICY_TEST := build/test_remediation_policy-sanitized
REMEDIATION_EXECUTOR_LAB := build/remediation_executor_lab
REMEDIATION_EXECUTOR_TEST := build/test_remediation_executor
SANITIZED_REMEDIATION_EXECUTOR_LAB := build/remediation_executor_lab-sanitized
SANITIZED_REMEDIATION_EXECUTOR_TEST := build/test_remediation_executor-sanitized
RECOVERY_ORCHESTRATOR_LAB := build/recovery_orchestrator_lab
RECOVERY_ORCHESTRATOR_TEST := build/test_recovery_orchestrator
SANITIZED_RECOVERY_ORCHESTRATOR_LAB := build/recovery_orchestrator_lab-sanitized
SANITIZED_RECOVERY_ORCHESTRATOR_TEST := build/test_recovery_orchestrator-sanitized
HEALING_COORDINATOR_LAB := build/healing_coordinator_lab
HEALING_COORDINATOR_TEST := build/test_healing_coordinator
SANITIZED_HEALING_COORDINATOR_LAB := build/healing_coordinator_lab-sanitized
SANITIZED_HEALING_COORDINATOR_TEST := build/test_healing_coordinator-sanitized
HEALING_EVENT_TEST := build/test_healing_event
SELF_HEALING_FAULT_LAB := build/self_healing_fault_lab
SANITIZED_HEALING_EVENT_TEST := build/test_healing_event-sanitized
SANITIZED_SELF_HEALING_FAULT_LAB := build/self_healing_fault_lab-sanitized
EDGE_AWARE_SELECTION_LAB := build/edge_aware_selection_lab
EDGE_AWARE_POOL_TEST := build/test_edge_aware_pool
SANITIZED_EDGE_AWARE_SELECTION_LAB := build/edge_aware_selection_lab-sanitized
SANITIZED_EDGE_AWARE_POOL_TEST := build/test_edge_aware_pool-sanitized
LATENCY_AWARE_SELECTION_LAB := build/latency_aware_selection_lab
LATENCY_AWARE_POOL_TEST := build/test_latency_aware_pool
SANITIZED_LATENCY_AWARE_SELECTION_LAB := build/latency_aware_selection_lab-sanitized
SANITIZED_LATENCY_AWARE_POOL_TEST := build/test_latency_aware_pool-sanitized
ADAPTIVE_AWARE_SELECTION_LAB := build/adaptive_aware_selection_lab
ADAPTIVE_AWARE_POOL_TEST := build/test_adaptive_aware_pool
SANITIZED_ADAPTIVE_AWARE_SELECTION_LAB := build/adaptive_aware_selection_lab-sanitized
SANITIZED_ADAPTIVE_AWARE_POOL_TEST := build/test_adaptive_aware_pool-sanitized
ADAPTIVE_STABILITY_LAB := build/adaptive_stability_lab
ROUTING_STABILITY_TEST := build/test_routing_stability
SANITIZED_ADAPTIVE_STABILITY_LAB := build/adaptive_stability_lab-sanitized
SANITIZED_ROUTING_STABILITY_TEST := build/test_routing_stability-sanitized
EDGE_ROUTING_PERF_LAB := build/edge_routing_perf_lab
ROUTING_PERF_TEST := build/test_routing_perf
SANITIZED_EDGE_ROUTING_PERF_LAB := build/edge_routing_perf_lab-sanitized
SANITIZED_ROUTING_PERF_TEST := build/test_routing_perf-sanitized
SANITIZED_TELEMETRY_OVERHEAD_LAB := build/telemetry_overhead_lab-sanitized
TCP_LISTENER := build/tcp_listener
IO_BUFFER_TEST := build/test_io_buffer
NONBLOCKING_TEST := build/test_nonblocking
EPOLL_SERVER := build/epoll_server
BENCH_EPOLL := build/epoll_server-bench
SANITIZED_EPOLL := build/epoll_server-sanitized
SANITIZED_TCP := build/tcp_listener-sanitized
CONNECTION_TEST := build/test_connection
BACKEND_POOL_TEST := build/test_backend_pool
HTTP_FRAMING_TEST := build/test_http_framing
MOCK_BACKEND_TEST := tests/integration/test_mock_backends.py

.PHONY: \
	all \
	clean \
	run \
	inspect-process \
	inspect-namespaces \
	pid-mount-lab \
	netns-veth-lab \
	test-netns-veth \
	phase23-gate \
	cgroup-v2-lab \
	test-cgroup-v2 \
	phase24-gate \
	cgroup-memory-lab \
	test-cgroup-memory \
	phase25-gate \
	cgroup-cpu-lab \
	test-cgroup-cpu \
	phase26-gate \
	isolation-launcher \
	test-isolation-launcher \
	phase27-gate \
	test-stage3-hardening \
	phase28-gate \
	stage3-gate \
	uds-server \
	test-uds \
	phase29-gate \
	ipc-framed-server \
	test-ipc-framing \
	phase30-gate \
	telemetry-lab \
	test-nonblocking-telemetry \
	phase31-gate \
	trace-context-lab \
	test-trace-context \
	phase32-gate \
	json-log-lab \
	test-json-logs \
	phase33-gate \
	otel-collector-pull \
	otel-collector-start \
	otel-collector-stop \
	test-otel-bridge \
	check-otel-bridge \
	phase34-real \
	phase34-gate \
	check-otel-metrics \
	test-otel-metrics \
	phase35-real \
	phase35-gate \
	check-otel-traces \
	test-otel-traces \
	sanitize-otel-traces \
	phase36-real \
	phase36-gate \
	grafana-lgtm-pull \
	grafana-lgtm-start \
	grafana-lgtm-stop \
	check-grafana \
	test-grafana \
	phase37-real \
	phase37-gate \
	check-telemetry-overhead \
	test-telemetry-overhead \
	benchmark-telemetry-overhead \
	sanitize-telemetry-overhead \
	phase38-gate \
	test-backend-health \
	health-state-lab \
	sanitize-backend-health \
	phase39-gate \
	test-active-health-probe \
	active-health-probe-lab \
	sanitize-active-health-probe \
	phase40-gate \
	test-passive-health \
	sanitize-passive-health \
	phase41-gate \
	health-aware-selection-lab \
	test-health-aware-selection \
	sanitize-health-aware-selection \
	phase42-gate \
	test-circuit-breaker \
	sanitize-circuit-breaker \
	phase43-gate \
	remediation-policy-lab \
	test-remediation-policy \
	sanitize-remediation-policy \
	phase44-gate \
	remediation-executor-lab \
	test-remediation-executor \
	sanitize-remediation-executor \
	phase45-gate \
	recovery-orchestrator-lab \
	test-recovery-orchestrator \
	sanitize-recovery-orchestrator \
	phase46-gate \
	healing-coordinator-lab \
	test-healing-coordinator \
	sanitize-healing-coordinator \
	phase47-gate \
	self-healing-fault-lab \
	test-self-healing-observability \
	sanitize-self-healing-observability \
	sanitize-latency-aware-selection \
	test-adaptive-aware-selection \
	sanitize-adaptive-aware-selection \
	phase51-gate \
	test-adaptive-stability \
	sanitize-adaptive-stability \
	phase52-gate \
	test-edge-routing-perf \
	sanitize-edge-routing-perf \
	phase53-gate \
	phase48-gate \
	stage5-gate \
	tcp-listener \
	epoll-server \
	mock-backend-a \
	mock-backend-b \
	test \
	test-unit \
	test-integration \
	test-stress \
	sanitize \
	sanitize-main \
	sanitize-buffer \
	sanitize-nonblocking \
	sanitize-backend-pool \
	sanitize-http \
	sanitize-pid-mount \
	sanitize-memory-pressure \
	sanitize-cpu-pressure \
	sanitize-uds \
	sanitize-ipc-framing \
	sanitize-nonblocking-telemetry \
	sanitize-trace-context \
	sanitize-json-logs \
	sanitize-telemetry-overhead \
	sanitize-backend-health \
	sanitize-health-aware-selection \
	sanitize-tcp \
	sanitize-epoll \
	benchmark \
	stage2-gate

all: $(TARGET)

$(TARGET): $(SOURCE)
	mkdir -p build
	$(CC) $(CFLAGS) \
		$(SOURCE) \
		-o $(TARGET)

$(PROCESS_INSPECT): src/process_inspect.c
	mkdir -p build
	$(CC) $(CFLAGS) \
		src/process_inspect.c \
		-o $(PROCESS_INSPECT)

$(NAMESPACE_INSPECT): src/namespace_inspect.c
	mkdir -p build
	$(CC) $(CFLAGS) src/namespace_inspect.c -o $(NAMESPACE_INSPECT)

$(PID_MOUNT_LAB): src/pid_mount_lab.c
	mkdir -p build
	$(CC) $(CFLAGS) src/pid_mount_lab.c -o $(PID_MOUNT_LAB)

$(MEMORY_PRESSURE): src/memory_pressure.c
	mkdir -p build
	$(CC) $(CFLAGS) src/memory_pressure.c -o $(MEMORY_PRESSURE)

$(CPU_PRESSURE): src/cpu_pressure.c
	mkdir -p build
	$(CC) $(CFLAGS) src/cpu_pressure.c -o $(CPU_PRESSURE)

$(BACKEND_HEALTH_TEST): tests/unit/test_backend_health.c src/backend_health.c include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_backend_health.c \
		src/backend_health.c \
		-o $(BACKEND_HEALTH_TEST)

$(HEALTH_STATE_LAB): src/health_state_lab.c src/backend_health.c include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/health_state_lab.c \
		src/backend_health.c \
		-o $(HEALTH_STATE_LAB)

$(HEALTH_PROBE_TEST): tests/unit/test_health_probe.c src/health_probe.c include/health_probe.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_health_probe.c \
		src/health_probe.c \
		-o $(HEALTH_PROBE_TEST)

$(ACTIVE_HEALTH_PROBE_LAB): src/active_health_probe_lab.c src/health_probe.c src/backend_health.c include/health_probe.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/active_health_probe_lab.c \
		src/health_probe.c \
		src/backend_health.c \
		-o $(ACTIVE_HEALTH_PROBE_LAB)

$(PASSIVE_HEALTH_TEST): tests/unit/test_passive_health.c src/passive_health.c src/backend_health.c include/passive_health.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_passive_health.c \
		src/passive_health.c \
		src/backend_health.c \
		-o $(PASSIVE_HEALTH_TEST)

$(PASSIVE_HEALTH_LAB): src/passive_health_lab.c src/passive_health.c src/backend_health.c include/passive_health.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/passive_health_lab.c \
		src/passive_health.c \
		src/backend_health.c \
		-o $(PASSIVE_HEALTH_LAB)

$(UDS_SERVER): src/uds_server.c src/uds.c include/uds.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/uds_server.c \
		src/uds.c \
		-o $(UDS_SERVER)

$(UDS_TEST): tests/unit/test_uds.c src/uds.c include/uds.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_uds.c \
		src/uds.c \
		-o $(UDS_TEST)

$(IPC_FRAMED_SERVER): src/ipc_framed_server.c src/ipc_frame.c src/uds.c include/ipc_frame.h include/uds.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/ipc_framed_server.c \
		src/ipc_frame.c \
		src/uds.c \
		-o $(IPC_FRAMED_SERVER)

$(IPC_FRAME_TEST): tests/unit/test_ipc_frame.c src/ipc_frame.c include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_ipc_frame.c \
		src/ipc_frame.c \
		-o $(IPC_FRAME_TEST)

$(TELEMETRY_LAB): src/telemetry_nonblocking_lab.c src/telemetry_channel.c src/ipc_frame.c include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/telemetry_nonblocking_lab.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(TELEMETRY_LAB)

$(TELEMETRY_CHANNEL_TEST): tests/unit/test_telemetry_channel.c src/telemetry_channel.c src/ipc_frame.c include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_telemetry_channel.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(TELEMETRY_CHANNEL_TEST)


$(TRACE_CONTEXT_LAB): src/trace_context_telemetry_lab.c src/trace_context.c src/telemetry_channel.c src/ipc_frame.c include/trace_context.h include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/trace_context_telemetry_lab.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(TRACE_CONTEXT_LAB)

$(TRACE_CONTEXT_TEST): tests/unit/test_trace_context.c src/trace_context.c include/trace_context.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_trace_context.c \
		src/trace_context.c \
		-o $(TRACE_CONTEXT_TEST)


$(JSON_LOG_LAB): src/json_log_telemetry_lab.c src/json_log.c src/trace_context.c src/telemetry_channel.c src/ipc_frame.c include/json_log.h include/trace_context.h include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/json_log_telemetry_lab.c \
		src/json_log.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(JSON_LOG_LAB)

$(JSON_LOG_TEST): tests/unit/test_json_log.c src/json_log.c src/trace_context.c include/json_log.h include/trace_context.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_json_log.c \
		src/json_log.c \
		src/trace_context.c \
		-o $(JSON_LOG_TEST)

$(TRACE_SPAN_LAB): src/trace_span_telemetry_lab.c src/trace_context.c src/telemetry_channel.c src/ipc_frame.c include/trace_context.h include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/trace_span_telemetry_lab.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(TRACE_SPAN_LAB)

$(TELEMETRY_OVERHEAD_LAB): src/telemetry_overhead_lab.c src/telemetry_channel.c src/ipc_frame.c include/telemetry_channel.h include/ipc_frame.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/telemetry_overhead_lab.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(TELEMETRY_OVERHEAD_LAB)

$(HEALTH_AWARE_POOL_TEST): tests/unit/test_health_aware_pool.c src/health_aware_pool.c src/backend_pool.c src/backend_health.c include/health_aware_pool.h include/backend_pool.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_health_aware_pool.c \
		src/health_aware_pool.c \
		src/backend_pool.c \
		src/backend_health.c \
		-o $(HEALTH_AWARE_POOL_TEST)

$(HEALTH_AWARE_SELECTION_LAB): src/health_aware_selection_lab.c src/health_aware_pool.c src/backend_pool.c src/backend_health.c include/health_aware_pool.h include/backend_pool.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/health_aware_selection_lab.c \
		src/health_aware_pool.c \
		src/backend_pool.c \
		src/backend_health.c \
		-o $(HEALTH_AWARE_SELECTION_LAB)

$(TCP_LISTENER): \
	src/tcp_listener.c \
	src/io_buffer.c \
	include/io_buffer.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/tcp_listener.c \
		src/io_buffer.c \
		-o $(TCP_LISTENER)

$(IO_BUFFER_TEST): \
	tests/unit/test_io_buffer.c \
	src/io_buffer.c \
	include/io_buffer.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_io_buffer.c \
		src/io_buffer.c \
		-o $(IO_BUFFER_TEST)

$(NONBLOCKING_TEST): \
	tests/unit/test_nonblocking.c \
	src/net_utils.c \
	include/net_utils.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_nonblocking.c \
		src/net_utils.c \
		-o $(NONBLOCKING_TEST)

$(EPOLL_SERVER): \
	src/epoll_server.c \
	src/connection.c \
	src/net_utils.c \
	src/io_buffer.c \
	src/http_framing.c \
	src/backend_pool.c \
	include/connection.h \
	include/backend_pool.h \
	include/net_utils.h \
	include/io_buffer.h \
	include/http_framing.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/epoll_server.c \
		src/connection.c \
		src/net_utils.c \
		src/io_buffer.c \
		src/http_framing.c \
		src/backend_pool.c \
		-o $(EPOLL_SERVER)

$(BENCH_EPOLL): \
	src/epoll_server.c \
	src/connection.c \
	src/net_utils.c \
	src/io_buffer.c \
	src/http_framing.c \
	src/backend_pool.c \
	include/connection.h \
	include/backend_pool.h \
	include/net_utils.h \
	include/io_buffer.h \
	include/http_framing.h
	mkdir -p build
	$(CC) $(BENCH_CFLAGS) -Iinclude \
		src/epoll_server.c \
		src/connection.c \
		src/net_utils.c \
		src/io_buffer.c \
		src/http_framing.c \
		src/backend_pool.c \
		-o $(BENCH_EPOLL)

$(BACKEND_POOL_TEST): \
	tests/unit/test_backend_pool.c \
	src/backend_pool.c \
	include/backend_pool.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_backend_pool.c \
		src/backend_pool.c \
		-o $(BACKEND_POOL_TEST)

$(CONNECTION_TEST): \
	tests/unit/test_connection.c \
	src/connection.c \
	src/io_buffer.c \
	include/connection.h \
	include/io_buffer.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_connection.c \
		src/connection.c \
		src/io_buffer.c \
		-o $(CONNECTION_TEST)

$(HTTP_FRAMING_TEST): \
	tests/unit/test_http_framing.c \
	src/http_framing.c \
	include/http_framing.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_http_framing.c \
		src/http_framing.c \
		-o $(HTTP_FRAMING_TEST)

run: all
	./$(TARGET)

inspect-process: $(PROCESS_INSPECT)
	./$(PROCESS_INSPECT)

inspect-namespaces: $(NAMESPACE_INSPECT)
	./$(NAMESPACE_INSPECT)

pid-mount-lab: $(PID_MOUNT_LAB)
	./$(PID_MOUNT_LAB)

netns-veth-lab:
	@echo "Phase 23 topology commands require root; use:"
	@echo "  sudo $(NETNS_VETH_LAB) setup"
	@echo "  sudo $(NETNS_VETH_LAB) verify"
	@echo "  sudo $(NETNS_VETH_LAB) status"
	@echo "  sudo $(NETNS_VETH_LAB) cleanup"


cgroup-v2-lab:
	@echo "Phase 24 cgroup v2 lab commands require root; use:"
	@echo "  sudo $(CGROUP_V2_LAB) setup"
	@echo "  sudo $(CGROUP_V2_LAB) verify"
	@echo "  sudo $(CGROUP_V2_LAB) status"
	@echo "  sudo $(CGROUP_V2_LAB) cleanup"

cgroup-memory-lab: $(MEMORY_PRESSURE)
	@echo "Phase 25 memory/OOM lab commands require root; use:"
	@echo "  sudo $(CGROUP_MEMORY_LAB) setup"
	@echo "  sudo $(CGROUP_MEMORY_LAB) status"
	@echo "  sudo $(CGROUP_MEMORY_LAB) verify"
	@echo "  sudo $(CGROUP_MEMORY_LAB) cleanup"

cgroup-cpu-lab: $(CPU_PRESSURE)
	@echo "Phase 26 CPU quota/throttling lab commands require root; use:"
	@echo "  sudo $(CGROUP_CPU_LAB) setup"
	@echo "  sudo $(CGROUP_CPU_LAB) status"
	@echo "  sudo $(CGROUP_CPU_LAB) verify"
	@echo "  sudo $(CGROUP_CPU_LAB) cleanup"

uds-server: $(UDS_SERVER)
	./$(UDS_SERVER) --socket /tmp/self-healing-proxy-phase29.sock

ipc-framed-server: $(IPC_FRAMED_SERVER)
	./$(IPC_FRAMED_SERVER) --socket /tmp/self-healing-proxy-phase30.sock

isolation-launcher:
	@echo "Phase 27 composed isolation launcher commands require root; use:"
	@echo "  sudo python3 $(ISOLATION_LAUNCHER) start -- /usr/bin/sleep 300"
	@echo "  sudo python3 $(ISOLATION_LAUNCHER) status"
	@echo "  sudo python3 $(ISOLATION_LAUNCHER) verify"
	@echo "  sudo python3 $(ISOLATION_LAUNCHER) cleanup"

tcp-listener: $(TCP_LISTENER)
	./$(TCP_LISTENER)

epoll-server: $(EPOLL_SERVER)
	./$(EPOLL_SERVER)

mock-backend-a:
	python3 control-plane/mock_backend.py \
		--name backend-a \
		--port 9001

mock-backend-b:
	python3 control-plane/mock_backend.py \
		--name backend-b \
		--port 9002

test-unit: \
	$(PID_MOUNT_LAB) \
	$(MEMORY_PRESSURE) \
	$(CPU_PRESSURE) \
	all \
	$(PROCESS_INSPECT) \
	$(NAMESPACE_INSPECT) \
	$(IO_BUFFER_TEST) \
	$(NONBLOCKING_TEST) \
	$(CONNECTION_TEST) \
	$(BACKEND_POOL_TEST) \
	$(HTTP_FRAMING_TEST) \
	$(UDS_TEST) \
	$(IPC_FRAME_TEST) \
	$(TELEMETRY_CHANNEL_TEST) \
	$(TRACE_CONTEXT_TEST) \
	$(JSON_LOG_TEST) \
	$(BACKEND_HEALTH_TEST) \
	$(HEALTH_PROBE_TEST) \
	$(PASSIVE_HEALTH_TEST) \
	$(HEALTH_AWARE_POOL_TEST) \
	$(REMEDIATION_EXECUTOR_TEST) \
	$(RECOVERY_ORCHESTRATOR_TEST) \
	$(HEALING_COORDINATOR_TEST) \
	$(HEALING_EVENT_TEST) \
	$(LATENCY_AWARE_POOL_TEST) \
	$(ADAPTIVE_AWARE_POOL_TEST) \
	$(ROUTING_STABILITY_TEST) \
	$(ROUTING_PERF_TEST)
	python3 tests/unit/test_smoke.py
	python3 tests/unit/test_process_inspect.py
	python3 tests/unit/test_namespace_inspect.py
	python3 tests/unit/test_pid_mount_lab.py
	python3 tests/unit/test_memory_pressure.py
	python3 tests/unit/test_cpu_pressure.py
	python3 tests/unit/test_otel_bridge.py
	python3 tests/unit/test_otel_metrics.py
	python3 tests/unit/test_grafana_dashboard.py
	./$(IO_BUFFER_TEST)
	./$(NONBLOCKING_TEST)
	./$(CONNECTION_TEST)
	./$(BACKEND_POOL_TEST)
	./$(HTTP_FRAMING_TEST)
	./$(UDS_TEST)
	./$(IPC_FRAME_TEST)
	./$(TELEMETRY_CHANNEL_TEST)
	./$(TRACE_CONTEXT_TEST)
	./$(JSON_LOG_TEST)
	./$(BACKEND_HEALTH_TEST)
	./$(HEALTH_PROBE_TEST)
	./$(PASSIVE_HEALTH_TEST)
	./$(HEALTH_AWARE_POOL_TEST)
	./$(REMEDIATION_EXECUTOR_TEST)
	./$(RECOVERY_ORCHESTRATOR_TEST)
	./$(HEALING_COORDINATOR_TEST)
	./$(HEALING_EVENT_TEST)
	./$(LATENCY_AWARE_POOL_TEST)
	./$(ADAPTIVE_AWARE_POOL_TEST)
	./$(ROUTING_STABILITY_TEST)
	./$(ROUTING_PERF_TEST)

test-integration: \
	$(TCP_LISTENER) \
	$(EPOLL_SERVER) \
	$(UDS_SERVER) \
	$(IPC_FRAMED_SERVER) \
	$(TELEMETRY_LAB) \
	$(TRACE_CONTEXT_LAB) \
	$(JSON_LOG_LAB) \
	$(TRACE_SPAN_LAB) \
	$(TELEMETRY_OVERHEAD_LAB) \
	$(HEALTH_STATE_LAB) \
	$(ACTIVE_HEALTH_PROBE_LAB) \
	$(PASSIVE_HEALTH_LAB) \
	$(HEALTH_AWARE_SELECTION_LAB) \
	$(REMEDIATION_EXECUTOR_LAB) \
	$(RECOVERY_ORCHESTRATOR_LAB) \
	$(HEALING_COORDINATOR_LAB) \
	$(SELF_HEALING_FAULT_LAB) \
	$(LATENCY_AWARE_SELECTION_LAB) \
	$(ADAPTIVE_AWARE_SELECTION_LAB)
	python3 tests/integration/test_tcp_listener.py
	python3 tests/integration/test_epoll_server.py
	python3 tests/integration/test_connection_state.py
	python3 $(MOCK_BACKEND_TEST)
	python3 tests/integration/test_http_framing.py
	python3 tests/integration/test_upstream_connect.py
	python3 tests/integration/test_bidirectional_proxy.py
	python3 tests/integration/test_backpressure.py
	python3 tests/integration/test_timeouts.py
	python3 tests/integration/test_round_robin.py
	python3 tests/integration/test_admission_control.py
	python3 tests/integration/test_retry_safety.py
	python3 tests/integration/test_netns_veth.py
	python3 tests/integration/test_cgroup_v2.py
	python3 tests/integration/test_cgroup_memory_oom.py
	python3 tests/integration/test_cgroup_cpu_throttle.py
	python3 tests/integration/test_isolation_launcher.py
	python3 tests/integration/test_stage3_hardening.py
	python3 tests/integration/test_uds_server.py
	python3 tests/integration/test_ipc_framed_server.py
	python3 tests/integration/test_nonblocking_telemetry.py
	python3 tests/integration/test_trace_context_telemetry.py
	python3 tests/integration/test_json_log_telemetry.py
	python3 tests/integration/test_otel_collector_bridge.py
	python3 tests/integration/test_otel_metrics.py
	python3 tests/integration/test_grafana_lgtm.py
	python3 tests/integration/test_telemetry_overhead.py
	python3 tests/integration/test_healing_coordinator.py
	python3 tests/integration/test_health_state_lab.py
	python3 tests/integration/test_active_health_probe.py
	python3 tests/integration/test_passive_health_lab.py
	python3 tests/integration/test_health_aware_selection.py
	python3 tests/integration/test_remediation_executor.py
	python3 tests/integration/test_recovery_orchestrator.py
	python3 tests/integration/test_self_healing_faults.py
	python3 tests/integration/test_latency_aware_selection.py
	python3 tests/integration/test_adaptive_aware_selection.py
	python3 tests/integration/test_adaptive_stability.py

test-stress: $(EPOLL_SERVER)
	python3 tests/stress/test_100_clients.py

test: \
	test-unit \
	test-integration \
	test-stress

benchmark: $(BENCH_EPOLL)
	python3 scripts/benchmark_proxy.py \
		--proxy-bin $(BENCH_EPOLL) \
		--requests 1000 \
		--concurrency 32 \
		--warmup 100 \
		--output benchmarks/phase20_baseline.json

sanitize-main:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) \
		$(SOURCE) \
		-o build/microproxy-sanitized
	./build/microproxy-sanitized

sanitize-buffer:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_io_buffer.c \
		src/io_buffer.c \
		-o build/test_io_buffer-sanitized
	./build/test_io_buffer-sanitized

sanitize-nonblocking:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_nonblocking.c \
		src/net_utils.c \
		-o build/test_nonblocking-sanitized
	./build/test_nonblocking-sanitized

sanitize-backend-pool:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_backend_pool.c \
		src/backend_pool.c \
		-o build/test_backend_pool-sanitized
	./build/test_backend_pool-sanitized

sanitize-http:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_http_framing.c \
		src/http_framing.c \
		-o build/test_http_framing-sanitized
	./build/test_http_framing-sanitized

sanitize-pid-mount:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) src/pid_mount_lab.c -o build/pid_mount_lab-sanitized
	PID_MOUNT_LAB_BIN=build/pid_mount_lab-sanitized python3 tests/unit/test_pid_mount_lab.py

sanitize-memory-pressure:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) src/memory_pressure.c -o build/memory_pressure-sanitized
	MEMORY_PRESSURE_BIN=build/memory_pressure-sanitized python3 tests/unit/test_memory_pressure.py

sanitize-cpu-pressure:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) src/cpu_pressure.c -o build/cpu_pressure-sanitized
	CPU_PRESSURE_BIN=build/cpu_pressure-sanitized python3 tests/unit/test_cpu_pressure.py

sanitize-uds:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_uds.c \
		src/uds.c \
		-o build/test_uds-sanitized
	./build/test_uds-sanitized
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/uds_server.c \
		src/uds.c \
		-o $(SANITIZED_UDS_SERVER)
	UDS_SERVER_BIN=$(SANITIZED_UDS_SERVER) python3 tests/integration/test_uds_server.py

sanitize-ipc-framing:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_ipc_frame.c \
		src/ipc_frame.c \
		-o build/test_ipc_frame-sanitized
	./build/test_ipc_frame-sanitized
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/ipc_framed_server.c \
		src/ipc_frame.c \
		src/uds.c \
		-o $(SANITIZED_IPC_FRAMED_SERVER)
	IPC_FRAMED_SERVER_BIN=$(SANITIZED_IPC_FRAMED_SERVER) python3 tests/integration/test_ipc_framed_server.py

sanitize-nonblocking-telemetry:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_telemetry_channel.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o build/test_telemetry_channel-sanitized
	./build/test_telemetry_channel-sanitized
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/telemetry_nonblocking_lab.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(SANITIZED_TELEMETRY_LAB)
	TELEMETRY_LAB_BIN=$(SANITIZED_TELEMETRY_LAB) python3 tests/integration/test_nonblocking_telemetry.py

sanitize-trace-context:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_trace_context.c \
		src/trace_context.c \
		-o build/test_trace_context-sanitized
	./build/test_trace_context-sanitized
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/trace_context_telemetry_lab.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(SANITIZED_TRACE_CONTEXT_LAB)
	TRACE_CONTEXT_LAB_BIN=$(SANITIZED_TRACE_CONTEXT_LAB) python3 tests/integration/test_trace_context_telemetry.py

sanitize-json-logs:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_json_log.c \
		src/json_log.c \
		src/trace_context.c \
		-o build/test_json_log-sanitized
	./build/test_json_log-sanitized
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/json_log_telemetry_lab.c \
		src/json_log.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(SANITIZED_JSON_LOG_LAB)
	JSON_LOG_LAB_BIN=$(SANITIZED_JSON_LOG_LAB) python3 tests/integration/test_json_log_telemetry.py

sanitize-telemetry-overhead:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/telemetry_overhead_lab.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(SANITIZED_TELEMETRY_OVERHEAD_LAB)
	TELEMETRY_OVERHEAD_LAB_BIN=$(SANITIZED_TELEMETRY_OVERHEAD_LAB) python3 tests/integration/test_telemetry_overhead.py

sanitize-tcp:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/tcp_listener.c \
		src/io_buffer.c \
		-o $(SANITIZED_TCP)
	TCP_LISTENER_BIN=$(SANITIZED_TCP) \
		python3 tests/integration/test_tcp_listener.py

sanitize-epoll:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/epoll_server.c \
		src/connection.c \
		src/http_framing.c \
		src/net_utils.c \
		src/io_buffer.c \
		src/backend_pool.c \
		-o $(SANITIZED_EPOLL)
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_epoll_server.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_connection_state.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_http_framing.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_upstream_connect.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_bidirectional_proxy.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_backpressure.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_timeouts.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_round_robin.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_admission_control.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_retry_safety.py
	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/stress/test_100_clients.py

sanitize: \
	sanitize-main \
	sanitize-buffer \
	sanitize-nonblocking \
	sanitize-backend-pool \
	sanitize-http \
	sanitize-pid-mount \
	sanitize-memory-pressure \
	sanitize-cpu-pressure \
	sanitize-uds \
	sanitize-ipc-framing \
	sanitize-nonblocking-telemetry \
	sanitize-trace-context \
	sanitize-json-logs \
	sanitize-telemetry-overhead \
	sanitize-backend-health \
	sanitize-self-healing-observability \
	sanitize-tcp \
	sanitize-epoll

stage2-gate: test sanitize benchmark
	@echo "Stage 2 gate passed: tests, sanitizers, and benchmark completed."

test-netns-veth:
	python3 tests/integration/test_netns_veth.py

phase23-gate:
	python3 tests/integration/test_netns_veth.py
	sudo env PHASE23_REQUIRE_REAL=1 python3 tests/integration/test_netns_veth.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 23 gate passed: real netns/veth topology plus regressions completed."


test-cgroup-v2:
	python3 tests/integration/test_cgroup_v2.py
	python3 tests/integration/test_cgroup_memory_oom.py

phase24-gate:
	python3 tests/integration/test_cgroup_v2.py
	sudo env PHASE24_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_v2.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 24 gate passed: real cgroup v2 membership/accounting plus regressions completed."


test-cgroup-memory: $(MEMORY_PRESSURE)
	python3 tests/integration/test_cgroup_memory_oom.py

phase25-gate: $(MEMORY_PRESSURE)
	python3 tests/unit/test_memory_pressure.py
	python3 tests/integration/test_cgroup_memory_oom.py
	sudo env PHASE25_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_memory_oom.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 25 gate passed: memory.max enforcement, controlled OOM, and regressions completed."


test-cgroup-cpu: $(CPU_PRESSURE)
	python3 tests/integration/test_cgroup_cpu_throttle.py

phase26-gate: $(CPU_PRESSURE)
	python3 tests/unit/test_cpu_pressure.py
	python3 tests/integration/test_cgroup_cpu_throttle.py
	sudo env PHASE26_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_cpu_throttle.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 26 gate passed: cpu.max throttling, cpu.stat evidence, and regressions completed."

test-isolation-launcher:
	python3 tests/integration/test_isolation_launcher.py

phase27-gate:
	python3 tests/integration/test_isolation_launcher.py
	sudo env PHASE27_REQUIRE_REAL=1 python3 tests/integration/test_isolation_launcher.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 27 gate passed: composed namespace+cgroup launcher plus regressions completed."


test-stage3-hardening:
	python3 tests/integration/test_stage3_hardening.py

phase28-gate:
	python3 tests/integration/test_stage3_hardening.py
	sudo env PHASE28_REQUIRE_REAL=1 python3 tests/integration/test_stage3_hardening.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 28 gate passed: hardened PID/mount/net/cgroup isolation, privilege drop, stale recovery, and regressions completed."

stage3-gate: $(NAMESPACE_INSPECT) $(PID_MOUNT_LAB) $(MEMORY_PRESSURE) $(CPU_PRESSURE)
	python3 tests/unit/test_namespace_inspect.py
	python3 tests/unit/test_pid_mount_lab.py
	sudo env PHASE23_REQUIRE_REAL=1 python3 tests/integration/test_netns_veth.py
	sudo env PHASE24_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_v2.py
	sudo env PHASE25_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_memory_oom.py
	sudo env PHASE26_REQUIRE_REAL=1 python3 tests/integration/test_cgroup_cpu_throttle.py
	sudo env PHASE27_REQUIRE_REAL=1 python3 tests/integration/test_isolation_launcher.py
	sudo env PHASE28_REQUIRE_REAL=1 python3 tests/integration/test_stage3_hardening.py
	$(MAKE) test
	$(MAKE) sanitize
	sudo python3 control-plane/isolation_launcher.py doctor | grep -q '^INSTANCE_STATE=CLEAN$$'
	@echo "Stage 3 gate passed: Linux namespace isolation, cgroup controls, hardened launcher, stale recovery, regressions, and sanitizers completed."


test-uds: $(UDS_SERVER) $(UDS_TEST)
	./$(UDS_TEST)
	python3 tests/integration/test_uds_server.py

phase29-gate: $(UDS_SERVER) $(UDS_TEST)
	./$(UDS_TEST)
	python3 tests/integration/test_uds_server.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 29 gate passed: Unix domain socket lifecycle, credentials, stream behavior, cleanup, regressions, and sanitizers completed."


test-ipc-framing: $(IPC_FRAMED_SERVER) $(IPC_FRAME_TEST)
	./$(IPC_FRAME_TEST)
	python3 tests/integration/test_ipc_framed_server.py

phase30-gate: $(IPC_FRAMED_SERVER) $(IPC_FRAME_TEST)
	./$(IPC_FRAME_TEST)
	python3 tests/integration/test_ipc_framed_server.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 30 gate passed: framed IPC boundaries, fragmentation/coalescing, malformed-frame rejection, regressions, and sanitizers completed."

telemetry-lab: $(TELEMETRY_LAB)
	@echo "Run: ./$(TELEMETRY_LAB) --socket /tmp/collector.sock"

test-nonblocking-telemetry: $(TELEMETRY_LAB) $(TELEMETRY_CHANNEL_TEST)
	./$(TELEMETRY_CHANNEL_TEST)
	python3 tests/integration/test_nonblocking_telemetry.py

phase31-gate: $(TELEMETRY_LAB) $(TELEMETRY_CHANNEL_TEST)
	./$(TELEMETRY_CHANNEL_TEST)
	python3 tests/integration/test_nonblocking_telemetry.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 31 gate passed: bounded nonblocking telemetry, slow/missing collector isolation, regressions, and sanitizers completed."

trace-context-lab: $(TRACE_CONTEXT_LAB)
	@echo "Run: ./$(TRACE_CONTEXT_LAB) --socket /tmp/collector.sock --attempts 3"

test-trace-context: $(TRACE_CONTEXT_LAB) $(TRACE_CONTEXT_TEST)
	./$(TRACE_CONTEXT_TEST)
	python3 tests/integration/test_trace_context_telemetry.py

phase32-gate: $(TRACE_CONTEXT_LAB) $(TRACE_CONTEXT_TEST)
	./$(TRACE_CONTEXT_TEST)
	python3 tests/integration/test_trace_context_telemetry.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 32 gate passed: request/trace identity, traceparent validation, retry correlation, framed telemetry propagation, regressions, and sanitizers completed."

json-log-lab: $(JSON_LOG_LAB)
	@echo "Run: ./$(JSON_LOG_LAB) --socket /tmp/collector.sock --attempts 3"

test-json-logs: $(JSON_LOG_LAB) $(JSON_LOG_TEST)
	./$(JSON_LOG_TEST)
	python3 tests/integration/test_json_log_telemetry.py

phase33-gate: $(JSON_LOG_LAB) $(JSON_LOG_TEST)
	./$(JSON_LOG_TEST)
	python3 tests/integration/test_json_log_telemetry.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 33 gate passed: structured JSON schema, escaping, correlation fields, bounded serialization, regressions, and sanitizers completed."

otel-collector-pull:
	./scripts/otel_collector_lab.sh pull

otel-collector-start:
	./scripts/otel_collector_lab.sh start

otel-collector-stop:
	./scripts/otel_collector_lab.sh stop

check-otel-bridge:
	python3 -m py_compile control-plane/otel_bridge.py tests/unit/test_otel_bridge.py tests/integration/test_otel_collector_bridge.py

test-otel-bridge: $(JSON_LOG_LAB)
	$(MAKE) check-otel-bridge
	python3 tests/unit/test_otel_bridge.py
	python3 tests/integration/test_otel_collector_bridge.py

phase34-real: $(JSON_LOG_LAB)
	PHASE34_REQUIRE_REAL=1 python3 tests/integration/test_otel_collector_bridge.py

phase34-gate: $(JSON_LOG_LAB)
	$(MAKE) check-otel-bridge
	python3 tests/unit/test_otel_bridge.py
	python3 tests/integration/test_otel_collector_bridge.py
	PHASE34_REQUIRE_REAL=1 python3 tests/integration/test_otel_collector_bridge.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 34 gate passed: SHP JSON events converted to OTLP/HTTP, accepted by a real OpenTelemetry Collector, failure isolation, regressions, and sanitizers completed."

check-otel-metrics:
	python3 -m py_compile control-plane/otel_bridge.py tests/unit/test_otel_metrics.py tests/integration/test_otel_metrics.py

test-otel-metrics:
	$(MAKE) check-otel-metrics
	python3 tests/unit/test_otel_metrics.py
	python3 tests/integration/test_otel_metrics.py

phase35-real:
	PHASE35_REQUIRE_REAL=1 python3 tests/integration/test_otel_metrics.py

phase35-gate:
	$(MAKE) check-otel-metrics
	python3 tests/unit/test_otel_metrics.py
	python3 tests/integration/test_otel_metrics.py
	PHASE35_REQUIRE_REAL=1 python3 tests/integration/test_otel_metrics.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 35 gate passed: bounded cumulative OTLP metrics, cardinality control, real Collector acceptance, failure isolation, regressions, and sanitizers completed."

clean:
	rm -rf build


check-otel-traces:
	python3 -m py_compile control-plane/otel_bridge.py tests/unit/test_otel_traces.py tests/integration/test_otel_traces.py

test-otel-traces: $(TRACE_SPAN_LAB) check-otel-traces
	python3 tests/unit/test_otel_traces.py
	python3 tests/integration/test_otel_traces.py

phase36-real: $(TRACE_SPAN_LAB) check-otel-traces
	PHASE36_REQUIRE_REAL=1 python3 tests/integration/test_otel_traces.py

phase36-gate: $(TRACE_SPAN_LAB) check-otel-traces
	python3 tests/unit/test_otel_traces.py
	python3 tests/integration/test_otel_traces.py
	PHASE36_REQUIRE_REAL=1 python3 tests/integration/test_otel_traces.py
	$(MAKE) sanitize-otel-traces
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 36 gate passed: parented OTLP request/backend spans, monotonic timing, remote-parent propagation, real Collector acceptance, failure isolation, regressions, and sanitizers completed."


sanitize-otel-traces:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/trace_span_telemetry_lab.c \
		src/trace_context.c \
		src/telemetry_channel.c \
		src/ipc_frame.c \
		-o $(SANITIZED_TRACE_SPAN_LAB)
	TRACE_SPAN_LAB_BIN=$(SANITIZED_TRACE_SPAN_LAB) python3 tests/integration/test_otel_traces.py


grafana-lgtm-pull:
	./scripts/grafana_lgtm_lab.sh pull

grafana-lgtm-start:
	./scripts/grafana_lgtm_lab.sh start

grafana-lgtm-stop:
	./scripts/grafana_lgtm_lab.sh stop

check-grafana:
	python3 -m py_compile tests/unit/test_grafana_dashboard.py tests/integration/test_grafana_lgtm.py
	python3 -m json.tool observability/grafana/dashboards/self-healing-proxy-overview.json >/dev/null
	bash -n scripts/grafana_lgtm_lab.sh

test-grafana: $(JSON_LOG_LAB) $(TRACE_SPAN_LAB) check-grafana
	python3 tests/unit/test_grafana_dashboard.py
	python3 tests/integration/test_grafana_lgtm.py

phase37-real: $(JSON_LOG_LAB) $(TRACE_SPAN_LAB) check-grafana
	PHASE37_REQUIRE_REAL=1 python3 tests/integration/test_grafana_lgtm.py

phase37-gate: $(JSON_LOG_LAB) $(TRACE_SPAN_LAB) check-grafana
	python3 tests/unit/test_grafana_dashboard.py
	python3 tests/integration/test_grafana_lgtm.py
	PHASE37_REQUIRE_REAL=1 python3 tests/integration/test_grafana_lgtm.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 37 gate passed: provisioned Grafana dashboard queries real Loki/Prometheus/Tempo signals through pinned LGTM, with correlations, regressions, and sanitizers completed."
check-telemetry-overhead:
	python3 -m py_compile tests/integration/test_telemetry_overhead.py scripts/phase38_telemetry_benchmark.py

test-telemetry-overhead: $(TELEMETRY_OVERHEAD_LAB) check-telemetry-overhead
	python3 tests/integration/test_telemetry_overhead.py

benchmark-telemetry-overhead: $(TELEMETRY_OVERHEAD_LAB) check-telemetry-overhead
	python3 scripts/phase38_telemetry_benchmark.py

phase38-gate: $(TELEMETRY_OVERHEAD_LAB) check-telemetry-overhead
	python3 tests/integration/test_telemetry_overhead.py
	python3 scripts/phase38_telemetry_benchmark.py
	PHASE37_REQUIRE_REAL=1 python3 tests/integration/test_grafana_lgtm.py
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 38 gate passed: telemetry latency/CPU/memory budgets, bounded slow/dead collector behavior, real cross-signal observability, regressions, and sanitizers completed."
	@echo "Stage 4 gate passed: nonblocking IPC, correlation, structured logs, OpenTelemetry logs/metrics/traces, Grafana, and measured failure-isolated observability completed."


health-state-lab: $(HEALTH_STATE_LAB)
	./$(HEALTH_STATE_LAB) --scenario trip-recover

test-backend-health: $(BACKEND_HEALTH_TEST) $(HEALTH_STATE_LAB)
	./$(BACKEND_HEALTH_TEST)
	python3 tests/integration/test_health_state_lab.py

sanitize-backend-health:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_backend_health.c src/backend_health.c -o $(SANITIZED_BACKEND_HEALTH_TEST)
	./$(SANITIZED_BACKEND_HEALTH_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/health_state_lab.c src/backend_health.c -o $(SANITIZED_HEALTH_STATE_LAB)
	HEALTH_STATE_LAB_BIN=$(SANITIZED_HEALTH_STATE_LAB) python3 tests/integration/test_health_state_lab.py

phase39-gate: $(BACKEND_HEALTH_TEST) $(HEALTH_STATE_LAB)
	./$(BACKEND_HEALTH_TEST)
	python3 tests/integration/test_health_state_lab.py
	$(MAKE) sanitize-backend-health
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 39 gate passed: explicit backend health states, hysteretic failure/recovery transitions, deterministic timing, regressions, and sanitizers completed."


active-health-probe-lab: $(ACTIVE_HEALTH_PROBE_LAB)
	./$(ACTIVE_HEALTH_PROBE_LAB) --help

test-active-health-probe: $(HEALTH_PROBE_TEST) $(ACTIVE_HEALTH_PROBE_LAB)
	./$(HEALTH_PROBE_TEST)
	python3 tests/integration/test_active_health_probe.py

sanitize-active-health-probe:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_health_probe.c src/health_probe.c -o $(SANITIZED_HEALTH_PROBE_TEST)
	./$(SANITIZED_HEALTH_PROBE_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/active_health_probe_lab.c src/health_probe.c src/backend_health.c \
		-o $(SANITIZED_ACTIVE_HEALTH_PROBE_LAB)
	ACTIVE_HEALTH_PROBE_LAB_BIN=$(SANITIZED_ACTIVE_HEALTH_PROBE_LAB) python3 tests/integration/test_active_health_probe.py

phase40-gate: $(HEALTH_PROBE_TEST) $(ACTIVE_HEALTH_PROBE_LAB)
	./$(HEALTH_PROBE_TEST)
	python3 tests/integration/test_active_health_probe.py
	$(MAKE) sanitize-active-health-probe
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 40 gate passed: nonblocking active TCP probes, bounded deadlines, health-state evidence integration, recovery, regressions, and sanitizers completed."


passive-health-lab: $(PASSIVE_HEALTH_LAB)
	./$(PASSIVE_HEALTH_LAB) --scenario mixed

test-passive-health: $(PASSIVE_HEALTH_TEST) $(PASSIVE_HEALTH_LAB)
	./$(PASSIVE_HEALTH_TEST)
	python3 tests/integration/test_passive_health_lab.py

sanitize-passive-health:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_passive_health.c src/passive_health.c src/backend_health.c \
		-o $(SANITIZED_PASSIVE_HEALTH_TEST)
	./$(SANITIZED_PASSIVE_HEALTH_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/passive_health_lab.c src/passive_health.c src/backend_health.c \
		-o $(SANITIZED_PASSIVE_HEALTH_LAB)
	PASSIVE_HEALTH_LAB_BIN=$(SANITIZED_PASSIVE_HEALTH_LAB) python3 tests/integration/test_passive_health_lab.py

phase41-gate: $(PASSIVE_HEALTH_TEST) $(PASSIVE_HEALTH_LAB)
	./$(PASSIVE_HEALTH_TEST)
	python3 tests/integration/test_passive_health_lab.py
	$(MAKE) sanitize-passive-health
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 41 gate passed: request-path passive health classification, single-count attempt evidence, client/local neutrality, hysteretic recovery, regressions, and sanitizers completed."

health-aware-selection-lab: $(HEALTH_AWARE_SELECTION_LAB)
	./$(HEALTH_AWARE_SELECTION_LAB) --scenario skip

test-health-aware-selection: $(HEALTH_AWARE_POOL_TEST) $(HEALTH_AWARE_SELECTION_LAB)
	./$(HEALTH_AWARE_POOL_TEST)
	python3 tests/integration/test_health_aware_selection.py

sanitize-health-aware-selection:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_health_aware_pool.c src/health_aware_pool.c src/backend_pool.c src/backend_health.c \
		-o $(SANITIZED_HEALTH_AWARE_POOL_TEST)
	./$(SANITIZED_HEALTH_AWARE_POOL_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/health_aware_selection_lab.c src/health_aware_pool.c src/backend_pool.c src/backend_health.c \
		-o $(SANITIZED_HEALTH_AWARE_SELECTION_LAB)
	HEALTH_AWARE_SELECTION_LAB_BIN=$(SANITIZED_HEALTH_AWARE_SELECTION_LAB) python3 tests/integration/test_health_aware_selection.py

phase42-gate: $(HEALTH_AWARE_POOL_TEST) $(HEALTH_AWARE_SELECTION_LAB)
	./$(HEALTH_AWARE_POOL_TEST)
	python3 tests/integration/test_health_aware_selection.py
	$(MAKE) sanitize-health-aware-selection
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 42 gate passed: health-aware round robin skips non-eligible backends, preserves capacity/fairness, fails closed when none are eligible, reintegrates recovered backends, regressions, and sanitizers completed."



$(CIRCUIT_BREAKER_TEST): tests/unit/test_circuit_breaker.c src/circuit_breaker.c include/circuit_breaker.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_circuit_breaker.c src/circuit_breaker.c -o $(CIRCUIT_BREAKER_TEST)

$(CIRCUIT_BREAKER_LAB): src/circuit_breaker_lab.c src/circuit_breaker.c src/circuit_aware_pool.c src/backend_pool.c src/backend_health.c include/circuit_breaker.h include/circuit_aware_pool.h include/backend_pool.h include/backend_health.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/circuit_breaker_lab.c src/circuit_breaker.c src/circuit_aware_pool.c src/backend_pool.c src/backend_health.c \
		-o $(CIRCUIT_BREAKER_LAB)

circuit-breaker-lab: $(CIRCUIT_BREAKER_LAB)
	./$(CIRCUIT_BREAKER_LAB) --scenario open-skip

test-circuit-breaker: $(CIRCUIT_BREAKER_TEST) $(CIRCUIT_BREAKER_LAB)
	./$(CIRCUIT_BREAKER_TEST)
	python3 tests/integration/test_circuit_breaker_lab.py

sanitize-circuit-breaker:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_circuit_breaker.c src/circuit_breaker.c -o $(SANITIZED_CIRCUIT_BREAKER_TEST)
	./$(SANITIZED_CIRCUIT_BREAKER_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/circuit_breaker_lab.c src/circuit_breaker.c src/circuit_aware_pool.c src/backend_pool.c src/backend_health.c \
		-o $(SANITIZED_CIRCUIT_BREAKER_LAB)
	CIRCUIT_BREAKER_LAB_BIN=$(SANITIZED_CIRCUIT_BREAKER_LAB) python3 tests/integration/test_circuit_breaker_lab.py

phase43-gate: $(CIRCUIT_BREAKER_TEST) $(CIRCUIT_BREAKER_LAB)
	./$(CIRCUIT_BREAKER_TEST)
	python3 tests/integration/test_circuit_breaker_lab.py
	$(MAKE) sanitize-circuit-breaker
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 43 gate passed: bounded failure windows open circuits, open circuits reject new traffic, half-open permits one controlled trial, successful trials close safely, regressions, and sanitizers completed."


$(REMEDIATION_POLICY_TEST): tests/unit/test_remediation_policy.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c include/remediation_policy.h include/backend_health.h include/circuit_breaker.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_remediation_policy.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c \
		-o $(REMEDIATION_POLICY_TEST)

$(REMEDIATION_POLICY_LAB): src/remediation_policy_lab.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c include/remediation_policy.h include/backend_health.h include/circuit_breaker.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/remediation_policy_lab.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c \
		-o $(REMEDIATION_POLICY_LAB)

remediation-policy-lab: $(REMEDIATION_POLICY_LAB)
	./$(REMEDIATION_POLICY_LAB) --scenario backoff

test-remediation-policy: $(REMEDIATION_POLICY_TEST) $(REMEDIATION_POLICY_LAB)
	./$(REMEDIATION_POLICY_TEST)
	python3 tests/integration/test_remediation_policy_lab.py

sanitize-remediation-policy:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_remediation_policy.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c \
		-o $(SANITIZED_REMEDIATION_POLICY_TEST)
	./$(SANITIZED_REMEDIATION_POLICY_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/remediation_policy_lab.c src/remediation_policy.c src/backend_health.c src/circuit_breaker.c \
		-o $(SANITIZED_REMEDIATION_POLICY_LAB)
	REMEDIATION_POLICY_LAB_BIN=$(SANITIZED_REMEDIATION_POLICY_LAB) python3 tests/integration/test_remediation_policy_lab.py

phase44-gate: $(REMEDIATION_POLICY_TEST) $(REMEDIATION_POLICY_LAB)
	./$(REMEDIATION_POLICY_TEST)
	python3 tests/integration/test_remediation_policy_lab.py
	$(MAKE) sanitize-remediation-policy
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 44 gate passed: remediation requires strong evidence, applies capped exponential backoff, enforces rolling attempt budgets, resets safely after recovery, regressions, and sanitizers completed."


$(REMEDIATION_EXECUTOR_TEST): tests/unit/test_remediation_executor.c src/remediation_executor.c include/remediation_executor.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_remediation_executor.c src/remediation_executor.c \
		-o $(REMEDIATION_EXECUTOR_TEST)

$(REMEDIATION_EXECUTOR_LAB): src/remediation_executor_lab.c src/remediation_executor.c include/remediation_executor.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/remediation_executor_lab.c src/remediation_executor.c \
		-o $(REMEDIATION_EXECUTOR_LAB)

remediation-executor-lab: $(REMEDIATION_EXECUTOR_LAB)
	./$(REMEDIATION_EXECUTOR_LAB) --scenario graceful

test-remediation-executor: $(REMEDIATION_EXECUTOR_TEST) $(REMEDIATION_EXECUTOR_LAB)
	./$(REMEDIATION_EXECUTOR_TEST)
	python3 tests/integration/test_remediation_executor.py

sanitize-remediation-executor:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_remediation_executor.c src/remediation_executor.c \
		-o $(SANITIZED_REMEDIATION_EXECUTOR_TEST)
	./$(SANITIZED_REMEDIATION_EXECUTOR_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/remediation_executor_lab.c src/remediation_executor.c \
		-o $(SANITIZED_REMEDIATION_EXECUTOR_LAB)
	REMEDIATION_EXECUTOR_LAB_BIN=$(SANITIZED_REMEDIATION_EXECUTOR_LAB) python3 tests/integration/test_remediation_executor.py

phase45-gate: $(REMEDIATION_EXECUTOR_TEST) $(REMEDIATION_EXECUTOR_LAB)
	./$(REMEDIATION_EXECUTOR_TEST)
	python3 tests/integration/test_remediation_executor.py
	$(MAKE) sanitize-remediation-executor
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 45 gate passed: approved remediation restarts exact process identities with pidfds, bounded SIGTERM/SIGKILL lifecycle control, verified replacement exec, stale-PID refusal, regressions, and sanitizers completed."


$(RECOVERY_ORCHESTRATOR_TEST): tests/unit/test_recovery_orchestrator.c src/recovery_orchestrator.c src/backend_health.c src/circuit_breaker.c include/recovery_orchestrator.h include/backend_health.h include/circuit_breaker.h include/health_probe.h include/remediation_executor.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_recovery_orchestrator.c src/recovery_orchestrator.c src/backend_health.c src/circuit_breaker.c \
		-o $(RECOVERY_ORCHESTRATOR_TEST)

$(RECOVERY_ORCHESTRATOR_LAB): src/recovery_orchestrator_lab.c src/recovery_orchestrator.c src/remediation_executor.c src/health_probe.c src/backend_health.c src/circuit_breaker.c include/recovery_orchestrator.h include/remediation_executor.h include/health_probe.h include/backend_health.h include/circuit_breaker.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/recovery_orchestrator_lab.c src/recovery_orchestrator.c src/remediation_executor.c src/health_probe.c src/backend_health.c src/circuit_breaker.c \
		-o $(RECOVERY_ORCHESTRATOR_LAB)

recovery-orchestrator-lab: $(RECOVERY_ORCHESTRATOR_LAB)
	./$(RECOVERY_ORCHESTRATOR_LAB) --scenario ready

test-recovery-orchestrator: $(RECOVERY_ORCHESTRATOR_TEST) $(RECOVERY_ORCHESTRATOR_LAB)
	./$(RECOVERY_ORCHESTRATOR_TEST)
	python3 tests/integration/test_recovery_orchestrator.py

sanitize-recovery-orchestrator:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_recovery_orchestrator.c src/recovery_orchestrator.c src/backend_health.c src/circuit_breaker.c \
		-o $(SANITIZED_RECOVERY_ORCHESTRATOR_TEST)
	./$(SANITIZED_RECOVERY_ORCHESTRATOR_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/recovery_orchestrator_lab.c src/recovery_orchestrator.c src/remediation_executor.c src/health_probe.c src/backend_health.c src/circuit_breaker.c \
		-o $(SANITIZED_RECOVERY_ORCHESTRATOR_LAB)
	RECOVERY_ORCHESTRATOR_LAB_BIN=$(SANITIZED_RECOVERY_ORCHESTRATOR_LAB) python3 tests/integration/test_recovery_orchestrator.py

phase46-gate: $(RECOVERY_ORCHESTRATOR_TEST) $(RECOVERY_ORCHESTRATOR_LAB)
	./$(RECOVERY_ORCHESTRATOR_TEST)
	python3 tests/integration/test_recovery_orchestrator.py
	$(MAKE) sanitize-recovery-orchestrator
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 46 gate passed: restarted backends remain quarantined until bounded real readiness probes satisfy health/circuit recovery, startup deadlines fail closed, regressions, and sanitizers completed."

$(HEALING_COORDINATOR_TEST): tests/unit/test_healing_coordinator.c src/healing_coordinator.c include/healing_coordinator.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_healing_coordinator.c src/healing_coordinator.c \
		-o $(HEALING_COORDINATOR_TEST)

$(HEALING_COORDINATOR_LAB): src/healing_coordinator_lab.c src/healing_coordinator.c include/healing_coordinator.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/healing_coordinator_lab.c src/healing_coordinator.c \
		-o $(HEALING_COORDINATOR_LAB)

healing-coordinator-lab: $(HEALING_COORDINATOR_LAB)
	./$(HEALING_COORDINATOR_LAB) --scenario concurrency

test-healing-coordinator: $(HEALING_COORDINATOR_TEST) $(HEALING_COORDINATOR_LAB)
	./$(HEALING_COORDINATOR_TEST)
	python3 tests/integration/test_healing_coordinator.py

sanitize-healing-coordinator:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_healing_coordinator.c src/healing_coordinator.c \
		-o $(SANITIZED_HEALING_COORDINATOR_TEST)
	./$(SANITIZED_HEALING_COORDINATOR_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/healing_coordinator_lab.c src/healing_coordinator.c \
		-o $(SANITIZED_HEALING_COORDINATOR_LAB)
	HEALING_COORDINATOR_LAB_BIN=$(SANITIZED_HEALING_COORDINATOR_LAB) python3 tests/integration/test_healing_coordinator.py

phase47-gate: $(HEALING_COORDINATOR_TEST) $(HEALING_COORDINATOR_LAB)
	./$(HEALING_COORDINATOR_TEST)
	python3 tests/integration/test_healing_coordinator.py
	$(MAKE) sanitize-healing-coordinator
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 47 gate passed: fleet-wide healing enforces global concurrency and rolling restart budgets, preserves fair remediation admission, coalesces duplicate requests, regressions, and sanitizers completed."


$(HEALING_EVENT_TEST): tests/unit/test_healing_event.c src/healing_event.c include/healing_event.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		tests/unit/test_healing_event.c src/healing_event.c \
		-o $(HEALING_EVENT_TEST)

$(SELF_HEALING_FAULT_LAB): src/self_healing_fault_lab.c src/healing_event.c src/backend_health.c src/circuit_breaker.c src/remediation_policy.c src/healing_coordinator.c src/recovery_orchestrator.c include/healing_event.h include/backend_health.h include/circuit_breaker.h include/remediation_policy.h include/healing_coordinator.h include/recovery_orchestrator.h include/health_probe.h include/remediation_executor.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/self_healing_fault_lab.c src/healing_event.c src/backend_health.c src/circuit_breaker.c src/remediation_policy.c src/healing_coordinator.c src/recovery_orchestrator.c \
		-o $(SELF_HEALING_FAULT_LAB)

self-healing-fault-lab: $(SELF_HEALING_FAULT_LAB)
	./$(SELF_HEALING_FAULT_LAB) --scenario recover

test-self-healing-observability: $(HEALING_EVENT_TEST) $(SELF_HEALING_FAULT_LAB)
	./$(HEALING_EVENT_TEST)
	python3 tests/integration/test_self_healing_faults.py

sanitize-self-healing-observability:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_healing_event.c src/healing_event.c \
		-o $(SANITIZED_HEALING_EVENT_TEST)
	./$(SANITIZED_HEALING_EVENT_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/self_healing_fault_lab.c src/healing_event.c src/backend_health.c src/circuit_breaker.c src/remediation_policy.c src/healing_coordinator.c src/recovery_orchestrator.c \
		-o $(SANITIZED_SELF_HEALING_FAULT_LAB)
	SELF_HEALING_FAULT_LAB_BIN=$(SANITIZED_SELF_HEALING_FAULT_LAB) python3 tests/integration/test_self_healing_faults.py

phase48-gate: $(HEALING_EVENT_TEST) $(SELF_HEALING_FAULT_LAB)
	$(MAKE) test-backend-health
	$(MAKE) test-active-health-probe
	$(MAKE) test-passive-health
	$(MAKE) test-health-aware-selection
	$(MAKE) test-circuit-breaker
	$(MAKE) test-remediation-policy
	$(MAKE) test-remediation-executor
	$(MAKE) test-recovery-orchestrator
	$(MAKE) test-healing-coordinator
	$(MAKE) test-self-healing-observability
	$(MAKE) sanitize-backend-health
	$(MAKE) sanitize-active-health-probe
	$(MAKE) sanitize-passive-health
	$(MAKE) sanitize-health-aware-selection
	$(MAKE) sanitize-circuit-breaker
	$(MAKE) sanitize-remediation-policy
	$(MAKE) sanitize-remediation-executor
	$(MAKE) sanitize-recovery-orchestrator
	$(MAKE) sanitize-healing-coordinator
	$(MAKE) sanitize-self-healing-observability
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 48 gate passed: self-healing emits structured end-to-end events, fault injection proves containment/remediation/recovery and restart-storm suppression, regressions, and sanitizers completed."
	@echo "Stage 5 gate passed: health detection, active/passive evidence, health-aware routing, circuit breaking, bounded remediation, safe restart, supervised recovery, fleet coordination, and observable fault-injected self-healing completed."

stage5-gate: phase48-gate


$(EDGE_AWARE_POOL_TEST): tests/unit/test_edge_aware_pool.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/edge_aware_pool.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_edge_aware_pool.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(EDGE_AWARE_POOL_TEST)

$(EDGE_AWARE_SELECTION_LAB): src/edge_aware_selection_lab.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/edge_aware_pool.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude src/edge_aware_selection_lab.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(EDGE_AWARE_SELECTION_LAB)

test-edge-aware-selection: $(EDGE_AWARE_POOL_TEST) $(EDGE_AWARE_SELECTION_LAB)
	./$(EDGE_AWARE_POOL_TEST)
	python3 tests/integration/test_edge_aware_selection.py

sanitize-edge-aware-selection:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_edge_aware_pool.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_EDGE_AWARE_POOL_TEST)
	./$(SANITIZED_EDGE_AWARE_POOL_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/edge_aware_selection_lab.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_EDGE_AWARE_SELECTION_LAB)
	EDGE_AWARE_SELECTION_LAB_BIN=$(SANITIZED_EDGE_AWARE_SELECTION_LAB) python3 tests/integration/test_edge_aware_selection.py

phase49-gate: $(EDGE_AWARE_POOL_TEST) $(EDGE_AWARE_SELECTION_LAB)
	$(MAKE) test-edge-aware-selection
	$(MAKE) sanitize-edge-aware-selection
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 49 gate passed: explicit region/zone/site metadata prefers nearest eligible backends, preserves fair selection within locality tiers, falls back safely across locality boundaries, regressions, and sanitizers completed."


$(LATENCY_AWARE_POOL_TEST): tests/unit/test_latency_aware_pool.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/latency_aware_pool.h include/latency_signal.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_latency_aware_pool.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(LATENCY_AWARE_POOL_TEST)

$(LATENCY_AWARE_SELECTION_LAB): src/latency_aware_selection_lab.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/latency_aware_pool.h include/latency_signal.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude src/latency_aware_selection_lab.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(LATENCY_AWARE_SELECTION_LAB)

test-latency-aware-selection: $(LATENCY_AWARE_POOL_TEST) $(LATENCY_AWARE_SELECTION_LAB)
	./$(LATENCY_AWARE_POOL_TEST)
	python3 tests/integration/test_latency_aware_selection.py

sanitize-latency-aware-selection:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_latency_aware_pool.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_LATENCY_AWARE_POOL_TEST)
	./$(SANITIZED_LATENCY_AWARE_POOL_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/latency_aware_selection_lab.c src/latency_aware_pool.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_LATENCY_AWARE_SELECTION_LAB)
	LATENCY_AWARE_SELECTION_LAB_BIN=$(SANITIZED_LATENCY_AWARE_SELECTION_LAB) python3 tests/integration/test_latency_aware_selection.py

phase50-gate: $(LATENCY_AWARE_POOL_TEST) $(LATENCY_AWARE_SELECTION_LAB)
	$(MAKE) test-latency-aware-selection
	$(MAKE) sanitize-latency-aware-selection
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 50 gate passed: bounded fresh EWMA latency signals refine selection within the best safe locality tier, stale data loses authority, near-equal candidates preserve fairness, regressions, and sanitizers completed."


$(ADAPTIVE_AWARE_POOL_TEST): tests/unit/test_adaptive_aware_pool.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/adaptive_aware_pool.h include/adaptive_score.h include/latency_signal.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_adaptive_aware_pool.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(ADAPTIVE_AWARE_POOL_TEST)

$(ADAPTIVE_AWARE_SELECTION_LAB): src/adaptive_aware_selection_lab.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/adaptive_aware_pool.h include/adaptive_score.h include/latency_signal.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude src/adaptive_aware_selection_lab.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(ADAPTIVE_AWARE_SELECTION_LAB)

test-adaptive-aware-selection: $(ADAPTIVE_AWARE_POOL_TEST) $(ADAPTIVE_AWARE_SELECTION_LAB)
	./$(ADAPTIVE_AWARE_POOL_TEST)
	python3 tests/integration/test_adaptive_aware_selection.py

sanitize-adaptive-aware-selection:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_adaptive_aware_pool.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_ADAPTIVE_AWARE_POOL_TEST)
	./$(SANITIZED_ADAPTIVE_AWARE_POOL_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/adaptive_aware_selection_lab.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_ADAPTIVE_AWARE_SELECTION_LAB)
	ADAPTIVE_AWARE_SELECTION_LAB_BIN=$(SANITIZED_ADAPTIVE_AWARE_SELECTION_LAB) python3 tests/integration/test_adaptive_aware_selection.py

phase51-gate: $(ADAPTIVE_AWARE_POOL_TEST) $(ADAPTIVE_AWARE_SELECTION_LAB)
	$(MAKE) test-adaptive-aware-selection
	$(MAKE) sanitize-adaptive-aware-selection
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 51 gate passed: normalized latency/load scoring adapts within the best safe locality tier, stale latency loses influence, near-equal scores preserve fairness, regressions, and sanitizers completed."


$(ROUTING_STABILITY_TEST): tests/unit/test_routing_stability.c src/routing_stability.c include/routing_stability.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_routing_stability.c src/routing_stability.c -o $(ROUTING_STABILITY_TEST)

$(ADAPTIVE_STABILITY_LAB): src/adaptive_stability_lab.c src/routing_stability.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/routing_stability.h include/adaptive_aware_pool.h include/adaptive_score.h include/latency_signal.h include/edge_locality.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude src/adaptive_stability_lab.c src/routing_stability.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(ADAPTIVE_STABILITY_LAB)

test-adaptive-stability: $(ROUTING_STABILITY_TEST) $(ADAPTIVE_STABILITY_LAB)
	./$(ROUTING_STABILITY_TEST)
	python3 tests/integration/test_adaptive_stability.py

sanitize-adaptive-stability:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_routing_stability.c src/routing_stability.c -o $(SANITIZED_ROUTING_STABILITY_TEST)
	./$(SANITIZED_ROUTING_STABILITY_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/adaptive_stability_lab.c src/routing_stability.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_ADAPTIVE_STABILITY_LAB)
	ADAPTIVE_STABILITY_LAB_BIN=$(SANITIZED_ADAPTIVE_STABILITY_LAB) python3 tests/integration/test_adaptive_stability.py

phase52-gate: $(ROUTING_STABILITY_TEST) $(ADAPTIVE_STABILITY_LAB)
	$(MAKE) test-adaptive-stability
	$(MAKE) sanitize-adaptive-stability
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 52 gate passed: adaptive routing remains bounded under oscillating latency/load, shifts away from overload, expires stale evidence, fails over cleanly, regressions, and sanitizers completed."


$(ROUTING_PERF_TEST): tests/unit/test_routing_perf.c src/routing_perf.c include/routing_perf.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude tests/unit/test_routing_perf.c src/routing_perf.c -o $(ROUTING_PERF_TEST)

$(EDGE_ROUTING_PERF_LAB): src/edge_routing_perf_lab.c src/routing_perf.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c include/routing_perf.h include/adaptive_aware_pool.h include/adaptive_score.h include/latency_signal.h include/edge_aware_pool.h include/edge_locality.h
	mkdir -p build
	$(CC) $(PERF_CFLAGS) -Iinclude src/edge_routing_perf_lab.c src/routing_perf.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(EDGE_ROUTING_PERF_LAB)

test-edge-routing-perf: $(ROUTING_PERF_TEST) $(EDGE_ROUTING_PERF_LAB)
	./$(ROUTING_PERF_TEST)
	python3 tests/integration/test_edge_routing_perf.py

sanitize-edge-routing-perf:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude tests/unit/test_routing_perf.c src/routing_perf.c -o $(SANITIZED_ROUTING_PERF_TEST)
	./$(SANITIZED_ROUTING_PERF_TEST)
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude src/edge_routing_perf_lab.c src/routing_perf.c src/adaptive_aware_pool.c src/adaptive_score.c src/latency_signal.c src/edge_aware_pool.c src/edge_locality.c src/backend_pool.c src/backend_health.c src/circuit_breaker.c -o $(SANITIZED_EDGE_ROUTING_PERF_LAB)
	./$(SANITIZED_EDGE_ROUTING_PERF_LAB) --scenario smoke

phase53-gate: $(ROUTING_PERF_TEST) $(EDGE_ROUTING_PERF_LAB)
	$(MAKE) test-edge-routing-perf
	$(MAKE) sanitize-edge-routing-perf
	$(MAKE) test
	$(MAKE) sanitize
	@echo "Phase 53 gate passed: optimized median microbenchmarks bound adaptive edge-routing overhead versus locality-aware baseline, 32-backend scaling stays within budget, regressions, and sanitizers completed."
