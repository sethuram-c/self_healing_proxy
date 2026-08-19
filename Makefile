CC := gcc

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

TARGET := build/microproxy
SOURCE := src/main.c

PROCESS_INSPECT := build/process_inspect
TCP_LISTENER := build/tcp_listener
IO_BUFFER_TEST := build/test_io_buffer
NONBLOCKING_TEST := build/test_nonblocking
EPOLL_SERVER := build/epoll_server
SANITIZED_EPOLL := build/epoll_server-sanitized

SANITIZED_TCP := build/tcp_listener-sanitized


.PHONY: \
	all \
	clean \
	run \
	inspect-process \
	tcp-listener \
	test \
	test-unit \
	test-integration \
	sanitize \
	sanitize-main \
	sanitize-buffer \
	sanitize-tcp


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
	src/net_utils.c \
	include/net_utils.h

	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude \
		src/epoll_server.c \
		src/net_utils.c \
		-o $(EPOLL_SERVER)

run: all
	./$(TARGET)


inspect-process: $(PROCESS_INSPECT)
	./$(PROCESS_INSPECT)


tcp-listener: $(TCP_LISTENER)
	./$(TCP_LISTENER)

epoll-server: $(EPOLL_SERVER)
	./$(EPOLL_SERVER)


test-unit: \
	all \
	$(PROCESS_INSPECT) \
	$(IO_BUFFER_TEST) \
	$(NONBLOCKING_TEST)


	python3 tests/unit/test_smoke.py
	python3 tests/unit/test_process_inspect.py
	./$(IO_BUFFER_TESTi)
	./$(NONBLOCKING_TEST)


test-integration: \
	$(TCP_LISTENER) \
	$(EPOLL_SERVER)

	python3 tests/integration/test_tcp_listener.py
	python3 tests/integration/test_epoll_server.py

test: test-unit test-integration


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


sanitize-tcp:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/tcp_listener.c \
		src/io_buffer.c \
		-o $(SANITIZED_TCP)

	TCP_LISTENER_BIN=$(SANITIZED_TCP) \
		python3 tests/integration/test_tcp_listener.py

sanitize-nonblocking:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		tests/unit/test_nonblocking.c \
		src/net_utils.c \
		-o build/test_nonblocking-sanitized

	./build/test_nonblocking-sanitized

sanitize-epoll:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) -Iinclude \
		src/epoll_server.c \
		src/net_utils.c \
		-o $(SANITIZED_EPOLL)

	EPOLL_SERVER_BIN=$(SANITIZED_EPOLL) \
		python3 tests/integration/test_epoll_server.py

sanitize: \
	sanitize-main \
	sanitize-buffer \
	sanitize-nonblocking \
	sanitize-tcpi \
	sanitize-epoll


clean:
	rm -rf build
