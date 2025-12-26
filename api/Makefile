# Urbis Go gRPC API Makefile

# Go settings
GO := go
GOFLAGS := -v

# Protobuf settings
PROTO_DIR := proto
PB_DIR := pkg/pb
PROTO_FILE := $(PROTO_DIR)/urbis.proto

# Binary output
BIN_DIR := bin
SERVER_BIN := $(BIN_DIR)/urbis-server

# C library paths (relative to api/)
C_LIB_DIR := ../lib
C_INC_DIR := ../include

# Default target
.PHONY: all
all: proto build

# Create directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(PB_DIR):
	mkdir -p $(PB_DIR)

# Generate protobuf code
.PHONY: proto
proto: $(PB_DIR)
	@echo "Generating protobuf code..."
	protoc \
		--go_out=$(PB_DIR) \
		--go_opt=paths=source_relative \
		--go-grpc_out=$(PB_DIR) \
		--go-grpc_opt=paths=source_relative \
		-I$(PROTO_DIR) \
		$(PROTO_FILE)
	@echo "Protobuf code generated in $(PB_DIR)"

# Install protobuf tools
.PHONY: install-tools
install-tools:
	@echo "Installing protobuf tools..."
	$(GO) install google.golang.org/protobuf/cmd/protoc-gen-go@latest
	$(GO) install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
	@echo "Tools installed. Make sure ~/go/bin is in your PATH"

# Build C library first
.PHONY: c-lib
c-lib:
	@echo "Building C library..."
	cd .. && make release

# Download Go dependencies
.PHONY: deps
deps:
	@echo "Downloading Go dependencies..."
	$(GO) mod download
	$(GO) mod tidy

# Build the server
.PHONY: build
build: $(BIN_DIR) c-lib
	@echo "Building Urbis gRPC server..."
	CGO_ENABLED=1 $(GO) build $(GOFLAGS) -o $(SERVER_BIN) ./cmd/server
	@echo "Server built: $(SERVER_BIN)"

# Build without rebuilding C library
.PHONY: build-fast
build-fast: $(BIN_DIR)
	@echo "Building Urbis gRPC server (fast)..."
	CGO_ENABLED=1 $(GO) build $(GOFLAGS) -o $(SERVER_BIN) ./cmd/server
	@echo "Server built: $(SERVER_BIN)"

# Run the server
.PHONY: run
run: build
	@echo "Starting Urbis gRPC server..."
	LD_LIBRARY_PATH=$(C_LIB_DIR):$$LD_LIBRARY_PATH ./$(SERVER_BIN)

# Run without rebuilding
.PHONY: run-fast
run-fast:
	LD_LIBRARY_PATH=$(C_LIB_DIR):$$LD_LIBRARY_PATH ./$(SERVER_BIN)

# Run tests
.PHONY: test
test:
	CGO_ENABLED=1 $(GO) test -v ./...

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BIN_DIR)
	rm -rf $(PB_DIR)/*.pb.go

# Full clean including dependencies
.PHONY: distclean
distclean: clean
	$(GO) clean -cache -modcache

# Format code
.PHONY: fmt
fmt:
	$(GO) fmt ./...

# Lint code
.PHONY: lint
lint:
	golangci-lint run ./...

# Check if protoc is installed
.PHONY: check-protoc
check-protoc:
	@which protoc > /dev/null || (echo "Error: protoc not found. Install with: sudo apt install protobuf-compiler" && exit 1)
	@which protoc-gen-go > /dev/null || (echo "Error: protoc-gen-go not found. Run: make install-tools" && exit 1)
	@which protoc-gen-go-grpc > /dev/null || (echo "Error: protoc-gen-go-grpc not found. Run: make install-tools" && exit 1)
	@echo "All protobuf tools are installed"

# Setup everything from scratch
.PHONY: setup
setup: check-protoc deps proto build
	@echo ""
	@echo "Setup complete! Run the server with:"
	@echo "  make run"
	@echo ""
	@echo "Or directly:"
	@echo "  ./$(SERVER_BIN)"

# Help
.PHONY: help
help:
	@echo "Urbis Go gRPC API Makefile"
	@echo ""
	@echo "Setup:"
	@echo "  setup         - Full setup (check tools, deps, proto, build)"
	@echo "  install-tools - Install protobuf Go plugins"
	@echo "  check-protoc  - Verify protoc tools are installed"
	@echo "  deps          - Download Go dependencies"
	@echo ""
	@echo "Build:"
	@echo "  all           - Generate proto and build (default)"
	@echo "  proto         - Generate protobuf Go code"
	@echo "  build         - Build server (rebuilds C library)"
	@echo "  build-fast    - Build server (skip C library rebuild)"
	@echo "  c-lib         - Build only the C library"
	@echo ""
	@echo "Run:"
	@echo "  run           - Build and run server"
	@echo "  run-fast      - Run server without rebuilding"
	@echo ""
	@echo "Test & Quality:"
	@echo "  test          - Run tests"
	@echo "  fmt           - Format code"
	@echo "  lint          - Run linter"
	@echo ""
	@echo "Clean:"
	@echo "  clean         - Remove build artifacts"
	@echo "  distclean     - Full clean including caches"

