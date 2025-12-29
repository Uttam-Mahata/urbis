package service

import (
	"context"
	"testing"

	"github.com/urbis/api/pkg/pb"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

func TestUrbisServer_Workflow(t *testing.T) {
	// Initialize server
	s := NewUrbisServer()
	ctx := context.Background()

	// 1. Create Index
	createReq := &pb.CreateIndexRequest{
		IndexId: "test_index",
	}
	_, err := s.CreateIndex(ctx, createReq)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}

	// 2. Insert Data
	insertReq := &pb.InsertPointRequest{
		IndexId: "test_index",
		X:       10.0,
		Y:       10.0,
	}
	_, err = s.InsertPoint(ctx, insertReq)
	if err != nil {
		t.Fatalf("Failed to insert point: %v", err)
	}

	// 3. Build Index
	buildReq := &pb.BuildRequest{
		IndexId: "test_index",
	}
	_, err = s.Build(ctx, buildReq)
	if err != nil {
		t.Fatalf("Failed to build index: %v", err)
	}

	// 4. Query Data
	queryReq := &pb.RangeQueryRequest{
		IndexId: "test_index",
		Range: &pb.MBR{
			MinX: 0,
			MinY: 0,
			MaxX: 20,
			MaxY: 20,
		},
	}
	resp, err := s.QueryRange(ctx, queryReq)
	if err != nil {
		t.Fatalf("Failed to query range: %v", err)
	}

	if resp.Count != 1 {
		t.Errorf("Expected 1 result, got %d", resp.Count)
	}

	// 5. Cleanup
	destroyReq := &pb.DestroyIndexRequest{
		IndexId: "test_index",
	}
	_, err = s.DestroyIndex(ctx, destroyReq)
	if err != nil {
		t.Fatalf("Failed to destroy index: %v", err)
	}
}

func TestUrbisServer_Errors(t *testing.T) {
	s := NewUrbisServer()
	ctx := context.Background()

	// Test non-existent index
	_, err := s.InsertPoint(ctx, &pb.InsertPointRequest{
		IndexId: "non_existent",
		X:       0, Y: 0,
	})

	if status.Code(err) != codes.NotFound {
		t.Errorf("Expected NotFound error, got %v", err)
	}

	// Test duplicate index creation
	s.CreateIndex(ctx, &pb.CreateIndexRequest{IndexId: "dup_index"})
	_, err = s.CreateIndex(ctx, &pb.CreateIndexRequest{IndexId: "dup_index"})

	if status.Code(err) != codes.AlreadyExists {
		t.Errorf("Expected AlreadyExists error, got %v", err)
	}
}
