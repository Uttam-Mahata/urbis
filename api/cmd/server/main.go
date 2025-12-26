// Package main implements the Urbis gRPC server.
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/urbis/api/internal/service"
	"github.com/urbis/api/pkg/pb"
	"github.com/urbis/api/pkg/urbis"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
)

var (
	port        = flag.Int("port", 50051, "The server port")
	enableReflection = flag.Bool("reflection", true, "Enable gRPC reflection for debugging")
)

func main() {
	flag.Parse()

	// Print banner
	fmt.Println("╔═══════════════════════════════════════════════════════════════╗")
	fmt.Println("║                    Urbis Spatial Index Server                 ║")
	fmt.Println("║           Disk-Aware GIS Indexing via gRPC                    ║")
	fmt.Println("╚═══════════════════════════════════════════════════════════════╝")
	fmt.Printf("\nLibrary version: %s\n", urbis.Version())
	fmt.Printf("Server port: %d\n\n", *port)

	// Create listener
	addr := fmt.Sprintf(":%d", *port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("Failed to listen on %s: %v", addr, err)
	}

	// Create gRPC server with options
	opts := []grpc.ServerOption{
		grpc.MaxRecvMsgSize(100 * 1024 * 1024), // 100MB max message size
		grpc.MaxSendMsgSize(100 * 1024 * 1024),
	}
	grpcServer := grpc.NewServer(opts...)

	// Register Urbis service
	urbisServer := service.NewUrbisServer()
	pb.RegisterUrbisServiceServer(grpcServer, urbisServer)

	// Enable reflection for grpcurl and other debugging tools
	if *enableReflection {
		reflection.Register(grpcServer)
		log.Println("gRPC reflection enabled")
	}

	// Setup graceful shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Handle shutdown signals
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigChan
		log.Printf("\nReceived signal %v, initiating graceful shutdown...", sig)
		
		// Give ongoing requests time to complete
		shutdownCtx, shutdownCancel := context.WithTimeout(ctx, 30*time.Second)
		defer shutdownCancel()

		// Stop accepting new connections
		grpcServer.GracefulStop()
		
		select {
		case <-shutdownCtx.Done():
			log.Println("Shutdown timeout, forcing stop")
			grpcServer.Stop()
		default:
		}
		
		cancel()
	}()

	// Start server
	log.Printf("Urbis gRPC server listening on %s", addr)
	log.Println("Press Ctrl+C to stop")
	fmt.Println()
	
	printUsageExamples(*port)

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}

	log.Println("Server stopped")
}

func printUsageExamples(port int) {
	fmt.Println("Usage Examples (with grpcurl):")
	fmt.Println("─────────────────────────────────────────────────────────────────")
	fmt.Println()
	fmt.Println("# List available services")
	fmt.Printf("grpcurl -plaintext localhost:%d list\n", port)
	fmt.Println()
	fmt.Println("# Create an index")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\"}' localhost:%d urbis.UrbisService/CreateIndex\n", port)
	fmt.Println()
	fmt.Println("# Load GeoJSON file")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\", \"path\": \"examples/data/kolkata.geojson\"}' localhost:%d urbis.UrbisService/LoadGeoJSON\n", port)
	fmt.Println()
	fmt.Println("# Build index")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\"}' localhost:%d urbis.UrbisService/Build\n", port)
	fmt.Println()
	fmt.Println("# Query range")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\", \"range\": {\"min_x\": 88.34, \"min_y\": 22.56, \"max_x\": 88.36, \"max_y\": 22.58}}' localhost:%d urbis.UrbisService/QueryRange\n", port)
	fmt.Println()
	fmt.Println("# Get statistics")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\"}' localhost:%d urbis.UrbisService/GetStats\n", port)
	fmt.Println()
	fmt.Println("# Find adjacent pages (disk-aware)")
	fmt.Printf("grpcurl -plaintext -d '{\"index_id\": \"city\", \"region\": {\"min_x\": 88.34, \"min_y\": 22.56, \"max_x\": 88.36, \"max_y\": 22.58}}' localhost:%d urbis.UrbisService/FindAdjacentPages\n", port)
	fmt.Println()
	fmt.Println("─────────────────────────────────────────────────────────────────")
	fmt.Println()
}

