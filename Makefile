# Urbis Makefile
# Disk-aware spatial indexing library for city-scale GIS

# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L
CFLAGS_DEBUG := $(CFLAGS) -g -O0 -DDEBUG -fsanitize=address,undefined
CFLAGS_RELEASE := $(CFLAGS) -O3 -DNDEBUG
LDFLAGS := -lm

# Directories
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests
EXAMPLE_DIR := examples
BUILD_DIR := build
LIB_DIR := lib

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJS_DEBUG := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/debug/%.o)

# Test files
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/%)

# Example files
EXAMPLE_SRCS := $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS := $(EXAMPLE_SRCS:$(EXAMPLE_DIR)/%.c=$(BUILD_DIR)/%)

# Library names
STATIC_LIB := $(LIB_DIR)/liburbis.a
SHARED_LIB := $(LIB_DIR)/liburbis.so

# Default target
.PHONY: all
all: release

# Release build
.PHONY: release
release: CFLAGS := $(CFLAGS_RELEASE)
release: dirs $(STATIC_LIB) $(SHARED_LIB)

# Debug build
.PHONY: debug
debug: CFLAGS := $(CFLAGS_DEBUG)
debug: LDFLAGS += -fsanitize=address,undefined
debug: dirs $(BUILD_DIR)/debug $(STATIC_LIB)

# Create directories
.PHONY: dirs
dirs:
	@mkdir -p $(BUILD_DIR) $(LIB_DIR)

$(BUILD_DIR)/debug:
	@mkdir -p $(BUILD_DIR)/debug

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -fPIC -c $< -o $@

$(BUILD_DIR)/debug/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -fPIC -c $< -o $@

# Static library
$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

# Shared library
$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

# Tests
.PHONY: tests
tests: CFLAGS := $(CFLAGS_DEBUG)
tests: LDFLAGS += -fsanitize=address,undefined
tests: dirs $(STATIC_LIB) $(TEST_BINS)

$(BUILD_DIR)/test_%: $(TEST_DIR)/test_%.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INC_DIR) $< $(STATIC_LIB) $(LDFLAGS) -o $@

# Run tests
.PHONY: test
test: tests
	@echo "Running all tests..."
	@echo ""
	@for test in $(TEST_BINS); do \
		echo "=== Running $$test ==="; \
		$$test || exit 1; \
		echo ""; \
	done
	@echo "All tests passed!"

# Examples
.PHONY: examples
examples: dirs $(STATIC_LIB) $(BUILD_DIR)/city_demo $(BUILD_DIR)/real_map_demo

$(BUILD_DIR)/city_demo: $(EXAMPLE_DIR)/city_demo.c $(STATIC_LIB)
	$(CC) $(CFLAGS_RELEASE) -I$(INC_DIR) $< $(STATIC_LIB) $(LDFLAGS) -o $@

$(BUILD_DIR)/real_map_demo: $(EXAMPLE_DIR)/real_map_demo.c $(STATIC_LIB)
	$(CC) $(CFLAGS_RELEASE) -I$(INC_DIR) $< $(STATIC_LIB) $(LDFLAGS) -o $@

# Run city demo (synthetic data)
.PHONY: demo
demo: examples
	@echo "Running city demo..."
	@$(BUILD_DIR)/city_demo

# Download OSM data and run real map demo
.PHONY: real-demo
real-demo: examples
	@chmod +x $(EXAMPLE_DIR)/download_osm.sh
	@$(EXAMPLE_DIR)/download_osm.sh
	@echo ""
	@$(BUILD_DIR)/real_map_demo

# Install
.PHONY: install
install: release
	@echo "Installing to /usr/local..."
	install -d /usr/local/include/urbis
	install -m 644 $(INC_DIR)/*.h /usr/local/include/urbis/
	install -d /usr/local/lib
	install -m 644 $(STATIC_LIB) /usr/local/lib/
	install -m 755 $(SHARED_LIB) /usr/local/lib/
	ldconfig || true

# Uninstall
.PHONY: uninstall
uninstall:
	rm -rf /usr/local/include/urbis
	rm -f /usr/local/lib/liburbis.a
	rm -f /usr/local/lib/liburbis.so

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

# Documentation (requires doxygen)
.PHONY: docs
docs:
	@which doxygen > /dev/null || (echo "doxygen not found" && exit 1)
	doxygen Doxyfile

# Format code (requires clang-format)
.PHONY: format
format:
	@which clang-format > /dev/null || (echo "clang-format not found" && exit 1)
	find $(SRC_DIR) $(INC_DIR) $(TEST_DIR) $(EXAMPLE_DIR) -name '*.c' -o -name '*.h' | \
		xargs clang-format -i

# Static analysis (requires cppcheck)
.PHONY: analyze
analyze:
	@which cppcheck > /dev/null || (echo "cppcheck not found" && exit 1)
	cppcheck --enable=all --suppress=missingIncludeSystem \
		-I$(INC_DIR) $(SRC_DIR) $(TEST_DIR) $(EXAMPLE_DIR)

# Valgrind memory check
.PHONY: memcheck
memcheck: tests
	@which valgrind > /dev/null || (echo "valgrind not found" && exit 1)
	@for test in $(TEST_BINS); do \
		echo "=== Checking $$test ==="; \
		valgrind --leak-check=full --error-exitcode=1 $$test || exit 1; \
	done

# Help
.PHONY: help
help:
	@echo "Urbis Makefile - Disk-Aware GIS Spatial Indexing"
	@echo ""
	@echo "Build Targets:"
	@echo "  all       - Build release version (default)"
	@echo "  release   - Build optimized library"
	@echo "  debug     - Build debug version with sanitizers"
	@echo "  clean     - Remove build artifacts"
	@echo ""
	@echo "Testing:"
	@echo "  tests     - Build all tests"
	@echo "  test      - Build and run all tests"
	@echo "  memcheck  - Run valgrind memory check"
	@echo ""
	@echo "Demos:"
	@echo "  demo      - Run demo with synthetic city data"
	@echo "  real-demo - Download real OSM data and run demo"
	@echo "  examples  - Build all example programs"
	@echo ""
	@echo "Installation:"
	@echo "  install   - Install to /usr/local"
	@echo "  uninstall - Remove installation"
	@echo ""
	@echo "Code Quality:"
	@echo "  format    - Format source code (requires clang-format)"
	@echo "  analyze   - Run static analysis (requires cppcheck)"
	@echo "  docs      - Generate documentation (requires doxygen)"
	@echo ""
	@echo "Custom OSM Download:"
	@echo "  ./examples/download_osm.sh [city] [bbox]"
	@echo "  Example: ./examples/download_osm.sh manhattan 40.75,-74.00,40.76,-73.99"

