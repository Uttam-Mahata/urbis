package urbis

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -lurbis -lm

#include "urbis.h"
#include "streaming.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"
import (
	"errors"
	"sync"
	"unsafe"
)

// Stream wraps the C UrbisStream for real-time location tracking
type Stream struct {
	ptr      *C.UrbisStream
	mu       sync.RWMutex
	running  bool
	geofenceCb  GeofenceEventHandler
	proximityCb ProximityEventHandler
}

// GeofenceEventType represents the type of geofence event
type GeofenceEventType int

const (
	GeofenceEnter GeofenceEventType = 1
	GeofenceExit  GeofenceEventType = 2
	GeofenceDwell GeofenceEventType = 3
)

// StreamEventType represents the type of stream event
type StreamEventType int

const (
	EventGeofence       StreamEventType = 1
	EventProximity      StreamEventType = 2
	EventTrajectory     StreamEventType = 3
	EventSpeedAlert     StreamEventType = 4
	EventStopDetected   StreamEventType = 5
	EventMovementStarted StreamEventType = 6
)

// TrackedObject represents an object being tracked in real-time
type TrackedObject struct {
	ObjectID         uint64
	CurrentPosition  Point
	PreviousPosition Point
	Speed            float64
	Heading          float64
	Timestamp        uint64
	LastUpdate       uint64
	IsMoving         bool
}

// GeofenceZoneInfo contains geofence zone information
type GeofenceZoneInfo struct {
	ZoneID         uint64
	Name           string
	Boundary       []Point
	Active         bool
	DwellThreshold uint64
}

// GeofenceEvent represents a geofence event
type GeofenceEvent struct {
	EventID   uint64
	ObjectID  uint64
	ZoneID    uint64
	EventType GeofenceEventType
	Timestamp uint64
	Position  Point
	DwellTime uint64
}

// ProximityEvent represents a proximity event
type ProximityEvent struct {
	EventID   uint64
	RuleID    uint64
	ObjectA   uint64
	ObjectB   uint64
	Distance  float64
	Timestamp uint64
	PositionA Point
	PositionB Point
}

// StreamEvent represents a generic stream event
type StreamEvent struct {
	Type          StreamEventType
	Timestamp     uint64
	ObjectID      uint64
	ZoneID        uint64
	OtherObject   uint64
	Position      Point
	Distance      float64
	Speed         float64
}

// TrajectoryStats contains trajectory statistics
type TrajectoryStats struct {
	ObjectID      uint64
	TotalDistance float64
	AvgSpeed      float64
	MaxSpeed      float64
	TotalTime     uint64
	MovingTime    uint64
	StoppedTime   uint64
	StartPoint    Point
	EndPoint      Point
	StartTime     uint64
	EndTime       uint64
	PointCount    uint64
	StopCount     uint64
}

// StreamStats contains stream statistics
type StreamStats struct {
	TrackedObjects uint64
	GeofenceZones  uint64
	ProximityRules uint64
	PendingEvents  uint64
	TotalUpdates   uint64
	TotalEvents    uint64
}

// GeofenceEventHandler is called when geofence events occur
type GeofenceEventHandler func(event *GeofenceEvent)

// ProximityEventHandler is called when proximity events occur
type ProximityEventHandler func(event *ProximityEvent)

// NewStream creates a new streaming context
func NewStream(idx *Index) (*Stream, error) {
	var cIdx *C.SpatialIndex = nil
	if idx != nil {
		cIdx = idx.ptr
	}
	
	ptr := C.stream_create(cIdx)
	if ptr == nil {
		return nil, errors.New("failed to create stream")
	}
	
	return &Stream{
		ptr:     ptr,
		running: false,
	}, nil
}

// Close destroys the stream
func (s *Stream) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr != nil {
		C.stream_destroy(s.ptr)
		s.ptr = nil
	}
}

// Start begins stream processing
func (s *Stream) Start() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	if C.stream_start(s.ptr) != C.STREAM_OK {
		return errors.New("failed to start stream")
	}
	
	s.running = true
	return nil
}

// Stop halts stream processing
func (s *Stream) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	if C.stream_stop(s.ptr) != C.STREAM_OK {
		return errors.New("failed to stop stream")
	}
	
	s.running = false
	return nil
}

// UpdateLocation updates an object's location
func (s *Stream) UpdateLocation(objectID uint64, x, y float64, timestamp uint64) error {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	result := C.stream_update_location(s.ptr, C.uint64_t(objectID),
		C.double(x), C.double(y), C.uint64_t(timestamp))
	
	if result != C.STREAM_OK {
		return errors.New("failed to update location")
	}
	
	return nil
}

// UpdateLocationEx updates an object's location with speed and heading
func (s *Stream) UpdateLocationEx(objectID uint64, x, y float64, timestamp uint64,
	speed, heading float64) error {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	result := C.stream_update_location_ex(s.ptr, C.uint64_t(objectID),
		C.double(x), C.double(y), C.uint64_t(timestamp),
		C.double(speed), C.double(heading))
	
	if result != C.STREAM_OK {
		return errors.New("failed to update location")
	}
	
	return nil
}

// GetTrackedObject retrieves the current state of a tracked object
func (s *Stream) GetTrackedObject(objectID uint64) (*TrackedObject, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	cObj := C.stream_get_object(s.ptr, C.uint64_t(objectID))
	if cObj == nil {
		return nil, errors.New("object not found")
	}
	
	return &TrackedObject{
		ObjectID:         uint64(cObj.object_id),
		CurrentPosition:  Point{X: float64(cObj.current_position.x), Y: float64(cObj.current_position.y)},
		PreviousPosition: Point{X: float64(cObj.previous_position.x), Y: float64(cObj.previous_position.y)},
		Speed:            float64(cObj.speed),
		Heading:          float64(cObj.heading),
		Timestamp:        uint64(cObj.timestamp),
		LastUpdate:       uint64(cObj.last_update),
		IsMoving:         bool(cObj.is_moving),
	}, nil
}

// RemoveTrackedObject removes an object from tracking
func (s *Stream) RemoveTrackedObject(objectID uint64) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	result := C.stream_remove_object(s.ptr, C.uint64_t(objectID))
	if result != C.STREAM_OK {
		return errors.New("object not found")
	}
	
	return nil
}

// AddGeofenceZone adds a geofence zone
func (s *Stream) AddGeofenceZone(zoneID uint64, name string, boundary []Point, dwellThreshold uint64) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	if len(boundary) < 3 {
		return errors.New("boundary must have at least 3 points")
	}
	
	// Convert boundary points to C array
	cPoints := make([]C.Point, len(boundary))
	for i, p := range boundary {
		cPoints[i] = C.Point{x: C.double(p.X), y: C.double(p.Y)}
	}
	
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	
	result := C.urbis_geofence_add(s.ptr, C.uint64_t(zoneID), cName,
		(*C.Point)(unsafe.Pointer(&cPoints[0])), C.size_t(len(boundary)),
		C.uint64_t(dwellThreshold))
	
	if result != C.URBIS_OK {
		if result == C.URBIS_ERR_INVALID {
			return errors.New("zone already exists")
		}
		return errors.New("failed to add geofence zone")
	}
	
	return nil
}

// RemoveGeofenceZone removes a geofence zone
func (s *Stream) RemoveGeofenceZone(zoneID uint64) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	result := C.stream_geofence_remove(s.ptr, C.uint64_t(zoneID))
	if result != C.STREAM_OK {
		return errors.New("zone not found")
	}
	
	return nil
}

// CheckGeofence checks which zones contain a point
func (s *Stream) CheckGeofence(x, y float64) ([]uint64, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var count C.size_t
	p := C.Point{x: C.double(x), y: C.double(y)}
	cZones := C.stream_geofence_check_point(s.ptr, &p, &count)
	
	if cZones == nil || count == 0 {
		return []uint64{}, nil
	}
	defer C.free(unsafe.Pointer(cZones))
	
	zones := make([]uint64, int(count))
	for i := 0; i < int(count); i++ {
		zones[i] = uint64(*(*C.uint64_t)(unsafe.Pointer(uintptr(unsafe.Pointer(cZones)) + uintptr(i)*unsafe.Sizeof(C.uint64_t(0)))))
	}
	
	return zones, nil
}

// GetObjectsInZone gets all objects currently in a zone
func (s *Stream) GetObjectsInZone(zoneID uint64) ([]uint64, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var count C.size_t
	cObjects := C.stream_geofence_objects_in_zone(s.ptr, C.uint64_t(zoneID), &count)
	
	if cObjects == nil || count == 0 {
		return []uint64{}, nil
	}
	defer C.free(unsafe.Pointer(cObjects))
	
	objects := make([]uint64, int(count))
	for i := 0; i < int(count); i++ {
		objects[i] = uint64(*(*C.uint64_t)(unsafe.Pointer(uintptr(unsafe.Pointer(cObjects)) + uintptr(i)*unsafe.Sizeof(C.uint64_t(0)))))
	}
	
	return objects, nil
}

// AddProximityRule adds a proximity alert rule
func (s *Stream) AddProximityRule(objectA, objectB uint64, threshold float64, oneShot bool) (uint64, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return 0, ErrClosed
	}
	
	ruleID := C.urbis_proximity_add_rule(s.ptr, C.uint64_t(objectA), C.uint64_t(objectB),
		C.double(threshold), C.bool(oneShot))
	
	if ruleID == 0 {
		return 0, errors.New("failed to add proximity rule")
	}
	
	return uint64(ruleID), nil
}

// RemoveProximityRule removes a proximity rule
func (s *Stream) RemoveProximityRule(ruleID uint64) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.ptr == nil {
		return ErrClosed
	}
	
	result := C.stream_proximity_remove_rule(s.ptr, C.uint64_t(ruleID))
	if result != C.STREAM_OK {
		return errors.New("rule not found")
	}
	
	return nil
}

// QueryProximity finds objects within distance of a point
func (s *Stream) QueryProximity(x, y float64, distance float64) ([]uint64, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var count C.size_t
	p := C.Point{x: C.double(x), y: C.double(y)}
	cObjects := C.stream_proximity_query(s.ptr, &p, C.double(distance), &count)
	
	if cObjects == nil || count == 0 {
		return []uint64{}, nil
	}
	defer C.free(unsafe.Pointer(cObjects))
	
	objects := make([]uint64, int(count))
	for i := 0; i < int(count); i++ {
		objects[i] = uint64(*(*C.uint64_t)(unsafe.Pointer(uintptr(unsafe.Pointer(cObjects)) + uintptr(i)*unsafe.Sizeof(C.uint64_t(0)))))
	}
	
	return objects, nil
}

// GetTrajectoryStats retrieves trajectory statistics for an object
func (s *Stream) GetTrajectoryStats(objectID uint64, startTime, endTime uint64) (*TrajectoryStats, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	cStats := C.urbis_trajectory_stats(s.ptr, C.uint64_t(objectID),
		C.uint64_t(startTime), C.uint64_t(endTime))
	
	if cStats == nil {
		return nil, errors.New("no trajectory data found")
	}
	defer C.urbis_trajectory_stats_free(cStats)
	
	return &TrajectoryStats{
		ObjectID:      uint64(cStats.object_id),
		TotalDistance: float64(cStats.total_distance),
		AvgSpeed:      float64(cStats.avg_speed),
		MaxSpeed:      float64(cStats.max_speed),
		TotalTime:     uint64(cStats.total_time),
		MovingTime:    uint64(cStats.moving_time),
		StoppedTime:   uint64(cStats.stopped_time),
		StartPoint:    Point{X: float64(cStats.start_x), Y: float64(cStats.start_y)},
		EndPoint:      Point{X: float64(cStats.end_x), Y: float64(cStats.end_y)},
		StartTime:     uint64(cStats.start_time),
		EndTime:       uint64(cStats.end_time),
		PointCount:    uint64(cStats.point_count),
		StopCount:     uint64(cStats.stop_count),
	}, nil
}

// GetTrajectoryPath retrieves the trajectory path for an object
func (s *Stream) GetTrajectoryPath(objectID uint64, startTime, endTime uint64) ([]Point, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var count C.size_t
	cPoints := C.urbis_trajectory_path(s.ptr, C.uint64_t(objectID),
		C.uint64_t(startTime), C.uint64_t(endTime), &count)
	
	if cPoints == nil || count == 0 {
		return []Point{}, nil
	}
	defer C.free(unsafe.Pointer(cPoints))
	
	points := make([]Point, int(count))
	for i := 0; i < int(count); i++ {
		cP := (*C.Point)(unsafe.Pointer(uintptr(unsafe.Pointer(cPoints)) + uintptr(i)*unsafe.Sizeof(C.Point{})))
		points[i] = Point{X: float64(cP.x), Y: float64(cP.y)}
	}
	
	return points, nil
}

// GetSimplifiedTrajectory retrieves a simplified trajectory path
func (s *Stream) GetSimplifiedTrajectory(objectID uint64, startTime, endTime uint64, tolerance float64) ([]Point, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var count C.size_t
	cPoints := C.urbis_trajectory_simplified(s.ptr, C.uint64_t(objectID),
		C.uint64_t(startTime), C.uint64_t(endTime), C.double(tolerance), &count)
	
	if cPoints == nil || count == 0 {
		return []Point{}, nil
	}
	defer C.free(unsafe.Pointer(cPoints))
	
	points := make([]Point, int(count))
	for i := 0; i < int(count); i++ {
		cP := (*C.Point)(unsafe.Pointer(uintptr(unsafe.Pointer(cPoints)) + uintptr(i)*unsafe.Sizeof(C.Point{})))
		points[i] = Point{X: float64(cP.x), Y: float64(cP.y)}
	}
	
	return points, nil
}

// PollEvent polls for the next event (non-blocking)
func (s *Stream) PollEvent() (*StreamEvent, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	cEvent := C.urbis_stream_poll_event(s.ptr)
	if cEvent == nil {
		return nil, nil // No event available
	}
	defer C.urbis_stream_event_free(cEvent)
	
	return &StreamEvent{
		Type:        StreamEventType(cEvent.event_type),
		Timestamp:   uint64(cEvent.timestamp),
		ObjectID:    uint64(cEvent.object_id),
		ZoneID:      uint64(cEvent.zone_id),
		OtherObject: uint64(cEvent.other_object),
		Position:    Point{X: float64(cEvent.x), Y: float64(cEvent.y)},
		Distance:    float64(cEvent.distance),
		Speed:       float64(cEvent.speed),
	}, nil
}

// WaitEvent waits for the next event (blocking)
func (s *Stream) WaitEvent(timeoutMs uint64) (*StreamEvent, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	cEvent := C.urbis_stream_wait_event(s.ptr, C.uint64_t(timeoutMs))
	if cEvent == nil {
		return nil, nil // Timeout
	}
	defer C.urbis_stream_event_free(cEvent)
	
	return &StreamEvent{
		Type:        StreamEventType(cEvent.event_type),
		Timestamp:   uint64(cEvent.timestamp),
		ObjectID:    uint64(cEvent.object_id),
		ZoneID:      uint64(cEvent.zone_id),
		OtherObject: uint64(cEvent.other_object),
		Position:    Point{X: float64(cEvent.x), Y: float64(cEvent.y)},
		Distance:    float64(cEvent.distance),
		Speed:       float64(cEvent.speed),
	}, nil
}

// EventCount returns the number of pending events
func (s *Stream) EventCount() uint64 {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return 0
	}
	
	return uint64(C.urbis_stream_event_count(s.ptr))
}

// GetStats retrieves stream statistics
func (s *Stream) GetStats() (*StreamStats, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	var cStats C.UrbisStreamStats
	C.urbis_stream_get_stats(s.ptr, &cStats)
	
	return &StreamStats{
		TrackedObjects: uint64(cStats.tracked_objects),
		GeofenceZones:  uint64(cStats.geofence_zones),
		ProximityRules: uint64(cStats.proximity_rules),
		PendingEvents:  uint64(cStats.pending_events),
		TotalUpdates:   uint64(cStats.total_updates),
		TotalEvents:    uint64(cStats.total_events),
	}, nil
}

// QueryTrackedObjectsInRegion finds all tracked objects in a region
func (s *Stream) QueryTrackedObjectsInRegion(minX, minY, maxX, maxY float64) ([]*TrackedObject, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.ptr == nil {
		return nil, ErrClosed
	}
	
	region := C.MBR{
		min_x: C.double(minX),
		min_y: C.double(minY),
		max_x: C.double(maxX),
		max_y: C.double(maxY),
	}
	
	var count C.size_t
	cObjects := C.stream_query_region(s.ptr, &region, &count)
	
	if cObjects == nil || count == 0 {
		return []*TrackedObject{}, nil
	}
	defer C.free(unsafe.Pointer(cObjects))
	
	objects := make([]*TrackedObject, int(count))
	for i := 0; i < int(count); i++ {
		cObj := *(**C.TrackedObject)(unsafe.Pointer(uintptr(unsafe.Pointer(cObjects)) + uintptr(i)*unsafe.Sizeof((*C.TrackedObject)(nil))))
		objects[i] = &TrackedObject{
			ObjectID:         uint64(cObj.object_id),
			CurrentPosition:  Point{X: float64(cObj.current_position.x), Y: float64(cObj.current_position.y)},
			PreviousPosition: Point{X: float64(cObj.previous_position.x), Y: float64(cObj.previous_position.y)},
			Speed:            float64(cObj.speed),
			Heading:          float64(cObj.heading),
			Timestamp:        uint64(cObj.timestamp),
			LastUpdate:       uint64(cObj.last_update),
			IsMoving:         bool(cObj.is_moving),
		}
	}
	
	return objects, nil
}

