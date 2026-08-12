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

test: all
	python3 tests/unit/test_smoke.py

sanitize:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANFLAGS) $(SOURCE) -o build/microproxy-sanitized
	./build/microproxy-sanitized

clean:
	rm -rf build
