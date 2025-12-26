# Urbis Performance Benchmarks

Benchmark results using real OpenStreetMap data from multiple cities.

## Test Environment

- **System**: Linux
- **Compiler**: GCC with `-O3` optimization
- **Data Source**: OpenStreetMap via Overpass API
- **Index Configuration**:
  - Block size: 512
  - Page capacity: 32
  - Cache size: 256
  - Quadtree enabled: Yes

---

## Dataset Summary

| City | Features | Buildings | Roads | POIs | File Size |
|------|----------|-----------|-------|------|-----------|
| San Francisco | 2,324 | 453 | 1,067 | 804 | 1.62 MB |
| Manhattan | 859 | 349 | 510 | 0 | 880 KB |
| Kolkata | 2,781 | 2,149 | 532 | 100 | 1.82 MB |

---

## San Francisco (Financial District)

**Bounding Box**: `37.788,-122.405,37.795,-122.395`

### Data Loading
| Metric | Value |
|--------|-------|
| Features loaded | 2,324 |
| Load time | 53.67 ms |
| **Throughput** | **43,302 features/sec** |

### Index Structure
| Metric | Value |
|--------|-------|
| KD-tree blocks | 8 |
| Pages | 122 |
| Disk tracks | 24 |
| KD-tree depth | 12 |
| Quadtree depth | 3 |
| Avg objects/page | 19.05 |
| Page utilization | 29.8% |
| **Build time** | **2.89 ms** |

### Query Performance
| Query Type | Objects | Time (ms) | Rate |
|------------|---------|-----------|------|
| Small (1% area) | 2 | 0.023 | 87/ms |
| Medium (5% area) | 23 | 0.018 | 1,278/ms |
| Large (25% area) | 262 | 0.037 | 7,081/ms |
| **Full extent** | **2,324** | **0.090** | **25,822/ms** |

### Disk I/O Optimization
| Query Size | Pages | Seeks | Seek Ratio | Status |
|------------|-------|-------|------------|--------|
| Small (1%) | 7 | 2 | 0.29 | ✅ EXCELLENT |
| Medium (5%) | 14 | 6 | 0.43 | ✅ GOOD |
| Large (25%) | 23 | 12 | 0.52 | ✅ OK |

### K-Nearest Neighbor
- Query: 10 nearest features to center point
- Time: **0.148 ms**

---

## Manhattan (Times Square Area)

**Bounding Box**: `40.758,-73.988,40.764,-73.978`

### Data Loading
| Metric | Value |
|--------|-------|
| Features loaded | 859 |
| Load time | 23.80 ms |
| **Throughput** | **36,099 features/sec** |

### Index Structure
| Metric | Value |
|--------|-------|
| KD-tree blocks | 2 |
| Pages | 32 |
| Disk tracks | 6 |
| KD-tree depth | 10 |
| Quadtree depth | 1 |
| Avg objects/page | 26.84 |
| Page utilization | 41.9% |
| **Build time** | **0.92 ms** |

### Query Performance
| Query Type | Objects | Time (ms) | Rate |
|------------|---------|-----------|------|
| Small (1% area) | 7 | 0.009 | 778/ms |
| Medium (5% area) | 23 | 0.010 | 2,300/ms |
| Large (25% area) | 309 | 0.018 | 17,167/ms |
| **Full extent** | **859** | **0.021** | **40,905/ms** |

### Disk I/O Optimization
| Query Size | Pages | Seeks | Seek Ratio | Status |
|------------|-------|-------|------------|--------|
| Small (1%) | 3 | 2 | 0.67 | ⚠️ OK |
| Medium (5%) | 4 | 2 | 0.50 | ✅ OK |
| Large (25%) | 18 | 8 | 0.44 | ✅ GOOD |

### K-Nearest Neighbor
- Query: 10 nearest features to center point
- Time: **0.071 ms**

---

## Kolkata (Central Area)

**Bounding Box**: `22.565,88.340,22.580,88.360`

### Data Loading
| Metric | Value |
|--------|-------|
| Features loaded | 2,781 |
| Load time | 75.63 ms |
| **Throughput** | **36,770 features/sec** |

### Index Structure
| Metric | Value |
|--------|-------|
| KD-tree blocks | 8 |
| Pages | 119 |
| Disk tracks | 23 |
| KD-tree depth | 12 |
| Quadtree depth | 3 |
| Avg objects/page | 23.37 |
| Page utilization | 36.5% |
| **Build time** | **3.35 ms** |

### Query Performance
| Query Type | Objects | Time (ms) | Rate |
|------------|---------|-----------|------|
| Small (1% area) | 5 | 0.035 | 143/ms |
| Medium (5% area) | 14 | 0.019 | 737/ms |
| Large (25% area) | 320 | 0.073 | 4,384/ms |
| **Full extent** | **2,781** | **0.135** | **20,600/ms** |

### Disk I/O Optimization
| Query Size | Pages | Seeks | Seek Ratio | Status |
|------------|-------|-------|------------|--------|
| Small (1%) | 3 | 1 | 0.33 | ✅ GOOD |
| Medium (5%) | 4 | 2 | 0.50 | ✅ OK |
| Large (25%) | 23 | 12 | 0.52 | ✅ OK |

### K-Nearest Neighbor
- Query: 10 nearest features to center point
- Time: **0.155 ms**

---

## Performance Comparison

### Load Performance
| City | Features | Load Time | Rate |
|------|----------|-----------|------|
| San Francisco | 2,324 | 53.67 ms | 43,302/sec |
| Manhattan | 859 | 23.80 ms | 36,099/sec |
| Kolkata | 2,781 | 75.63 ms | 36,770/sec |
| **Average** | — | — | **38,724/sec** |

### Query Performance (Full Extent)
| City | Features | Query Time | Rate |
|------|----------|------------|------|
| San Francisco | 2,324 | 0.090 ms | 25,822/ms |
| Manhattan | 859 | 0.021 ms | 40,905/ms |
| Kolkata | 2,781 | 0.135 ms | 20,600/ms |
| **Average** | — | — | **29,109/ms** |

### Disk I/O Efficiency (Small Queries)
| City | Seek Ratio | Status |
|------|------------|--------|
| San Francisco | 0.29 | ✅ EXCELLENT |
| Manhattan | 0.67 | ⚠️ OK |
| Kolkata | 0.33 | ✅ GOOD |
| **Average** | **0.43** | **GOOD** |

---

## Understanding Seek Ratio

The **seek ratio** measures disk I/O efficiency:

```
Seek Ratio = Estimated Disk Seeks / Pages Accessed
```

| Ratio | Rating | Meaning |
|-------|--------|---------|
| < 0.3 | EXCELLENT | Pages highly clustered on same tracks |
| 0.3 - 0.5 | GOOD | Most pages share tracks |
| 0.5 - 0.7 | OK | Moderate clustering |
| > 0.7 | POOR | Pages scattered across tracks |

### Why This Matters

Traditional spatial indexes (R-trees) allocate pages without considering disk layout. A query touching 20 pages might require 20 separate disk seeks.

**Urbis disk-aware approach**:
1. KD-tree partitions space into blocks
2. Pages in the same block are allocated to the same disk track
3. Sequential reads within a track require no additional seeks

**Result**: For Kolkata's large query (23 pages, 12 seeks), we achieve **48% fewer disk operations** compared to random allocation.

---

## Reproducing These Results

```bash
# Build Urbis
make clean && make release

# Download and test San Francisco
./examples/download_osm.sh san_francisco "37.788,-122.405,37.795,-122.395"
./build/real_map_demo examples/data/san_francisco.geojson

# Download and test Manhattan
./examples/download_osm.sh manhattan "40.758,-73.988,40.764,-73.978"
./build/real_map_demo examples/data/manhattan.geojson

# Download and test Kolkata
./examples/download_osm.sh kolkata "22.565,88.340,22.580,88.360"
./build/real_map_demo examples/data/kolkata.geojson

# Test your own city
./examples/download_osm.sh [city_name] [min_lat,min_lon,max_lat,max_lon]
./build/real_map_demo examples/data/[city_name].geojson
```

---

## Notes

- All times measured using `clock()` with CPU time
- Results may vary based on system load and hardware
- Larger datasets will show more pronounced disk I/O benefits
- Memory-mapped I/O and SSD storage reduce but don't eliminate seek overhead

