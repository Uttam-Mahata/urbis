# Urbis - Disk-Aware Spatial Indexing for City-Scale GIS

Urbis is a high-performance C library for spatial indexing of city-scale geographic data. It uses **KD-trees for block partitioning** and **quadtrees for adjacent page lookups**, designed to minimize disk seeks when working with massive datasets.

## Features

- **Disk-Aware Design**: Pages with spatially close data are allocated to the same disk track, minimizing seeks
- **KD-Tree Partitioning**: Efficiently partitions space using object centroids
- **Quadtree Adjacency**: O(log n) lookup of adjacent pages for range queries
- **Full Geometry Support**: Points, LineStrings, and Polygons
- **GeoJSON/WKT Support**: Load standard GIS data formats
- **Production Ready**: Comprehensive error handling, thread-safe design

## Architecture

```
┌─────────────────┐     ┌─────────────────┐
│  GeoJSON/WKT    │────▶│   Parser        │
│  Input          │     └────────┬────────┘
└─────────────────┘              │
                                 ▼
┌─────────────────┐     ┌─────────────────┐
│  KD-Tree        │◀───▶│  Spatial Index  │
│  (Partitioning) │     └────────┬────────┘
└─────────────────┘              │
                                 ▼
┌─────────────────┐     ┌─────────────────┐
│  Quadtree       │◀───▶│  Disk Manager   │
│  (Adjacency)    │     └────────┬────────┘
└─────────────────┘              │
                                 ▼
                        ┌─────────────────┐
                        │  Pages/Tracks   │
                        │  (Disk Layout)  │
                        └─────────────────┘
```

## Quick Start

### Building

```bash
make release    # Optimized build
make debug      # Debug build with sanitizers
make test       # Build and run tests
make demo       # Build and run city demo
```

### Basic Usage

```c
#include <urbis.h>

int main() {
    // Create index
    UrbisIndex *idx = urbis_create(NULL);
    
    // Load data from GeoJSON
    urbis_load_geojson(idx, "city_map.geojson");
    
    // Or insert objects directly
    urbis_insert_point(idx, 10.5, 20.3);
    
    Point road[] = {
        urbis_point(0, 0),
        urbis_point(100, 100)
    };
    urbis_insert_linestring(idx, road, 2);
    
    // Build spatial index
    urbis_build(idx);
    
    // Query range
    MBR region = urbis_mbr(0, 0, 50, 50);
    UrbisObjectList *result = urbis_query_range(idx, &region);
    
    printf("Found %zu objects\n", result->count);
    
    // Find adjacent pages (for disk-aware queries)
    UrbisPageList *pages = urbis_find_adjacent_pages(idx, &region);
    printf("Pages: %zu, Estimated seeks: %zu\n", 
           pages->count, pages->estimated_seeks);
    
    // Cleanup
    urbis_object_list_free(result);
    urbis_page_list_free(pages);
    urbis_destroy(idx);
    
    return 0;
}
```

### Configuration

```c
UrbisConfig config = urbis_default_config();
config.block_size = 1024;      // Max objects per block
config.page_capacity = 64;     // Max objects per page
config.cache_size = 128;       // Page cache size
config.enable_quadtree = true; // Enable adjacent page lookups

UrbisIndex *idx = urbis_create(&config);
```

## API Reference

### Index Management

| Function | Description |
|----------|-------------|
| `urbis_create(config)` | Create a new spatial index |
| `urbis_destroy(idx)` | Destroy an index |
| `urbis_build(idx)` | Build/rebuild the spatial index |
| `urbis_optimize(idx)` | Optimize index for better performance |

### Data Loading

| Function | Description |
|----------|-------------|
| `urbis_load_geojson(idx, path)` | Load GeoJSON file |
| `urbis_load_geojson_string(idx, json)` | Load GeoJSON string |
| `urbis_load_wkt(idx, wkt)` | Load WKT geometry |
| `urbis_insert_point(idx, x, y)` | Insert a point |
| `urbis_insert_linestring(idx, points, count)` | Insert a linestring |
| `urbis_insert_polygon(idx, exterior, count)` | Insert a polygon |

### Spatial Queries

| Function | Description |
|----------|-------------|
| `urbis_query_range(idx, mbr)` | Find objects in bounding box |
| `urbis_query_point(idx, x, y)` | Find objects at point |
| `urbis_query_knn(idx, x, y, k)` | Find k nearest neighbors |
| `urbis_find_adjacent_pages(idx, mbr)` | Find adjacent pages (disk-aware) |
| `urbis_query_adjacent(idx, mbr)` | Query objects in adjacent pages |

### Statistics

| Function | Description |
|----------|-------------|
| `urbis_count(idx)` | Get total object count |
| `urbis_bounds(idx)` | Get spatial bounds |
| `urbis_get_stats(idx, stats)` | Get detailed statistics |
| `urbis_estimate_seeks(idx, regions, count)` | Estimate disk seeks |

## How It Works

### Block Partitioning with KD-Tree

1. **Centroid Extraction**: Each spatial object's centroid is computed
2. **KD-Tree Construction**: Centroids are used to build a balanced KD-tree
3. **Block Creation**: Leaf nodes of the KD-tree become blocks (disk tracks)
4. **Page Allocation**: Objects are assigned to pages within blocks based on spatial locality

### Adjacent Page Lookup with Quadtree

1. **Page Extent Indexing**: Page bounding boxes are inserted into a quadtree
2. **Range Query**: When querying a region, the quadtree finds intersecting pages
3. **Seek Estimation**: Pages from the same track require no additional seeks
4. **Optimal Access Order**: Pages are accessed in track order to minimize seeks

### Disk Layout

```
Track 1: [Page 1] [Page 2] [Page 3] ... [Page 16]
         ╲_______________________________╱
          Spatially clustered objects

Track 2: [Page 17] [Page 18] [Page 19] ... [Page 32]
         ╲_________________________________╱
          Different spatial region
```

## Performance

The disk-aware design significantly reduces I/O for large datasets:

| Operation | Traditional R-Tree | Urbis |
|-----------|-------------------|-------|
| Range query seeks | O(log n) | O(1) per track |
| Adjacent lookup | O(n) scan | O(log n) |
| Build time | O(n log n) | O(n log n) |

### Real-World Benchmarks

Tested with real OpenStreetMap data:

| City | Features | Build Time | Query Rate | Seek Reduction |
|------|----------|------------|------------|----------------|
| San Francisco | 2,324 | 2.89 ms | 25,822/ms | 48% |
| Manhattan | 859 | 0.92 ms | 40,905/ms | 33% |
| Kolkata | 2,781 | 3.35 ms | 20,600/ms | 48% |

📊 **[Full Benchmark Results](docs/BENCHMARKS.md)**

### Try with Real Data

```bash
# Download and run with San Francisco data
make real-demo

# Or download any city
./examples/download_osm.sh kolkata "22.565,88.340,22.580,88.360"
./build/real_map_demo examples/data/kolkata.geojson
```

## File Structure

```
urbis/
├── include/
│   ├── urbis.h          # Main public API
│   ├── geometry.h       # Geometry primitives
│   ├── kdtree.h         # KD-tree implementation
│   ├── quadtree.h       # Quadtree implementation
│   ├── disk_manager.h   # Disk I/O management
│   ├── page.h           # Page/track structures
│   ├── spatial_index.h  # Spatial index coordination
│   └── parser.h         # GeoJSON/WKT parsing
├── src/
│   └── *.c              # Implementations
├── tests/
│   └── test_*.c         # Unit and integration tests
├── examples/
│   ├── city_demo.c      # Synthetic city data demo
│   ├── real_map_demo.c  # Real OpenStreetMap demo
│   └── download_osm.sh  # OSM data downloader
├── docs/
│   └── BENCHMARKS.md    # Performance benchmarks
├── Makefile
└── README.md
```

## Requirements

- C11 compiler (GCC 4.9+ or Clang 3.4+)
- POSIX-compatible system (Linux, macOS, *BSD)
- Math library (`-lm`)

## License

MIT License - see LICENSE file for details.

## Contributing

Contributions are welcome! Please ensure:
- Code passes all tests (`make test`)
- No memory leaks (`make memcheck`)
- Code is formatted (`make format`)
- Static analysis passes (`make analyze`)
