# Urbis gRPC API

Go gRPC backend for the Urbis disk-aware spatial indexing library.

## Quick Start

```bash
# Build everything (requires protoc)
make setup

# Run the server
make run
```

## Prerequisites

- Go 1.21+
- protobuf compiler (`protoc`)
- protoc Go plugins

### Install protoc

```bash
# Ubuntu/Debian
sudo apt install protobuf-compiler

# macOS
brew install protobuf
```

### Install Go plugins

```bash
make install-tools
```

## Build

```bash
# Full setup (check tools, generate proto, build)
make setup

# Or step by step:
make proto      # Generate protobuf code
make build      # Build server (also rebuilds C library)
make build-fast # Build server only
```

## Run

```bash
# Start server on default port (50051)
make run

# Or with custom port
./bin/urbis-server --port 8080
```

## Usage Examples

### Using grpcurl

```bash
# List services
grpcurl -plaintext localhost:50051 list

# Create an index
grpcurl -plaintext \
  -d '{"index_id": "city"}' \
  localhost:50051 urbis.UrbisService/CreateIndex

# Load GeoJSON file
grpcurl -plaintext \
  -d '{"index_id": "city", "path": "/path/to/data.geojson"}' \
  localhost:50051 urbis.UrbisService/LoadGeoJSON

# Build spatial index
grpcurl -plaintext \
  -d '{"index_id": "city"}' \
  localhost:50051 urbis.UrbisService/Build

# Get statistics
grpcurl -plaintext \
  -d '{"index_id": "city"}' \
  localhost:50051 urbis.UrbisService/GetStats

# Range query
grpcurl -plaintext \
  -d '{"index_id": "city", "range": {"min_x": 88.34, "min_y": 22.56, "max_x": 88.36, "max_y": 22.58}}' \
  localhost:50051 urbis.UrbisService/QueryRange

# K-NN query
grpcurl -plaintext \
  -d '{"index_id": "city", "x": 88.35, "y": 22.575, "k": 10}' \
  localhost:50051 urbis.UrbisService/QueryKNN

# Find adjacent pages (disk-aware)
grpcurl -plaintext \
  -d '{"index_id": "city", "region": {"min_x": 88.34, "min_y": 22.56, "max_x": 88.36, "max_y": 22.58}}' \
  localhost:50051 urbis.UrbisService/FindAdjacentPages
```

### Using Go Client

```go
package main

import (
    "context"
    "log"

    "github.com/urbis/api/pkg/pb"
    "google.golang.org/grpc"
    "google.golang.org/grpc/credentials/insecure"
)

func main() {
    // Connect to server
    conn, err := grpc.Dial("localhost:50051", 
        grpc.WithTransportCredentials(insecure.NewCredentials()))
    if err != nil {
        log.Fatal(err)
    }
    defer conn.Close()

    client := pb.NewUrbisServiceClient(conn)
    ctx := context.Background()

    // Create index
    _, err = client.CreateIndex(ctx, &pb.CreateIndexRequest{
        IndexId: "myindex",
    })
    if err != nil {
        log.Fatal(err)
    }

    // Load data
    _, err = client.LoadGeoJSON(ctx, &pb.LoadGeoJSONRequest{
        IndexId: "myindex",
        Path:    "/path/to/data.geojson",
    })
    if err != nil {
        log.Fatal(err)
    }

    // Build index
    _, err = client.Build(ctx, &pb.BuildRequest{IndexId: "myindex"})
    if err != nil {
        log.Fatal(err)
    }

    // Query
    resp, err := client.QueryRange(ctx, &pb.RangeQueryRequest{
        IndexId: "myindex",
        Range: &pb.MBR{
            MinX: 88.34, MinY: 22.56,
            MaxX: 88.36, MaxY: 22.58,
        },
    })
    if err != nil {
        log.Fatal(err)
    }

    log.Printf("Found %d objects in %.3f ms", resp.Count, resp.QueryTimeMs)
}
```

## API Reference

### Index Management

| RPC | Description |
|-----|-------------|
| `CreateIndex` | Create a new spatial index |
| `DestroyIndex` | Destroy an index |
| `ListIndexes` | List all available indexes |

### Data Loading

| RPC | Description |
|-----|-------------|
| `LoadGeoJSON` | Load data from GeoJSON file |
| `LoadGeoJSONString` | Load data from GeoJSON string |
| `LoadWKT` | Load data from WKT string |

### Object Operations

| RPC | Description |
|-----|-------------|
| `InsertPoint` | Insert a point |
| `InsertLineString` | Insert a linestring |
| `InsertPolygon` | Insert a polygon |
| `Remove` | Remove an object by ID |
| `GetObject` | Get an object by ID |

### Index Operations

| RPC | Description |
|-----|-------------|
| `Build` | Build/rebuild spatial index |
| `Optimize` | Optimize index for performance |

### Spatial Queries

| RPC | Description |
|-----|-------------|
| `QueryRange` | Find objects in bounding box |
| `QueryPoint` | Find objects at a point |
| `QueryKNN` | Find k nearest neighbors |
| `QueryAdjacent` | Query objects in adjacent pages |

### Disk-Aware Operations

| RPC | Description |
|-----|-------------|
| `FindAdjacentPages` | Find adjacent pages with disk seek estimation |

### Statistics

| RPC | Description |
|-----|-------------|
| `GetStats` | Get detailed index statistics |
| `GetCount` | Get object count |
| `GetBounds` | Get spatial bounds |

### Persistence

| RPC | Description |
|-----|-------------|
| `Save` | Save index to file |
| `Load` | Load index from file |

### Advanced Spatial Operations (NEW!)

| RPC | Description |
|-----|-------------|
| `SpatialJoin` | Join two indexes by spatial relationship |
| `AggregateGrid` | Aggregate objects into grid cells |
| `Buffer` | Create buffer zone around object |
| `BufferPoint` | Create buffer zone around a point |
| `Intersects` | Check if two objects intersect |
| `Contains` | Check if object A contains B |
| `Distance` | Calculate distance between objects |
| `Voronoi` | Generate Voronoi diagram |
| `Delaunay` | Generate Delaunay triangulation |
| `ConvexHull` | Compute convex hull of all objects |

## Advanced Operations Examples

### Spatial Join (Find restaurants near metro stations)

```bash
# Create two indexes
grpcurl -plaintext -d '{"index_id": "restaurants"}' localhost:50051 urbis.UrbisService/CreateIndex
grpcurl -plaintext -d '{"index_id": "metro_stations"}' localhost:50051 urbis.UrbisService/CreateIndex

# Load data and build indexes...

# Find all restaurants within 500m (0.005 degrees) of metro stations
grpcurl -plaintext -d '{
  "index_a": "restaurants",
  "index_b": "metro_stations",
  "join_type": "JOIN_WITHIN",
  "distance": 0.005
}' localhost:50051 urbis.UrbisService/SpatialJoin
```

### Grid Aggregation (Density map)

```bash
# Count objects per 1km grid cell
grpcurl -plaintext -d '{
  "index_id": "city",
  "bounds": {"min_x": 88.34, "min_y": 22.56, "max_x": 88.38, "max_y": 22.60},
  "cell_size": 0.01,
  "agg_type": "AGG_COUNT"
}' localhost:50051 urbis.UrbisService/AggregateGrid
```

### Buffer (Create zone around point)

```bash
# Create a 500m buffer around a point
grpcurl -plaintext -d '{
  "x": 88.35,
  "y": 22.57,
  "distance": 0.005,
  "segments": 32
}' localhost:50051 urbis.UrbisService/BufferPoint
```

## Architecture

```
┌─────────────────────────────────────┐
│           gRPC Client               │
│    (grpcurl, Go client, etc.)       │
└───────────────┬─────────────────────┘
                │
                ▼
┌─────────────────────────────────────┐
│         gRPC Server (Go)            │
│  ┌────────────────────────────────┐ │
│  │    UrbisService (service/)     │ │
│  │    - Index management          │ │
│  │    - Query handling            │ │
│  │    - Stats retrieval           │ │
│  └────────────────────────────────┘ │
│                │                    │
│  ┌────────────────────────────────┐ │
│  │    CGO Bindings (pkg/urbis/)   │ │
│  │    - Type conversion           │ │
│  │    - Memory management         │ │
│  └────────────────────────────────┘ │
└───────────────┬─────────────────────┘
                │
                ▼
┌─────────────────────────────────────┐
│       liburbis.a (C Library)        │
│    - KD-tree partitioning           │
│    - Quadtree adjacency             │
│    - Disk-aware page allocation     │
└─────────────────────────────────────┘
```

## Project Structure

```
api/
├── proto/
│   └── urbis.proto       # Protocol Buffer definitions
├── pkg/
│   ├── pb/               # Generated protobuf code
│   │   ├── urbis.pb.go
│   │   └── urbis_grpc.pb.go
│   └── urbis/
│       └── bindings.go   # CGO bindings to C library
├── internal/
│   └── service/
│       └── urbis_service.go  # gRPC service implementation
├── cmd/
│   └── server/
│       └── main.go       # Server entry point
├── bin/
│   └── urbis-server      # Compiled binary
├── go.mod
├── go.sum
├── Makefile
└── README.md
```

