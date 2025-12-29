# Urbis Architecture

Urbis is a high-performance, disk-aware spatial indexing library designed for city-scale GIS data. This document describes the internal architecture and design decisions that enable efficient spatial queries on large datasets.

## High-Level Overview

Urbis bridges the gap between in-memory spatial data structures and disk-based storage. It optimizes for disk I/O by clustering spatially adjacent data into contiguous disk blocks (tracks).

The core architecture consists of five main components:

1. **Parser**: Handles input data formats (GeoJSON, WKT).
2. **KD-Tree**: Partitions space to create balanced blocks.
3. **Spatial Index**: Coordinates the indexing process and manages objects.
4. **Quadtree**: Provides efficient lookups for adjacent pages/tracks.
5. **Disk Manager**: Manages page caching, serialization, and disk I/O.

```mermaid
graph TD
    User[User Application] --> API[Urbis Public API]

    subgraph "Input Processing"
        API --> Parser[Parser (GeoJSON/WKT)]
        Parser --> Geometry[Geometry Primitives]
    end

    subgraph "Core Indexing"
        API --> SpatialIndex[Spatial Index]
        SpatialIndex --> KDTree[KD-Tree Partitioning]
        SpatialIndex --> Quadtree[Quadtree Adjacency]
    end

    subgraph "Storage Engine"
        SpatialIndex --> PageManager[Page Manager]
        PageManager --> DiskManager[Disk Manager]
        DiskManager --> Cache[Page Cache]
        DiskManager --> IO[File I/O]
        IO --> Disk[Disk Storage]
    end

    KDTree -- "Defines Blocks" --> SpatialIndex
    SpatialIndex -- "Indexes Pages" --> Quadtree
```

## Component Details

### 1. Geometry System (`geometry.h`)

The foundation of Urbis is a robust geometry system supporting:
- **Points**: (x, y) coordinates.
- **LineStrings**: Sequences of points representing roads, paths, etc.
- **Polygons**: Closed loops representing buildings, zones, etc.
- **MBR**: Minimum Bounding Rectangles for fast intersection tests.

All spatial objects are wrapped in a `SpatialObject` structure containing their geometry, MBR, and a unique ID.

### 2. KD-Tree Partitioning (`kdtree.h`)

Unlike R-Trees which build the index bottom-up or incrementally, Urbis uses a bulk-loading approach for the initial build:
1. **Centroid Calculation**: The centroid of every object is computed.
2. **Recursive Partitioning**: Objects are recursively split along the median of the widest dimension (x or y).
3. **Leaf Node Creation**: Recursion stops when a node contains fewer than `block_size` objects.
4. **Block Assignment**: Each leaf node becomes a "block" or "track" on disk.

This ensures that spatially close objects are likely to end up in the same block.

### 3. Page & Track Management (`page.h`)

Physical storage is organized into **Tracks** and **Pages**:
- **Track**: Corresponds to a KD-Tree leaf node. It is a contiguous region on disk.
- **Page**: A fixed-size unit (e.g., 4KB) within a track.

**Key Optimization**: When reading a page, the OS read-ahead mechanism or custom logic can fetch adjacent pages in the same track cheaply. Urbis exploits this by placing spatially adjacent pages sequentially within a track.

### 4. Quadtree Adjacency Index (`quadtree.h`)

While the KD-Tree is great for partitioning, it doesn't support efficient range queries across boundaries once flattened to disk blocks. Urbis solves this with a secondary **Quadtree**:

- **Indexing**: After blocks are created, the MBR of every **Page** is inserted into a Quadtree.
- **Querying**: To find data in a region, the Quadtree is queried to find all intersecting Pages.
- **Seek Optimization**: The query result groups pages by Track ID. If multiple pages from the same track are needed, they can be read in a single sequential sweep (or with minimal seeks).

### 5. Disk Manager (`disk_manager.h`)

The Disk Manager abstracts the file system:
- **Pager**: Reads/writes fixed-size pages.
- **LRU Cache**: Keeps frequently accessed pages in memory (`cache_size` config).
- **Serialization**: Converts in-memory `SpatialObject` structs to/from binary page format.

## Data Flow

### Insertion Flow
1. User calls `urbis_insert()` or loads GeoJSON.
2. Object is added to an in-memory buffer in `SpatialIndex`.
3. If the buffer is full (or `urbis_build()` is called), the bulk loading process begins.

### Build Flow (`urbis_build()`)
1. **Partition**: All buffered objects are fed into the KD-Tree builder.
2. **Assign**: Objects are assigned to leaf nodes (Tracks).
3. **Paginate**: Objects within each Track are packed into Pages.
4. **Write**: Pages are written to disk (or memory buffer) sequentially by Track.
5. **Index**: The MBR and ID of every created Page are inserted into the Quadtree.

### Query Flow (`urbis_query_range()`)
1. User provides a query MBR.
2. **Quadtree Lookup**: The Quadtree finds all Pages whose MBR intersects the query MBR.
3. **Seek Optimization**: Found Pages are sorted/grouped by Track ID.
4. **Fetch**: The Disk Manager fetches the required Pages (checking Cache first).
5. **Filter**: For each fetched Page, objects are checked against the query MBR.
6. **Return**: Matching objects are returned to the user.

## Design Decisions & Trade-offs

| Decision | Benefit | Trade-off |
|----------|---------|-----------|
| **Bulk Loading (KD-Tree)** | Extremely fast build; highly optimized read layout. | Incremental updates are expensive (require re-build or overflow pages). |
| **Separate Adjacency Index** | Decouples physical layout from logical adjacency. | Extra memory usage for the Quadtree. |
| **Fixed-Size Pages** | Aligns with OS/Disk block sizes for performance. | Internal fragmentation if objects are small/few. |
| **C Language** | Low overhead, manual memory control. | Requires careful memory management (handled by internal allocators). |

## Future Improvements

- **Incremental Updates**: Implementing an LSM-tree like structure for efficiently handling new insertions without full rebuilds.
- **Compression**: Compressing page data to reduce I/O bandwidth.
- **Parallel Build**: Multi-threaded KD-tree construction.
