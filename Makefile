CC := gcc

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
BENCH_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -DNDEBUG

TARGET := build/microproxy
SOURCE := src/main.c

PROCESS_INSPECT := build/process_inspect
NAMESPACE_INSPECT := build/namespace_inspect
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
	all \
	$(PROCESS_INSPECT) \
	$(NAMESPACE_INSPECT) \
	$(IO_BUFFER_TEST) \
	$(NONBLOCKING_TEST) \
	$(CONNECTION_TEST) \
	$(BACKEND_POOL_TEST) \
	$(HTTP_FRAMING_TEST)
	python3 tests/unit/test_smoke.py
	python3 tests/unit/test_process_inspect.py
	python3 tests/unit/test_namespace_inspect.py
	./$(IO_BUFFER_TEST)
	./$(NONBLOCKING_TEST)
	./$(CONNECTION_TEST)
	./$(BACKEND_POOL_TEST)
	./$(HTTP_FRAMING_TEST)

test-integration: \
	$(TCP_LISTENER) \
	$(EPOLL_SERVER)
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
	sanitize-tcp \
	sanitize-epoll

stage2-gate: test sanitize benchmark
	@echo "Stage 2 gate passed: tests, sanitizers, and benchmark completed."

clean:
	rm -rf build
