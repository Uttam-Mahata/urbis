// Package service implements the gRPC service for Urbis spatial indexing.
package service

import (
	"context"
	"sync"
	"time"

	"github.com/urbis/api/pkg/pb"
	"github.com/urbis/api/pkg/urbis"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// UrbisServer implements the UrbisService gRPC server
type UrbisServer struct {
	pb.UnimplementedUrbisServiceServer
	indexes sync.Map // map[string]*urbis.Index
	mu      sync.RWMutex
}

// NewUrbisServer creates a new Urbis gRPC server
func NewUrbisServer() *UrbisServer {
	return &UrbisServer{}
}

// getIndex retrieves an index by ID
func (s *UrbisServer) getIndex(indexID string) (*urbis.Index, error) {
	if indexID == "" {
		return nil, status.Error(codes.InvalidArgument, "index_id is required")
	}
	
	val, ok := s.indexes.Load(indexID)
	if !ok {
		return nil, status.Errorf(codes.NotFound, "index %q not found", indexID)
	}
	
	return val.(*urbis.Index), nil
}

// =============================================================================
// Index Management
// =============================================================================

// CreateIndex creates a new spatial index
func (s *UrbisServer) CreateIndex(ctx context.Context, req *pb.CreateIndexRequest) (*pb.CreateIndexResponse, error) {
	if req.IndexId == "" {
		return nil, status.Error(codes.InvalidArgument, "index_id is required")
	}
	
	// Check if index already exists
	if _, ok := s.indexes.Load(req.IndexId); ok {
		return nil, status.Errorf(codes.AlreadyExists, "index %q already exists", req.IndexId)
	}
	
	// Build configuration
	var config *urbis.Config
	if req.Config != nil {
		config = &urbis.Config{
			BlockSize:      req.Config.BlockSize,
			PageCapacity:   req.Config.PageCapacity,
			CacheSize:      req.Config.CacheSize,
			EnableQuadtree: req.Config.EnableQuadtree,
			Persist:        req.Config.Persist,
			DataPath:       req.Config.DataPath,
		}
	}
	
	// Create index
	idx, err := urbis.NewIndex(config)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to create index: %v", err)
	}
	
	s.indexes.Store(req.IndexId, idx)
	
	return &pb.CreateIndexResponse{
		IndexId: req.IndexId,
		Message: "Index created successfully",
	}, nil
}

// DestroyIndex destroys an existing index
func (s *UrbisServer) DestroyIndex(ctx context.Context, req *pb.DestroyIndexRequest) (*pb.DestroyIndexResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	idx.Close()
	s.indexes.Delete(req.IndexId)
	
	return &pb.DestroyIndexResponse{
		Message: "Index destroyed successfully",
	}, nil
}

// ListIndexes lists all available indexes
func (s *UrbisServer) ListIndexes(ctx context.Context, req *pb.ListIndexesRequest) (*pb.ListIndexesResponse, error) {
	var ids []string
	s.indexes.Range(func(key, value interface{}) bool {
		ids = append(ids, key.(string))
		return true
	})
	
	return &pb.ListIndexesResponse{
		IndexIds: ids,
	}, nil
}

// =============================================================================
// Data Loading
// =============================================================================

// LoadGeoJSON loads data from a GeoJSON file
func (s *UrbisServer) LoadGeoJSON(ctx context.Context, req *pb.LoadGeoJSONRequest) (*pb.LoadResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	countBefore := idx.Count()
	
	if err := idx.LoadGeoJSON(req.Path); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to load GeoJSON: %v", err)
	}
	
	countAfter := idx.Count()
	loaded := countAfter - countBefore
	
	return &pb.LoadResponse{
		ObjectsLoaded: loaded,
		Message:       "GeoJSON loaded successfully",
	}, nil
}

// LoadGeoJSONString loads data from a GeoJSON string
func (s *UrbisServer) LoadGeoJSONString(ctx context.Context, req *pb.LoadGeoJSONStringRequest) (*pb.LoadResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	countBefore := idx.Count()
	
	if err := idx.LoadGeoJSONString(req.Geojson); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to load GeoJSON: %v", err)
	}
	
	countAfter := idx.Count()
	loaded := countAfter - countBefore
	
	return &pb.LoadResponse{
		ObjectsLoaded: loaded,
		Message:       "GeoJSON loaded successfully",
	}, nil
}

// LoadWKT loads data from a WKT string
func (s *UrbisServer) LoadWKT(ctx context.Context, req *pb.LoadWKTRequest) (*pb.LoadResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	countBefore := idx.Count()
	
	if err := idx.LoadWKT(req.Wkt); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to load WKT: %v", err)
	}
	
	countAfter := idx.Count()
	loaded := countAfter - countBefore
	
	return &pb.LoadResponse{
		ObjectsLoaded: loaded,
		Message:       "WKT loaded successfully",
	}, nil
}

// =============================================================================
// Object Operations
// =============================================================================

// InsertPoint inserts a point into the index
func (s *UrbisServer) InsertPoint(ctx context.Context, req *pb.InsertPointRequest) (*pb.InsertResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	id, err := idx.InsertPoint(req.X, req.Y)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to insert point: %v", err)
	}
	
	return &pb.InsertResponse{
		ObjectId: id,
	}, nil
}

// InsertLineString inserts a linestring into the index
func (s *UrbisServer) InsertLineString(ctx context.Context, req *pb.InsertLineStringRequest) (*pb.InsertResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	points := make([]urbis.Point, len(req.Points))
	for i, p := range req.Points {
		points[i] = urbis.Point{X: p.X, Y: p.Y}
	}
	
	id, err := idx.InsertLineString(points)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to insert linestring: %v", err)
	}
	
	return &pb.InsertResponse{
		ObjectId: id,
	}, nil
}

// InsertPolygon inserts a polygon into the index
func (s *UrbisServer) InsertPolygon(ctx context.Context, req *pb.InsertPolygonRequest) (*pb.InsertResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	exterior := make([]urbis.Point, len(req.Exterior))
	for i, p := range req.Exterior {
		exterior[i] = urbis.Point{X: p.X, Y: p.Y}
	}
	
	id, err := idx.InsertPolygon(exterior)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to insert polygon: %v", err)
	}
	
	return &pb.InsertResponse{
		ObjectId: id,
	}, nil
}

// Remove removes an object from the index
func (s *UrbisServer) Remove(ctx context.Context, req *pb.RemoveRequest) (*pb.RemoveResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if err := idx.Remove(req.ObjectId); err != nil {
		return &pb.RemoveResponse{Success: false}, nil
	}
	
	return &pb.RemoveResponse{Success: true}, nil
}

// GetObject retrieves an object by ID
func (s *UrbisServer) GetObject(ctx context.Context, req *pb.GetObjectRequest) (*pb.GetObjectResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	obj, err := idx.Get(req.ObjectId)
	if err != nil {
		return &pb.GetObjectResponse{Found: false}, nil
	}
	
	return &pb.GetObjectResponse{
		Object: convertToPbObject(obj),
		Found:  true,
	}, nil
}

// =============================================================================
// Index Building
// =============================================================================

// Build builds the spatial index
func (s *UrbisServer) Build(ctx context.Context, req *pb.BuildRequest) (*pb.BuildResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	start := time.Now()
	
	if err := idx.Build(); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to build index: %v", err)
	}
	
	elapsed := time.Since(start)
	
	return &pb.BuildResponse{
		Message:     "Index built successfully",
		BuildTimeMs: float64(elapsed.Microseconds()) / 1000.0,
	}, nil
}

// Optimize optimizes the index
func (s *UrbisServer) Optimize(ctx context.Context, req *pb.OptimizeRequest) (*pb.OptimizeResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if err := idx.Optimize(); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to optimize index: %v", err)
	}
	
	return &pb.OptimizeResponse{
		Message: "Index optimized successfully",
	}, nil
}

// =============================================================================
// Spatial Queries
// =============================================================================

// QueryRange queries objects in a bounding box
func (s *UrbisServer) QueryRange(ctx context.Context, req *pb.RangeQueryRequest) (*pb.QueryResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if req.Range == nil {
		return nil, status.Error(codes.InvalidArgument, "range is required")
	}
	
	region := urbis.MBR{
		MinX: req.Range.MinX,
		MinY: req.Range.MinY,
		MaxX: req.Range.MaxX,
		MaxY: req.Range.MaxY,
	}
	
	start := time.Now()
	result, err := idx.QueryRange(region)
	elapsed := time.Since(start)
	
	if err != nil {
		return nil, status.Errorf(codes.Internal, "query failed: %v", err)
	}
	
	return &pb.QueryResponse{
		Objects:     convertToPbObjects(result.Objects),
		Count:       result.Count,
		QueryTimeMs: float64(elapsed.Microseconds()) / 1000.0,
	}, nil
}

// QueryPoint queries objects at a point
func (s *UrbisServer) QueryPoint(ctx context.Context, req *pb.PointQueryRequest) (*pb.QueryResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	start := time.Now()
	result, err := idx.QueryPoint(req.X, req.Y)
	elapsed := time.Since(start)
	
	if err != nil {
		return nil, status.Errorf(codes.Internal, "query failed: %v", err)
	}
	
	return &pb.QueryResponse{
		Objects:     convertToPbObjects(result.Objects),
		Count:       result.Count,
		QueryTimeMs: float64(elapsed.Microseconds()) / 1000.0,
	}, nil
}

// QueryKNN queries k nearest neighbors
func (s *UrbisServer) QueryKNN(ctx context.Context, req *pb.KNNQueryRequest) (*pb.QueryResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	start := time.Now()
	result, err := idx.QueryKNN(req.X, req.Y, req.K)
	elapsed := time.Since(start)
	
	if err != nil {
		return nil, status.Errorf(codes.Internal, "query failed: %v", err)
	}
	
	return &pb.QueryResponse{
		Objects:     convertToPbObjects(result.Objects),
		Count:       result.Count,
		QueryTimeMs: float64(elapsed.Microseconds()) / 1000.0,
	}, nil
}

// QueryAdjacent queries objects in adjacent pages
func (s *UrbisServer) QueryAdjacent(ctx context.Context, req *pb.RangeQueryRequest) (*pb.QueryResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if req.Range == nil {
		return nil, status.Error(codes.InvalidArgument, "range is required")
	}
	
	region := urbis.MBR{
		MinX: req.Range.MinX,
		MinY: req.Range.MinY,
		MaxX: req.Range.MaxX,
		MaxY: req.Range.MaxY,
	}
	
	start := time.Now()
	result, err := idx.QueryAdjacent(region)
	elapsed := time.Since(start)
	
	if err != nil {
		return nil, status.Errorf(codes.Internal, "query failed: %v", err)
	}
	
	return &pb.QueryResponse{
		Objects:     convertToPbObjects(result.Objects),
		Count:       result.Count,
		QueryTimeMs: float64(elapsed.Microseconds()) / 1000.0,
	}, nil
}

// =============================================================================
// Disk-Aware Operations
// =============================================================================

// FindAdjacentPages finds adjacent pages to a region
func (s *UrbisServer) FindAdjacentPages(ctx context.Context, req *pb.AdjacentPagesRequest) (*pb.AdjacentPagesResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if req.Region == nil {
		return nil, status.Error(codes.InvalidArgument, "region is required")
	}
	
	region := urbis.MBR{
		MinX: req.Region.MinX,
		MinY: req.Region.MinY,
		MaxX: req.Region.MaxX,
		MaxY: req.Region.MaxY,
	}
	
	result, err := idx.FindAdjacentPages(region)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to find adjacent pages: %v", err)
	}
	
	pages := make([]*pb.PageInfo, len(result.Pages))
	for i, p := range result.Pages {
		pages[i] = &pb.PageInfo{
			PageId:  p.PageID,
			TrackId: p.TrackID,
		}
	}
	
	return &pb.AdjacentPagesResponse{
		Pages:          pages,
		Count:          result.Count,
		EstimatedSeeks: result.EstimatedSeeks,
	}, nil
}

// =============================================================================
// Statistics
// =============================================================================

// GetStats retrieves index statistics
func (s *UrbisServer) GetStats(ctx context.Context, req *pb.StatsRequest) (*pb.StatsResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	stats := idx.GetStats()
	
	return &pb.StatsResponse{
		Stats: &pb.Stats{
			TotalObjects:       stats.TotalObjects,
			TotalBlocks:        stats.TotalBlocks,
			TotalPages:         stats.TotalPages,
			TotalTracks:        stats.TotalTracks,
			AvgObjectsPerPage:  stats.AvgObjectsPerPage,
			PageUtilization:    stats.PageUtilization,
			KdtreeDepth:        stats.KDTreeDepth,
			QuadtreeDepth:      stats.QuadtreeDepth,
			Bounds: &pb.MBR{
				MinX: stats.Bounds.MinX,
				MinY: stats.Bounds.MinY,
				MaxX: stats.Bounds.MaxX,
				MaxY: stats.Bounds.MaxY,
			},
		},
	}, nil
}

// GetCount returns the object count
func (s *UrbisServer) GetCount(ctx context.Context, req *pb.CountRequest) (*pb.CountResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	return &pb.CountResponse{
		Count: idx.Count(),
	}, nil
}

// GetBounds returns the spatial bounds
func (s *UrbisServer) GetBounds(ctx context.Context, req *pb.BoundsRequest) (*pb.BoundsResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	bounds := idx.Bounds()
	
	return &pb.BoundsResponse{
		Bounds: &pb.MBR{
			MinX: bounds.MinX,
			MinY: bounds.MinY,
			MaxX: bounds.MaxX,
			MaxY: bounds.MaxY,
		},
	}, nil
}

// =============================================================================
// Persistence
// =============================================================================

// Save saves the index to a file
func (s *UrbisServer) Save(ctx context.Context, req *pb.SaveRequest) (*pb.SaveResponse, error) {
	idx, err := s.getIndex(req.IndexId)
	if err != nil {
		return nil, err
	}
	
	if err := idx.Save(req.Path); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to save index: %v", err)
	}
	
	return &pb.SaveResponse{
		Message: "Index saved successfully",
	}, nil
}

// Load loads an index from a file
func (s *UrbisServer) Load(ctx context.Context, req *pb.LoadIndexRequest) (*pb.LoadIndexResponse, error) {
	if req.IndexId == "" {
		return nil, status.Error(codes.InvalidArgument, "index_id is required")
	}
	
	// Check if index already exists
	if _, ok := s.indexes.Load(req.IndexId); ok {
		return nil, status.Errorf(codes.AlreadyExists, "index %q already exists", req.IndexId)
	}
	
	idx, err := urbis.Load(req.Path)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to load index: %v", err)
	}
	
	s.indexes.Store(req.IndexId, idx)
	
	return &pb.LoadIndexResponse{
		Message: "Index loaded successfully",
	}, nil
}

// =============================================================================
// Helper Functions
// =============================================================================

// convertToPbObject converts a Go SpatialObject to protobuf
func convertToPbObject(obj *urbis.SpatialObject) *pb.SpatialObject {
	if obj == nil {
		return nil
	}
	
	pbObj := &pb.SpatialObject{
		Id:   obj.ID,
		Type: pb.GeomType(obj.Type),
		Centroid: &pb.Point{
			X: obj.Centroid.X,
			Y: obj.Centroid.Y,
		},
		Mbr: &pb.MBR{
			MinX: obj.MBR.MinX,
			MinY: obj.MBR.MinY,
			MaxX: obj.MBR.MaxX,
			MaxY: obj.MBR.MaxY,
		},
		Properties: obj.Properties,
	}
	
	switch obj.Type {
	case urbis.GeomPoint:
		if obj.Point != nil {
			pbObj.Geometry = &pb.SpatialObject_Point{
				Point: &pb.Point{X: obj.Point.X, Y: obj.Point.Y},
			}
		}
	case urbis.GeomLineString:
		if obj.Line != nil {
			points := make([]*pb.Point, len(obj.Line))
			for i, p := range obj.Line {
				points[i] = &pb.Point{X: p.X, Y: p.Y}
			}
			pbObj.Geometry = &pb.SpatialObject_Line{
				Line: &pb.LineString{Points: points},
			}
		}
	case urbis.GeomPolygon:
		if obj.Polygon != nil {
			points := make([]*pb.Point, len(obj.Polygon))
			for i, p := range obj.Polygon {
				points[i] = &pb.Point{X: p.X, Y: p.Y}
			}
			pbObj.Geometry = &pb.SpatialObject_Polygon{
				Polygon: &pb.Polygon{Exterior: points},
			}
		}
	}
	
	return pbObj
}

// convertToPbObjects converts a slice of SpatialObjects to protobuf
func convertToPbObjects(objs []*urbis.SpatialObject) []*pb.SpatialObject {
	result := make([]*pb.SpatialObject, len(objs))
	for i, obj := range objs {
		result[i] = convertToPbObject(obj)
	}
	return result
}

