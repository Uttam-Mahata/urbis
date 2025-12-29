# Urbis API Reference

This document provides a detailed reference for the Urbis C API. The public API is defined in `include/urbis.h`.

## Table of Contents

1. [Initialization and Cleanup](#initialization-and-cleanup)
2. [Data Loading](#data-loading)
3. [Object Operations](#object-operations)
4. [Index Building](#index-building)
5. [Spatial Queries](#spatial-queries)
6. [Persistence](#persistence)
7. [Statistics](#statistics)
8. [Data Types](#data-types)

## Initialization and Cleanup

### `urbis_default_config`

```c
UrbisConfig urbis_default_config(void);
```

Returns a `UrbisConfig` struct with default settings.

**Defaults:**
- `block_size`: 1024 objects per track
- `page_capacity`: 64 objects per page
- `cache_size`: 128 pages
- `enable_quadtree`: true
- `persist`: false

### `urbis_create`

```c
UrbisIndex* urbis_create(const UrbisConfig *config);
```

Creates a new Urbis spatial index.

**Parameters:**
- `config`: Pointer to configuration struct, or `NULL` for defaults.

**Returns:**
- Pointer to new `UrbisIndex`, or `NULL` on error.

### `urbis_destroy`

```c
void urbis_destroy(UrbisIndex *idx);
```

Frees all resources associated with the index.

---

## Data Loading

### `urbis_load_geojson`

```c
int urbis_load_geojson(UrbisIndex *idx, const char *path);
```

Loads spatial data from a GeoJSON file.

**Parameters:**
- `idx`: Index handle.
- `path`: File path.

**Returns:**
- `URBIS_OK` on success, error code otherwise.

### `urbis_load_wkt`

```c
int urbis_load_wkt(UrbisIndex *idx, const char *wkt);
```

Parses and inserts a geometry from a Well-Known Text string.

**Example:**
```c
urbis_load_wkt(idx, "POINT(30 10)");
urbis_load_wkt(idx, "POLYGON((30 10, 40 40, 20 40, 10 20, 30 10))");
```

---

## Object Operations

### `urbis_insert`

```c
uint64_t urbis_insert(UrbisIndex *idx, const SpatialObject *obj);
```

Inserts a raw `SpatialObject` structure. This is a low-level API; prefer `urbis_insert_point` etc.

### `urbis_insert_point`

```c
uint64_t urbis_insert_point(UrbisIndex *idx, double x, double y);
```

Inserts a 2D point.

**Returns:**
- The assigned Object ID (unique > 0), or 0 on failure.

### `urbis_insert_linestring`

```c
uint64_t urbis_insert_linestring(UrbisIndex *idx, const Point *points, size_t count);
```

Inserts a line string defined by an array of points.

### `urbis_insert_polygon`

```c
uint64_t urbis_insert_polygon(UrbisIndex *idx, const Point *exterior, size_t count);
```

Inserts a polygon defined by its exterior ring.

### `urbis_remove`

```c
int urbis_remove(UrbisIndex *idx, uint64_t object_id);
```

Marks an object as deleted. Note: This may not immediately reclaim space until `urbis_build` or `urbis_optimize` is called.

---

## Index Building

### `urbis_build`

```c
int urbis_build(UrbisIndex *idx);
```

Constructs the spatial index from all inserted objects. This triggers:
1. KD-Tree partitioning.
2. Page creation and disk layout.
3. Quadtree adjacency indexing.

**Must be called** after bulk insertion and before querying for optimal performance.

### `urbis_optimize`

```c
int urbis_optimize(UrbisIndex *idx);
```

Rebuilds the index to remove gaps from deleted objects and optimize page layout.

---

## Spatial Queries

All query functions return a list that must be freed with `urbis_object_list_free()`.

### `urbis_query_range`

```c
UrbisObjectList* urbis_query_range(UrbisIndex *idx, const MBR *range);
```

Finds all objects that intersect the given Minimum Bounding Rectangle.

### `urbis_query_point`

```c
UrbisObjectList* urbis_query_point(UrbisIndex *idx, double x, double y);
```

Finds all objects covering the given point.

### `urbis_query_knn`

```c
UrbisObjectList* urbis_query_knn(UrbisIndex *idx, double x, double y, size_t k);
```

Finds the `k` nearest neighbors to the given point.

### `urbis_find_adjacent_pages`

```c
UrbisPageList* urbis_find_adjacent_pages(UrbisIndex *idx, const MBR *region);
```

Returns a list of disk pages that overlap the query region. Useful for visualizing disk access patterns or custom I/O handling.

**Returns:**
- `UrbisPageList` containing page IDs, track IDs, and estimated seek count.

---

## Persistence

### `urbis_save`

```c
int urbis_save(UrbisIndex *idx, const char *path);
```

Serializes the entire index state to a file.

### `urbis_load`

```c
UrbisIndex* urbis_load(const char *path);
```

Loads a previously saved index from a file.

---

## Statistics

### `urbis_get_stats`

```c
void urbis_get_stats(const UrbisIndex *idx, UrbisStats *stats);
```

Populates a `UrbisStats` structure with metrics like total objects, pages, tracks, and utilization.

---

## Data Types

### `UrbisConfig`
```c
typedef struct {
    size_t block_size;    // Objects per KD-tree leaf
    size_t page_capacity; // Objects per disk page
    size_t cache_size;    // Pages in LRU cache
    bool enable_quadtree; // Build adjacency index?
    bool persist;         // Auto-persist?
    const char *data_path;// Storage path
} UrbisConfig;
```

### `UrbisError`
```c
typedef enum {
    URBIS_OK = 0,
    URBIS_ERR_NULL = -1,
    URBIS_ERR_ALLOC = -2,
    URBIS_ERR_IO = -3,
    URBIS_ERR_PARSE = -4,
    URBIS_ERR_NOT_FOUND = -5,
    URBIS_ERR_FULL = -6,
    URBIS_ERR_INVALID = -7
} UrbisError;
```
