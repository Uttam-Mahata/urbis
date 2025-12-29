package service

import (
	"context"
	"errors"
	"io"
	"log"
	"sync"
	"time"

	pb "github.com/urbis/api/pkg/proto"
	"github.com/urbis/api/pkg/urbis"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// StreamServer implements the UrbisStreamService gRPC service
type StreamServer struct {
	pb.UnimplementedUrbisStreamServiceServer
	streams     map[string]*urbis.Stream
	streamIndex map[string]*urbis.Index // Associated spatial indexes
	mu          sync.RWMutex
	defaultStream *urbis.Stream // Default stream for single-stream usage
}

// NewStreamServer creates a new stream server
func NewStreamServer() (*StreamServer, error) {
	// Create a default stream
	stream, err := urbis.NewStream(nil)
	if err != nil {
		return nil, err
	}
	
	if err := stream.Start(); err != nil {
		stream.Close()
		return nil, err
	}
	
	return &StreamServer{
		streams:       make(map[string]*urbis.Stream),
		streamIndex:   make(map[string]*urbis.Index),
		defaultStream: stream,
	}, nil
}

// Close shuts down the stream server
func (s *StreamServer) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	for _, stream := range s.streams {
		stream.Close()
	}
	
	if s.defaultStream != nil {
		s.defaultStream.Close()
	}
}

// getStream returns the default stream or creates a named one
func (s *StreamServer) getStream(name string) *urbis.Stream {
	if name == "" {
		return s.defaultStream
	}
	
	s.mu.RLock()
	stream, exists := s.streams[name]
	s.mu.RUnlock()
	
	if exists {
		return stream
	}
	
	// Create new named stream
	s.mu.Lock()
	defer s.mu.Unlock()
	
	// Check again after acquiring write lock
	if stream, exists := s.streams[name]; exists {
		return stream
	}
	
	newStream, err := urbis.NewStream(s.streamIndex[name])
	if err != nil {
		return nil
	}
	
	newStream.Start()
	s.streams[name] = newStream
	return newStream
}

// StreamLocations implements bidirectional streaming for location updates
func (s *StreamServer) StreamLocations(stream pb.UrbisStreamService_StreamLocationsServer) error {
	ctx := stream.Context()
	urbisStream := s.defaultStream
	
	// Start event sender goroutine
	eventChan := make(chan *pb.StreamEvent, 100)
	errChan := make(chan error, 1)
	
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			case event := <-eventChan:
				if err := stream.Send(event); err != nil {
					errChan <- err
					return
				}
			}
		}
	}()
	
	// Poll events periodically
	go func() {
		ticker := time.NewTicker(10 * time.Millisecond)
		defer ticker.Stop()
		
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				event, err := urbisStream.PollEvent()
				if err != nil || event == nil {
					continue
				}
				
				pbEvent := convertStreamEvent(event)
				select {
				case eventChan <- pbEvent:
				default:
					// Drop if channel is full
				}
			}
		}
	}()
	
	// Receive location updates
	for {
		update, err := stream.Recv()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		
		// Process location update
		if update.Speed != nil && update.Heading != nil {
			err = urbisStream.UpdateLocationEx(update.ObjectId, update.X, update.Y,
				update.Timestamp, *update.Speed, *update.Heading)
		} else {
			err = urbisStream.UpdateLocation(update.ObjectId, update.X, update.Y, update.Timestamp)
		}
		
		if err != nil {
			log.Printf("Failed to update location for object %d: %v", update.ObjectId, err)
		}
		
		select {
		case err := <-errChan:
			return err
		default:
		}
	}
}

// SubscribeEvents subscribes to stream events
func (s *StreamServer) SubscribeEvents(req *pb.SubscribeEventsRequest, stream pb.UrbisStreamService_SubscribeEventsServer) error {
	ctx := stream.Context()
	urbisStream := s.defaultStream
	
	// Create filter sets
	eventTypes := make(map[pb.EventType]bool)
	for _, et := range req.EventTypes {
		eventTypes[et] = true
	}
	
	objectIDs := make(map[uint64]bool)
	for _, id := range req.ObjectIds {
		objectIDs[id] = true
	}
	
	zoneIDs := make(map[uint64]bool)
	for _, id := range req.ZoneIds {
		zoneIDs[id] = true
	}
	
	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()
	
	for {
		select {
		case <-ctx.Done():
			return nil
		case <-ticker.C:
			event, err := urbisStream.PollEvent()
			if err != nil || event == nil {
				continue
			}
			
			// Apply filters
			if len(eventTypes) > 0 && !eventTypes[pb.EventType(event.Type)] {
				continue
			}
			if len(objectIDs) > 0 && !objectIDs[event.ObjectID] {
				continue
			}
			if len(zoneIDs) > 0 && event.ZoneID != 0 && !zoneIDs[event.ZoneID] {
				continue
			}
			
			pbEvent := convertStreamEvent(event)
			if err := stream.Send(pbEvent); err != nil {
				return err
			}
		}
	}
}

// UpdateLocation updates a single location
func (s *StreamServer) UpdateLocation(ctx context.Context, req *pb.LocationUpdate) (*pb.UpdateLocationResponse, error) {
	urbisStream := s.defaultStream
	
	var err error
	if req.Speed != nil && req.Heading != nil {
		err = urbisStream.UpdateLocationEx(req.ObjectId, req.X, req.Y,
			req.Timestamp, *req.Speed, *req.Heading)
	} else {
		err = urbisStream.UpdateLocation(req.ObjectId, req.X, req.Y, req.Timestamp)
	}
	
	if err != nil {
		return &pb.UpdateLocationResponse{Success: false, Error: err.Error()}, nil
	}
	
	return &pb.UpdateLocationResponse{Success: true}, nil
}

// BatchUpdateLocations updates multiple locations
func (s *StreamServer) BatchUpdateLocations(ctx context.Context, req *pb.BatchLocationUpdate) (*pb.BatchUpdateResponse, error) {
	urbisStream := s.defaultStream
	
	var successCount, errorCount uint32
	var errorMsgs []string
	
	for _, update := range req.Updates {
		var err error
		if update.Speed != nil && update.Heading != nil {
			err = urbisStream.UpdateLocationEx(update.ObjectId, update.X, update.Y,
				update.Timestamp, *update.Speed, *update.Heading)
		} else {
			err = urbisStream.UpdateLocation(update.ObjectId, update.X, update.Y, update.Timestamp)
		}
		
		if err != nil {
			errorCount++
			errorMsgs = append(errorMsgs, err.Error())
		} else {
			successCount++
		}
	}
	
	return &pb.BatchUpdateResponse{
		SuccessCount: successCount,
		ErrorCount:   errorCount,
		Errors:       errorMsgs,
	}, nil
}

// GetTrackedObject retrieves a tracked object
func (s *StreamServer) GetTrackedObject(ctx context.Context, req *pb.GetTrackedObjectRequest) (*pb.TrackedObjectResponse, error) {
	urbisStream := s.defaultStream
	
	obj, err := urbisStream.GetTrackedObject(req.ObjectId)
	if err != nil {
		return &pb.TrackedObjectResponse{Found: false, ObjectId: req.ObjectId}, nil
	}
	
	return &pb.TrackedObjectResponse{
		Found:            true,
		ObjectId:         obj.ObjectID,
		CurrentPosition:  &pb.Point{X: obj.CurrentPosition.X, Y: obj.CurrentPosition.Y},
		PreviousPosition: &pb.Point{X: obj.PreviousPosition.X, Y: obj.PreviousPosition.Y},
		Speed:            obj.Speed,
		Heading:          obj.Heading,
		Timestamp:        obj.Timestamp,
		LastUpdate:       obj.LastUpdate,
		IsMoving:         obj.IsMoving,
	}, nil
}

// RemoveTrackedObject removes a tracked object
func (s *StreamServer) RemoveTrackedObject(ctx context.Context, req *pb.RemoveTrackedObjectRequest) (*pb.RemoveTrackedObjectResponse, error) {
	urbisStream := s.defaultStream
	
	err := urbisStream.RemoveTrackedObject(req.ObjectId)
	if err != nil {
		return &pb.RemoveTrackedObjectResponse{Success: false, Error: err.Error()}, nil
	}
	
	return &pb.RemoveTrackedObjectResponse{Success: true}, nil
}

// QueryTrackedObjects queries tracked objects in a region
func (s *StreamServer) QueryTrackedObjects(ctx context.Context, req *pb.QueryTrackedObjectsRequest) (*pb.QueryTrackedObjectsResponse, error) {
	urbisStream := s.defaultStream
	
	objects, err := urbisStream.QueryTrackedObjectsInRegion(
		req.Region.MinX, req.Region.MinY, req.Region.MaxX, req.Region.MaxY)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to query objects: %v", err)
	}
	
	pbObjects := make([]*pb.TrackedObjectResponse, len(objects))
	for i, obj := range objects {
		pbObjects[i] = &pb.TrackedObjectResponse{
			Found:            true,
			ObjectId:         obj.ObjectID,
			CurrentPosition:  &pb.Point{X: obj.CurrentPosition.X, Y: obj.CurrentPosition.Y},
			PreviousPosition: &pb.Point{X: obj.PreviousPosition.X, Y: obj.PreviousPosition.Y},
			Speed:            obj.Speed,
			Heading:          obj.Heading,
			Timestamp:        obj.Timestamp,
			LastUpdate:       obj.LastUpdate,
			IsMoving:         obj.IsMoving,
		}
	}
	
	return &pb.QueryTrackedObjectsResponse{
		Objects: pbObjects,
		Count:   uint32(len(objects)),
	}, nil
}

// CreateGeofence creates a geofence zone
func (s *StreamServer) CreateGeofence(ctx context.Context, req *pb.CreateGeofenceRequest) (*pb.CreateGeofenceResponse, error) {
	urbisStream := s.defaultStream
	
	boundary := make([]urbis.Point, len(req.Boundary))
	for i, p := range req.Boundary {
		boundary[i] = urbis.Point{X: p.X, Y: p.Y}
	}
	
	err := urbisStream.AddGeofenceZone(req.ZoneId, req.Name, boundary, req.DwellThreshold)
	if err != nil {
		return &pb.CreateGeofenceResponse{Success: false, Error: err.Error()}, nil
	}
	
	log.Printf("Created geofence zone %d: %s", req.ZoneId, req.Name)
	return &pb.CreateGeofenceResponse{Success: true, ZoneId: req.ZoneId}, nil
}

// UpdateGeofence updates a geofence zone
func (s *StreamServer) UpdateGeofence(ctx context.Context, req *pb.UpdateGeofenceRequest) (*pb.UpdateGeofenceResponse, error) {
	// For now, we remove and recreate
	urbisStream := s.defaultStream
	
	// Remove existing
	urbisStream.RemoveGeofenceZone(req.ZoneId)
	
	// Recreate with new settings
	if len(req.Boundary) > 0 {
		boundary := make([]urbis.Point, len(req.Boundary))
		for i, p := range req.Boundary {
			boundary[i] = urbis.Point{X: p.X, Y: p.Y}
		}
		
		name := ""
		if req.Name != nil {
			name = *req.Name
		}
		
		dwellThreshold := uint64(0)
		if req.DwellThreshold != nil {
			dwellThreshold = *req.DwellThreshold
		}
		
		err := urbisStream.AddGeofenceZone(req.ZoneId, name, boundary, dwellThreshold)
		if err != nil {
			return &pb.UpdateGeofenceResponse{Success: false, Error: err.Error()}, nil
		}
	}
	
	return &pb.UpdateGeofenceResponse{Success: true}, nil
}

// DeleteGeofence deletes a geofence zone
func (s *StreamServer) DeleteGeofence(ctx context.Context, req *pb.DeleteGeofenceRequest) (*pb.DeleteGeofenceResponse, error) {
	urbisStream := s.defaultStream
	
	err := urbisStream.RemoveGeofenceZone(req.ZoneId)
	if err != nil {
		return &pb.DeleteGeofenceResponse{Success: false, Error: err.Error()}, nil
	}
	
	log.Printf("Deleted geofence zone %d", req.ZoneId)
	return &pb.DeleteGeofenceResponse{Success: true}, nil
}

// ListGeofences lists all geofence zones
func (s *StreamServer) ListGeofences(ctx context.Context, req *pb.ListGeofencesRequest) (*pb.ListGeofencesResponse, error) {
	// Note: This would require additional C API to list zones
	// For now, return empty list
	return &pb.ListGeofencesResponse{Zones: []*pb.GeofenceZone{}}, nil
}

// CheckGeofence checks which zones contain a point
func (s *StreamServer) CheckGeofence(ctx context.Context, req *pb.CheckGeofenceRequest) (*pb.CheckGeofenceResponse, error) {
	urbisStream := s.defaultStream
	
	zones, err := urbisStream.CheckGeofence(req.X, req.Y)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to check geofence: %v", err)
	}
	
	return &pb.CheckGeofenceResponse{ZoneIds: zones}, nil
}

// GetObjectsInZone gets objects currently in a zone
func (s *StreamServer) GetObjectsInZone(ctx context.Context, req *pb.GetObjectsInZoneRequest) (*pb.GetObjectsInZoneResponse, error) {
	urbisStream := s.defaultStream
	
	objects, err := urbisStream.GetObjectsInZone(req.ZoneId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get objects in zone: %v", err)
	}
	
	return &pb.GetObjectsInZoneResponse{
		ObjectIds: objects,
		Count:     uint32(len(objects)),
	}, nil
}

// AddProximityRule adds a proximity rule
func (s *StreamServer) AddProximityRule(ctx context.Context, req *pb.AddProximityRuleRequest) (*pb.AddProximityRuleResponse, error) {
	urbisStream := s.defaultStream
	
	ruleID, err := urbisStream.AddProximityRule(req.ObjectA, req.ObjectB, req.Threshold, req.OneShot)
	if err != nil {
		return &pb.AddProximityRuleResponse{Success: false, Error: err.Error()}, nil
	}
	
	log.Printf("Added proximity rule %d: objects %d/%d within %.2f meters",
		ruleID, req.ObjectA, req.ObjectB, req.Threshold)
	return &pb.AddProximityRuleResponse{Success: true, RuleId: ruleID}, nil
}

// RemoveProximityRule removes a proximity rule
func (s *StreamServer) RemoveProximityRule(ctx context.Context, req *pb.RemoveProximityRuleRequest) (*pb.RemoveProximityRuleResponse, error) {
	urbisStream := s.defaultStream
	
	err := urbisStream.RemoveProximityRule(req.RuleId)
	if err != nil {
		return &pb.RemoveProximityRuleResponse{Success: false, Error: err.Error()}, nil
	}
	
	return &pb.RemoveProximityRuleResponse{Success: true}, nil
}

// QueryProximity queries objects near a point or object
func (s *StreamServer) QueryProximity(ctx context.Context, req *pb.QueryProximityRequest) (*pb.QueryProximityResponse, error) {
	urbisStream := s.defaultStream
	
	var objects []uint64
	var err error
	
	switch q := req.Query.(type) {
	case *pb.QueryProximityRequest_Point:
		objects, err = urbisStream.QueryProximity(q.Point.X, q.Point.Y, req.Distance)
	case *pb.QueryProximityRequest_ObjectId:
		obj, e := urbisStream.GetTrackedObject(q.ObjectId)
		if e != nil {
			return nil, status.Errorf(codes.NotFound, "object not found")
		}
		objects, err = urbisStream.QueryProximity(obj.CurrentPosition.X, obj.CurrentPosition.Y, req.Distance)
	default:
		return nil, status.Errorf(codes.InvalidArgument, "query point or object_id required")
	}
	
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to query proximity: %v", err)
	}
	
	return &pb.QueryProximityResponse{ObjectIds: objects}, nil
}

// GetTrajectoryStats gets trajectory statistics
func (s *StreamServer) GetTrajectoryStats(ctx context.Context, req *pb.GetTrajectoryStatsRequest) (*pb.TrajectoryStatsResponse, error) {
	urbisStream := s.defaultStream
	
	stats, err := urbisStream.GetTrajectoryStats(req.ObjectId, req.StartTime, req.EndTime)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "no trajectory data: %v", err)
	}
	
	return &pb.TrajectoryStatsResponse{
		ObjectId:      stats.ObjectID,
		TotalDistance: stats.TotalDistance,
		AvgSpeed:      stats.AvgSpeed,
		MaxSpeed:      stats.MaxSpeed,
		TotalTime:     stats.TotalTime,
		MovingTime:    stats.MovingTime,
		StoppedTime:   stats.StoppedTime,
		StartPoint:    &pb.Point{X: stats.StartPoint.X, Y: stats.StartPoint.Y},
		EndPoint:      &pb.Point{X: stats.EndPoint.X, Y: stats.EndPoint.Y},
		StartTime:     stats.StartTime,
		EndTime:       stats.EndTime,
		PointCount:    uint32(stats.PointCount),
		StopCount:     uint32(stats.StopCount),
	}, nil
}

// GetTrajectoryPath gets trajectory path
func (s *StreamServer) GetTrajectoryPath(ctx context.Context, req *pb.GetTrajectoryPathRequest) (*pb.TrajectoryPathResponse, error) {
	urbisStream := s.defaultStream
	
	points, err := urbisStream.GetTrajectoryPath(req.ObjectId, req.StartTime, req.EndTime)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "no trajectory data: %v", err)
	}
	
	pbPoints := make([]*pb.Point, len(points))
	for i, p := range points {
		pbPoints[i] = &pb.Point{X: p.X, Y: p.Y}
	}
	
	return &pb.TrajectoryPathResponse{
		Points:     pbPoints,
		PointCount: uint32(len(points)),
	}, nil
}

// GetSimplifiedPath gets simplified trajectory path
func (s *StreamServer) GetSimplifiedPath(ctx context.Context, req *pb.GetSimplifiedPathRequest) (*pb.TrajectoryPathResponse, error) {
	urbisStream := s.defaultStream
	
	points, err := urbisStream.GetSimplifiedTrajectory(req.ObjectId, req.StartTime, req.EndTime, req.Tolerance)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "no trajectory data: %v", err)
	}
	
	pbPoints := make([]*pb.Point, len(points))
	for i, p := range points {
		pbPoints[i] = &pb.Point{X: p.X, Y: p.Y}
	}
	
	return &pb.TrajectoryPathResponse{
		Points:     pbPoints,
		PointCount: uint32(len(points)),
	}, nil
}

// GetStreamStats gets stream statistics
func (s *StreamServer) GetStreamStats(ctx context.Context, req *pb.GetStreamStatsRequest) (*pb.StreamStatsResponse, error) {
	urbisStream := s.defaultStream
	
	stats, err := urbisStream.GetStats()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get stats: %v", err)
	}
	
	return &pb.StreamStatsResponse{
		TrackedObjects: stats.TrackedObjects,
		GeofenceZones:  stats.GeofenceZones,
		ProximityRules: stats.ProximityRules,
		PendingEvents:  stats.PendingEvents,
		TotalUpdates:   stats.TotalUpdates,
		TotalEvents:    stats.TotalEvents,
	}, nil
}

// Helper function to convert stream event to protobuf
func convertStreamEvent(event *urbis.StreamEvent) *pb.StreamEvent {
	pbEvent := &pb.StreamEvent{
		Type:      pb.EventType(event.Type),
		Timestamp: event.Timestamp,
	}
	
	switch event.Type {
	case urbis.EventGeofence:
		pbEvent.Event = &pb.StreamEvent_Geofence{
			Geofence: &pb.GeofenceEvent{
				ObjectId:  event.ObjectID,
				ZoneId:    event.ZoneID,
				Position:  &pb.Point{X: event.Position.X, Y: event.Position.Y},
			},
		}
	case urbis.EventProximity:
		pbEvent.Event = &pb.StreamEvent_Proximity{
			Proximity: &pb.ProximityEventData{
				ObjectA:   event.ObjectID,
				ObjectB:   event.OtherObject,
				Distance:  event.Distance,
				PositionA: &pb.Point{X: event.Position.X, Y: event.Position.Y},
			},
		}
	default:
		pbEvent.Event = &pb.StreamEvent_Movement{
			Movement: &pb.MovementEvent{
				ObjectId: event.ObjectID,
				Position: &pb.Point{X: event.Position.X, Y: event.Position.Y},
				Speed:    event.Speed,
			},
		}
	}
	
	return pbEvent
}

var (
	ErrStreamNotFound = errors.New("stream not found")
)

