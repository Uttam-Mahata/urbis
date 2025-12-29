package urbis

import (
	"testing"
)

func TestNewIndex(t *testing.T) {
	// Test default config
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index with default config: %v", err)
	}
	defer idx.Close()

	if idx.Count() != 0 {
		t.Errorf("Expected empty index, got count %d", idx.Count())
	}

	// Test custom config
	config := &Config{
		BlockSize:      512,
		PageCapacity:   32,
		CacheSize:      64,
		EnableQuadtree: true,
	}
	idx2, err := NewIndex(config)
	if err != nil {
		t.Fatalf("Failed to create index with custom config: %v", err)
	}
	defer idx2.Close()
}

func TestInsertPoint(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	id, err := idx.InsertPoint(10.5, 20.3)
	if err != nil {
		t.Fatalf("Failed to insert point: %v", err)
	}
	if id == 0 {
		t.Error("Expected valid ID, got 0")
	}

	if idx.Count() != 1 {
		t.Errorf("Expected count 1, got %d", idx.Count())
	}

	obj, err := idx.Get(id)
	if err != nil {
		t.Fatalf("Failed to get object: %v", err)
	}
	if obj.Type != GeomPoint {
		t.Errorf("Expected GeomPoint, got %d", obj.Type)
	}
	if obj.Point.X != 10.5 || obj.Point.Y != 20.3 {
		t.Errorf("Expected point (10.5, 20.3), got (%f, %f)", obj.Point.X, obj.Point.Y)
	}
}

func TestInsertLineString(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	points := []Point{
		{0, 0},
		{10, 10},
		{20, 0},
	}
	id, err := idx.InsertLineString(points)
	if err != nil {
		t.Fatalf("Failed to insert linestring: %v", err)
	}

	obj, err := idx.Get(id)
	if err != nil {
		t.Fatalf("Failed to get object: %v", err)
	}
	if obj.Type != GeomLineString {
		t.Errorf("Expected GeomLineString, got %d", obj.Type)
	}
	if len(obj.Line) != 3 {
		t.Errorf("Expected 3 points, got %d", len(obj.Line))
	}

	// Test invalid linestring (1 point)
	_, err = idx.InsertLineString([]Point{{0, 0}})
	if err != ErrInvalid {
		t.Errorf("Expected ErrInvalid for 1-point line, got %v", err)
	}
}

func TestInsertPolygon(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	points := []Point{
		{0, 0},
		{10, 0},
		{10, 10},
		{0, 10},
		{0, 0},
	}
	id, err := idx.InsertPolygon(points)
	if err != nil {
		t.Fatalf("Failed to insert polygon: %v", err)
	}

	obj, err := idx.Get(id)
	if err != nil {
		t.Fatalf("Failed to get object: %v", err)
	}
	if obj.Type != GeomPolygon {
		t.Errorf("Expected GeomPolygon, got %d", obj.Type)
	}
	if len(obj.Polygon) != 5 {
		t.Errorf("Expected 5 points, got %d", len(obj.Polygon))
	}

	// Test invalid polygon (< 3 points)
	_, err = idx.InsertPolygon([]Point{{0, 0}, {10, 10}})
	if err != ErrInvalid {
		t.Errorf("Expected ErrInvalid for 2-point polygon, got %v", err)
	}
}

func TestQueryRange(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	idx.InsertPoint(5, 5)
	idx.InsertPoint(15, 15)
	idx.InsertPoint(25, 25)

	err = idx.Build()
	if err != nil {
		t.Fatalf("Failed to build index: %v", err)
	}

	// Query including (5,5) and (15,15)
	rangeMBR := MBR{0, 0, 20, 20}
	results, err := idx.QueryRange(rangeMBR)
	if err != nil {
		t.Fatalf("Failed to query range: %v", err)
	}

	if results.Count != 2 {
		t.Errorf("Expected 2 results, got %d", results.Count)
	}
}

func TestLoadGeoJSONString(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	json := `{"type": "Feature", "geometry": {"type": "Point", "coordinates": [10.0, 20.0]}}`
	err = idx.LoadGeoJSONString(json)
	if err != nil {
		t.Fatalf("Failed to load GeoJSON string: %v", err)
	}

	if idx.Count() != 1 {
		t.Errorf("Expected count 1, got %d", idx.Count())
	}
}

func TestLoadWKT(t *testing.T) {
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	wkt := "POINT(30 40)"
	err = idx.LoadWKT(wkt)
	if err != nil {
		t.Fatalf("Failed to load WKT: %v", err)
	}

	if idx.Count() != 1 {
		t.Errorf("Expected count 1, got %d", idx.Count())
	}
}

func TestFindAdjacentPages(t *testing.T) {
	config := &Config{
		PageCapacity:   2, // Small capacity to force splits
		EnableQuadtree: true,
	}
	idx, err := NewIndex(config)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	defer idx.Close()

	// Insert enough points to create multiple pages
	for i := 0; i < 10; i++ {
		idx.InsertPoint(float64(i)*10, float64(i)*10)
	}

	idx.Build()

	region := MBR{0, 0, 100, 100}
	pages, err := idx.FindAdjacentPages(region)
	if err != nil {
		t.Fatalf("Failed to find adjacent pages: %v", err)
	}

	if pages.Count == 0 {
		t.Error("Expected some pages, got 0")
	}
	// Note: We can't easily assert exact seek count without knowing internal layout details,
	// but we can check it returns valid structure.
}

func TestPersistence(t *testing.T) {
	tmpDir := t.TempDir()
	tmpFile := tmpDir + "/test_urbis_persistence.dat"

	// Create and save
	idx, err := NewIndex(nil)
	if err != nil {
		t.Fatalf("Failed to create index: %v", err)
	}
	idx.InsertPoint(1, 1)
	idx.InsertPoint(2, 2)
	idx.Build()

	err = idx.Save(tmpFile)
	if err != nil {
		t.Fatalf("Failed to save index: %v", err)
	}
	idx.Close()

	// Load
	idx2, err := Load(tmpFile)
	if err != nil {
		t.Fatalf("Failed to load index: %v", err)
	}
	defer idx2.Close()

	if idx2.Count() != 2 {
		t.Errorf("Expected loaded count 2, got %d", idx2.Count())
	}
}
