CC := gcc

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

TARGET := build/microproxy
SOURCE := src/main.c

.PHONY: all clean run test sanitize

all: $(TARGET)

$(TARGET): $(SOURCE)
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

run: all
	./$(TARGET)

test: all $(PROCESS_INSPECT)
	python3 tests/unit/test_smoke.py
	python3 tests/unit/test_process_inspect.py

sanitize:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) $(SOURCE) -o build/microproxy-sanitized
	./build/microproxy-sanitized

clean:
	rm -rf build

PROCESS_INSPECT := build/process_inspect

.PHONY: inspect-process

$(PROCESS_INSPECT): src/process_inspect.c
	mkdir -p build
	$(CC) $(CFLAGS) src/process_inspect.c -o $(PROCESS_INSPECT)

inspect-process: $(PROCESS_INSPECT)
	./$(PROCESS_INSPECT)

TCP_LISTENER := build/tcp_listener

.PHONY: tcp-listener

$(TCP_LISTENER): src/tcp_listener.c
	mkdir -p build
	$(CC) $(CFLAGS) src/tcp_listener.c -o $(TCP_LISTENER)

tcp-listener: $(TCP_LISTENER)
	./$(TCP_LISTENER)


.PHONY: test-unit test-integration

test-unit: all $(PROCESS_INSPECT)
	python3 tests/unit/test_smoke.py
	python3 tests/unit/test_process_inspect.py

test-integration: $(TCP_LISTENER)
	python3 tests/integration/test_tcp_listener.py

test: test-unit test-integration
