// Package urbis provides CGO bindings to the Urbis C spatial indexing library.
package urbis

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -lurbis -lm
#include <stdlib.h>
#include <string.h>
#include "urbis.h"
*/
import "C"
import (
	"errors"
	"runtime"
	"unsafe"
)

// Error codes matching C library
var (
	ErrNull     = errors.New("null pointer")
	ErrAlloc    = errors.New("memory allocation failed")
	ErrIO       = errors.New("I/O error")
	ErrParse    = errors.New("parse error")
	ErrNotFound = errors.New("not found")
	ErrFull     = errors.New("index full")
	ErrInvalid  = errors.New("invalid argument")
)

// toError converts C error code to Go error
func toError(code C.int) error {
	switch code {
	case C.URBIS_OK:
		return nil
	case C.URBIS_ERR_NULL:
		return ErrNull
	case C.URBIS_ERR_ALLOC:
		return ErrAlloc
	case C.URBIS_ERR_IO:
		return ErrIO
	case C.URBIS_ERR_PARSE:
		return ErrParse
	case C.URBIS_ERR_NOT_FOUND:
		return ErrNotFound
	case C.URBIS_ERR_FULL:
		return ErrFull
	case C.URBIS_ERR_INVALID:
		return ErrInvalid
	default:
		return errors.New("unknown error")
	}
}

// Config represents index configuration
type Config struct {
	BlockSize     uint64
	PageCapacity  uint64
	CacheSize     uint64
	EnableQuadtree bool
	Persist       bool
	DataPath      string
}

// DefaultConfig returns default configuration
func DefaultConfig() Config {
	cConfig := C.urbis_default_config()
	return Config{
		BlockSize:     uint64(cConfig.block_size),
		PageCapacity:  uint64(cConfig.page_capacity),
		CacheSize:     uint64(cConfig.cache_size),
		EnableQuadtree: bool(cConfig.enable_quadtree),
		Persist:       bool(cConfig.persist),
	}
}

// Index represents a spatial index
type Index struct {
	ptr *C.UrbisIndex
}

// NewIndex creates a new spatial index with optional configuration
func NewIndex(config *Config) (*Index, error) {
	var cConfig *C.UrbisConfig
	var cConfigVal C.UrbisConfig

	if config != nil {
		cConfigVal = C.UrbisConfig{
			block_size:      C.size_t(config.BlockSize),
			page_capacity:   C.size_t(config.PageCapacity),
			cache_size:      C.size_t(config.CacheSize),
			enable_quadtree: C.bool(config.EnableQuadtree),
			persist:         C.bool(config.Persist),
		}
		if config.DataPath != "" {
			cConfigVal.data_path = C.CString(config.DataPath)
			defer C.free(unsafe.Pointer(cConfigVal.data_path))
		}
		cConfig = &cConfigVal
	}

	ptr := C.urbis_create(cConfig)
	if ptr == nil {
		return nil, ErrAlloc
	}

	idx := &Index{ptr: ptr}
	runtime.SetFinalizer(idx, (*Index).Close)
	return idx, nil
}

// Close destroys the index and frees resources
func (idx *Index) Close() {
	if idx.ptr != nil {
		C.urbis_destroy(idx.ptr)
		idx.ptr = nil
	}
}

// Version returns the library version string
func Version() string {
	return C.GoString(C.urbis_version())
}

// =============================================================================
// Data Loading
// =============================================================================

// LoadGeoJSON loads data from a GeoJSON file
func (idx *Index) LoadGeoJSON(path string) error {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	return toError(C.urbis_load_geojson(idx.ptr, cpath))
}

// LoadGeoJSONString loads data from a GeoJSON string
func (idx *Index) LoadGeoJSONString(json string) error {
	cjson := C.CString(json)
	defer C.free(unsafe.Pointer(cjson))
	return toError(C.urbis_load_geojson_string(idx.ptr, cjson))
}

// LoadWKT loads data from a WKT string
func (idx *Index) LoadWKT(wkt string) error {
	cwkt := C.CString(wkt)
	defer C.free(unsafe.Pointer(cwkt))
	return toError(C.urbis_load_wkt(idx.ptr, cwkt))
}

// =============================================================================
// Object Operations
// =============================================================================

// Point represents a 2D point
type Point struct {
	X, Y float64
}

// MBR represents a minimum bounding rectangle
type MBR struct {
	MinX, MinY, MaxX, MaxY float64
}

// GeomType represents geometry type
type GeomType int

const (
	GeomPoint      GeomType = 0
	GeomLineString GeomType = 1
	GeomPolygon    GeomType = 2
)

// SpatialObject represents a spatial object
type SpatialObject struct {
	ID         uint64
	Type       GeomType
	Centroid   Point
	MBR        MBR
	Properties []byte
	// Geometry data (type-specific)
	Point   *Point
	Line    []Point
	Polygon []Point
}

// InsertPoint inserts a point and returns its ID
func (idx *Index) InsertPoint(x, y float64) (uint64, error) {
	id := C.urbis_insert_point(idx.ptr, C.double(x), C.double(y))
	if id == 0 {
		return 0, ErrAlloc
	}
	return uint64(id), nil
}

// InsertLineString inserts a linestring and returns its ID
func (idx *Index) InsertLineString(points []Point) (uint64, error) {
	if len(points) < 2 {
		return 0, ErrInvalid
	}

	cpoints := make([]C.Point, len(points))
	for i, p := range points {
		cpoints[i] = C.Point{x: C.double(p.X), y: C.double(p.Y)}
	}

	id := C.urbis_insert_linestring(idx.ptr, &cpoints[0], C.size_t(len(points)))
	if id == 0 {
		return 0, ErrAlloc
	}
	return uint64(id), nil
}

// InsertPolygon inserts a polygon and returns its ID
func (idx *Index) InsertPolygon(exterior []Point) (uint64, error) {
	if len(exterior) < 3 {
		return 0, ErrInvalid
	}

	cpoints := make([]C.Point, len(exterior))
	for i, p := range exterior {
		cpoints[i] = C.Point{x: C.double(p.X), y: C.double(p.Y)}
	}

	id := C.urbis_insert_polygon(idx.ptr, &cpoints[0], C.size_t(len(exterior)))
	if id == 0 {
		return 0, ErrAlloc
	}
	return uint64(id), nil
}

// Remove removes an object by ID
func (idx *Index) Remove(objectID uint64) error {
	return toError(C.urbis_remove(idx.ptr, C.uint64_t(objectID)))
}

// Get retrieves an object by ID
func (idx *Index) Get(objectID uint64) (*SpatialObject, error) {
	cobj := C.urbis_get(idx.ptr, C.uint64_t(objectID))
	if cobj == nil {
		return nil, ErrNotFound
	}
	return convertSpatialObject(cobj), nil
}

// convertSpatialObject converts C SpatialObject to Go
func convertSpatialObject(cobj *C.SpatialObject) *SpatialObject {
	obj := &SpatialObject{
		ID:   uint64(cobj.id),
		Type: GeomType(cobj._type),
		Centroid: Point{
			X: float64(cobj.centroid.x),
			Y: float64(cobj.centroid.y),
		},
		MBR: MBR{
			MinX: float64(cobj.mbr.min_x),
			MinY: float64(cobj.mbr.min_y),
			MaxX: float64(cobj.mbr.max_x),
			MaxY: float64(cobj.mbr.max_y),
		},
	}

	// Copy geometry based on type
	switch obj.Type {
	case GeomPoint:
		// Access point from union - use pointer arithmetic
		pointPtr := (*C.Point)(unsafe.Pointer(&cobj.geom[0]))
		obj.Point = &Point{X: float64(pointPtr.x), Y: float64(pointPtr.y)}
	case GeomLineString:
		// Access line from union
		linePtr := (*C.LineString)(unsafe.Pointer(&cobj.geom[0]))
		obj.Line = make([]Point, linePtr.count)
		cpoints := unsafe.Slice(linePtr.points, linePtr.count)
		for i := range obj.Line {
			obj.Line[i] = Point{X: float64(cpoints[i].x), Y: float64(cpoints[i].y)}
		}
	case GeomPolygon:
		// Access polygon exterior from union
		polyPtr := (*C.Polygon)(unsafe.Pointer(&cobj.geom[0]))
		obj.Polygon = make([]Point, polyPtr.ext_count)
		cpoints := unsafe.Slice(polyPtr.exterior, polyPtr.ext_count)
		for i := range obj.Polygon {
			obj.Polygon[i] = Point{X: float64(cpoints[i].x), Y: float64(cpoints[i].y)}
		}
	}

	return obj
}

// =============================================================================
// Index Building
// =============================================================================

// Build builds the spatial index
func (idx *Index) Build() error {
	return toError(C.urbis_build(idx.ptr))
}

// Optimize optimizes the index for better performance
func (idx *Index) Optimize() error {
	return toError(C.urbis_optimize(idx.ptr))
}

// =============================================================================
// Spatial Queries
// =============================================================================

// ObjectList represents a list of spatial objects from a query
type ObjectList struct {
	Objects []*SpatialObject
	Count   uint64
}

// QueryRange queries objects in a bounding box
func (idx *Index) QueryRange(region MBR) (*ObjectList, error) {
	cmbr := C.MBR{
		min_x: C.double(region.MinX),
		min_y: C.double(region.MinY),
		max_x: C.double(region.MaxX),
		max_y: C.double(region.MaxY),
	}

	result := C.urbis_query_range(idx.ptr, &cmbr)
	if result == nil {
		return &ObjectList{Objects: []*SpatialObject{}, Count: 0}, nil
	}
	defer C.urbis_object_list_free(result)

	return convertObjectList(result), nil
}

// QueryPoint queries objects at a point
func (idx *Index) QueryPoint(x, y float64) (*ObjectList, error) {
	result := C.urbis_query_point(idx.ptr, C.double(x), C.double(y))
	if result == nil {
		return &ObjectList{Objects: []*SpatialObject{}, Count: 0}, nil
	}
	defer C.urbis_object_list_free(result)

	return convertObjectList(result), nil
}

// QueryKNN queries k nearest neighbors
func (idx *Index) QueryKNN(x, y float64, k uint32) (*ObjectList, error) {
	result := C.urbis_query_knn(idx.ptr, C.double(x), C.double(y), C.size_t(k))
	if result == nil {
		return &ObjectList{Objects: []*SpatialObject{}, Count: 0}, nil
	}
	defer C.urbis_object_list_free(result)

	return convertObjectList(result), nil
}

// QueryAdjacent queries objects in adjacent pages
func (idx *Index) QueryAdjacent(region MBR) (*ObjectList, error) {
	cmbr := C.MBR{
		min_x: C.double(region.MinX),
		min_y: C.double(region.MinY),
		max_x: C.double(region.MaxX),
		max_y: C.double(region.MaxY),
	}

	result := C.urbis_query_adjacent(idx.ptr, &cmbr)
	if result == nil {
		return &ObjectList{Objects: []*SpatialObject{}, Count: 0}, nil
	}
	defer C.urbis_object_list_free(result)

	return convertObjectList(result), nil
}

// convertObjectList converts C UrbisObjectList to Go
func convertObjectList(clist *C.UrbisObjectList) *ObjectList {
	if clist == nil || clist.count == 0 {
		return &ObjectList{Objects: []*SpatialObject{}, Count: 0}
	}

	list := &ObjectList{
		Objects: make([]*SpatialObject, clist.count),
		Count:   uint64(clist.count),
	}

	cobjects := unsafe.Slice(clist.objects, clist.count)
	for i := range list.Objects {
		list.Objects[i] = convertSpatialObject(cobjects[i])
	}

	return list
}

// =============================================================================
// Adjacent Pages (Disk-Aware)
// =============================================================================

// PageInfo represents disk page information
type PageInfo struct {
	PageID  uint32
	TrackID uint32
}

// PageList represents a list of pages
type PageList struct {
	Pages          []PageInfo
	Count          uint64
	EstimatedSeeks uint64
}

// FindAdjacentPages finds adjacent pages to a region
func (idx *Index) FindAdjacentPages(region MBR) (*PageList, error) {
	cmbr := C.MBR{
		min_x: C.double(region.MinX),
		min_y: C.double(region.MinY),
		max_x: C.double(region.MaxX),
		max_y: C.double(region.MaxY),
	}

	result := C.urbis_find_adjacent_pages(idx.ptr, &cmbr)
	if result == nil {
		return &PageList{Pages: []PageInfo{}, Count: 0}, nil
	}
	defer C.urbis_page_list_free(result)

	list := &PageList{
		Pages:          make([]PageInfo, result.count),
		Count:          uint64(result.count),
		EstimatedSeeks: uint64(result.estimated_seeks),
	}

	if result.count > 0 {
		pageIDs := unsafe.Slice(result.page_ids, result.count)
		trackIDs := unsafe.Slice(result.track_ids, result.count)
		for i := range list.Pages {
			list.Pages[i] = PageInfo{
				PageID:  uint32(pageIDs[i]),
				TrackID: uint32(trackIDs[i]),
			}
		}
	}

	return list, nil
}

// =============================================================================
// Statistics
// =============================================================================

// Stats represents index statistics
type Stats struct {
	TotalObjects       uint64
	TotalBlocks        uint64
	TotalPages         uint64
	TotalTracks        uint64
	AvgObjectsPerPage  float64
	PageUtilization    float64
	KDTreeDepth        uint64
	QuadtreeDepth      uint64
	Bounds             MBR
}

// GetStats retrieves index statistics
func (idx *Index) GetStats() Stats {
	var cstats C.UrbisStats
	C.urbis_get_stats(idx.ptr, &cstats)

	return Stats{
		TotalObjects:       uint64(cstats.total_objects),
		TotalBlocks:        uint64(cstats.total_blocks),
		TotalPages:         uint64(cstats.total_pages),
		TotalTracks:        uint64(cstats.total_tracks),
		AvgObjectsPerPage:  float64(cstats.avg_objects_per_page),
		PageUtilization:    float64(cstats.page_utilization),
		KDTreeDepth:        uint64(cstats.kdtree_depth),
		QuadtreeDepth:      uint64(cstats.quadtree_depth),
		Bounds: MBR{
			MinX: float64(cstats.bounds.min_x),
			MinY: float64(cstats.bounds.min_y),
			MaxX: float64(cstats.bounds.max_x),
			MaxY: float64(cstats.bounds.max_y),
		},
	}
}

// Count returns the number of objects in the index
func (idx *Index) Count() uint64 {
	return uint64(C.urbis_count(idx.ptr))
}

// Bounds returns the spatial bounds of all data
func (idx *Index) Bounds() MBR {
	cmbr := C.urbis_bounds(idx.ptr)
	return MBR{
		MinX: float64(cmbr.min_x),
		MinY: float64(cmbr.min_y),
		MaxX: float64(cmbr.max_x),
		MaxY: float64(cmbr.max_y),
	}
}

// =============================================================================
// Persistence
// =============================================================================

// Save saves the index to a file
func (idx *Index) Save(path string) error {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	return toError(C.urbis_save(idx.ptr, cpath))
}

// Load loads an index from a file
func Load(path string) (*Index, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	ptr := C.urbis_load(cpath)
	if ptr == nil {
		return nil, ErrIO
	}

	idx := &Index{ptr: ptr}
	runtime.SetFinalizer(idx, (*Index).Close)
	return idx, nil
}

// Sync syncs changes to disk
func (idx *Index) Sync() error {
	return toError(C.urbis_sync(idx.ptr))
}

