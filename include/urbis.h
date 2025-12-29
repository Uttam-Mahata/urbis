/**
 * @file urbis.h
 * @brief Main public API for the Urbis GIS spatial indexing library
 * 
 * Urbis is a disk-aware spatial indexing library for city-scale GIS data.
 * It uses KD-trees for block partitioning and quadtrees for efficient
 * adjacent page lookups, minimizing disk seeks for large datasets.
 * 
 * @example
 * ```c
 * // Create index
 * UrbisIndex *idx = urbis_create(NULL);
 * 
 * // Load data from GeoJSON
 * urbis_load_geojson(idx, "city_map.geojson");
 * 
 * // Build spatial index
 * urbis_build(idx);
 * 
 * // Query adjacent pages
 * MBR region = mbr_create(0, 0, 100, 100);
 * UrbisPageList *pages = urbis_find_adjacent_pages(idx, &region);
 * 
 * // Cleanup
 * urbis_page_list_free(pages);
 * urbis_destroy(idx);
 * ```
 */

#ifndef URBIS_H
#define URBIS_H

#include "geometry.h"
#include "spatial_index.h"
#include "parser.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define URBIS_VERSION_MAJOR 1
#define URBIS_VERSION_MINOR 0
#define URBIS_VERSION_PATCH 0
#define URBIS_VERSION_STRING "1.0.0"

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Opaque index handle
 */
typedef SpatialIndex UrbisIndex;

/**
 * @brief Index configuration
 */
typedef struct {
    size_t block_size;            /**< Max objects per block (default: 1024) */
    size_t page_capacity;         /**< Max objects per page (default: 64) */
    size_t cache_size;            /**< Page cache size (default: 128) */
    bool enable_quadtree;         /**< Enable quadtree for adjacency (default: true) */
    bool persist;                 /**< Enable persistence (default: false) */
    const char *data_path;        /**< Path for data file (if persist=true) */
} UrbisConfig;

/**
 * @brief List of objects returned from queries
 */
typedef struct {
    SpatialObject **objects;
    size_t count;
} UrbisObjectList;

/**
 * @brief List of pages returned from queries
 */
typedef struct {
    uint32_t *page_ids;
    uint32_t *track_ids;
    size_t count;
    size_t estimated_seeks;
} UrbisPageList;

/**
 * @brief Index statistics
 */
typedef struct {
    size_t total_objects;
    size_t total_blocks;
    size_t total_pages;
    size_t total_tracks;
    double avg_objects_per_page;
    double page_utilization;
    size_t kdtree_depth;
    size_t quadtree_depth;
    MBR bounds;
} UrbisStats;

/**
 * @brief Error codes
 */
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

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

/**
 * @brief Get default configuration
 */
UrbisConfig urbis_default_config(void);

/**
 * @brief Create a new spatial index
 * @param config Configuration (NULL for defaults)
 * @return New index or NULL on error
 */
UrbisIndex* urbis_create(const UrbisConfig *config);

/**
 * @brief Destroy an index
 */
void urbis_destroy(UrbisIndex *idx);

/**
 * @brief Get library version string
 */
const char* urbis_version(void);

/* ============================================================================
 * Data Loading
 * ============================================================================ */

/**
 * @brief Load data from a GeoJSON file
 * @param idx Index to load into
 * @param path Path to GeoJSON file
 * @return URBIS_OK on success, error code otherwise
 */
int urbis_load_geojson(UrbisIndex *idx, const char *path);

/**
 * @brief Load data from a GeoJSON string
 */
int urbis_load_geojson_string(UrbisIndex *idx, const char *json);

/**
 * @brief Load data from a WKT string
 */
int urbis_load_wkt(UrbisIndex *idx, const char *wkt);

/* ============================================================================
 * Object Operations
 * ============================================================================ */

/**
 * @brief Insert a single spatial object
 * @param idx Index
 * @param obj Object to insert (will be copied)
 * @return Assigned object ID, or 0 on error
 */
uint64_t urbis_insert(UrbisIndex *idx, const SpatialObject *obj);

/**
 * @brief Insert a point
 */
uint64_t urbis_insert_point(UrbisIndex *idx, double x, double y);

/**
 * @brief Insert a linestring
 */
uint64_t urbis_insert_linestring(UrbisIndex *idx, const Point *points, size_t count);

/**
 * @brief Insert a polygon
 */
uint64_t urbis_insert_polygon(UrbisIndex *idx, const Point *exterior, size_t count);

/**
 * @brief Remove an object by ID
 */
int urbis_remove(UrbisIndex *idx, uint64_t object_id);

/**
 * @brief Get an object by ID
 */
SpatialObject* urbis_get(UrbisIndex *idx, uint64_t object_id);

/* ============================================================================
 * Index Building
 * ============================================================================ */

/**
 * @brief Build the spatial index
 * 
 * This partitions data into blocks using KD-tree based on centroids,
 * and optionally builds a quadtree for adjacent page lookups.
 */
int urbis_build(UrbisIndex *idx);

/**
 * @brief Optimize index for better query performance
 */
int urbis_optimize(UrbisIndex *idx);

/* ============================================================================
 * Spatial Queries
 * ============================================================================ */

/**
 * @brief Query objects in a bounding box
 */
UrbisObjectList* urbis_query_range(UrbisIndex *idx, const MBR *range);

/**
 * @brief Query objects at a point
 */
UrbisObjectList* urbis_query_point(UrbisIndex *idx, double x, double y);

/**
 * @brief Query k nearest neighbors
 */
UrbisObjectList* urbis_query_knn(UrbisIndex *idx, double x, double y, size_t k);

/**
 * @brief Find adjacent pages to a region (uses quadtree)
 * 
 * This is the key operation for disk-aware spatial queries.
 * Returns pages whose extents intersect or are adjacent to the query region.
 * Pages from the same track require no additional disk seeks.
 */
UrbisPageList* urbis_find_adjacent_pages(UrbisIndex *idx, const MBR *region);

/**
 * @brief Query objects in adjacent pages
 * 
 * Combines adjacent page lookup with object retrieval.
 */
UrbisObjectList* urbis_query_adjacent(UrbisIndex *idx, const MBR *region);

/* ============================================================================
 * Persistence
 * ============================================================================ */

/**
 * @brief Save index to a file
 */
int urbis_save(UrbisIndex *idx, const char *path);

/**
 * @brief Load index from a file
 */
UrbisIndex* urbis_load(const char *path);

/**
 * @brief Sync changes to disk (if persistence enabled)
 */
int urbis_sync(UrbisIndex *idx);

/* ============================================================================
 * Statistics and Utilities
 * ============================================================================ */

/**
 * @brief Get index statistics
 */
void urbis_get_stats(const UrbisIndex *idx, UrbisStats *stats);

/**
 * @brief Get number of objects in index
 */
size_t urbis_count(const UrbisIndex *idx);

/**
 * @brief Get spatial bounds of all data
 */
MBR urbis_bounds(const UrbisIndex *idx);

/**
 * @brief Print statistics to a file
 */
void urbis_print_stats(const UrbisIndex *idx, FILE *out);

/**
 * @brief Estimate disk seeks for a sequence of queries
 */
size_t urbis_estimate_seeks(const UrbisIndex *idx, 
                            const MBR *regions, size_t count);

/* ============================================================================
 * Advanced Spatial Operations
 * ============================================================================ */

#include "spatial_ops.h"

/**
 * @brief Create a buffer zone around an object
 * @param idx Index containing the object
 * @param object_id ID of object to buffer
 * @param distance Buffer distance
 * @param segments Number of segments for curves (0 for default)
 * @return New polygon representing buffer, or NULL on error
 */
Polygon* urbis_buffer(UrbisIndex *idx, uint64_t object_id, double distance, int segments);

/**
 * @brief Create a buffer zone around a point
 */
Polygon* urbis_buffer_point(double x, double y, double distance, int segments);

/**
 * @brief Check if two objects intersect
 */
bool urbis_intersects(UrbisIndex *idx, uint64_t id_a, uint64_t id_b);

/**
 * @brief Check if object A contains object B
 */
bool urbis_contains(UrbisIndex *idx, uint64_t container_id, uint64_t contained_id);

/**
 * @brief Calculate distance between two objects
 */
double urbis_distance(UrbisIndex *idx, uint64_t id_a, uint64_t id_b);

/**
 * @brief Spatial join result
 */
typedef struct {
    uint64_t *ids_a;       /**< IDs from first dataset */
    uint64_t *ids_b;       /**< IDs from second dataset */
    double *distances;     /**< Distances (for WITHIN joins) */
    size_t count;          /**< Number of pairs */
} UrbisSpatialJoinResult;

/**
 * @brief Perform spatial join between two indexes
 * @param idx_a First index
 * @param idx_b Second index
 * @param join_type Type of join (0=intersects, 1=within, 2=contains, 3=nearest)
 * @param distance Distance for WITHIN joins
 * @return Join result, or NULL on error
 */
UrbisSpatialJoinResult* urbis_spatial_join(UrbisIndex *idx_a, UrbisIndex *idx_b,
                                            int join_type, double distance);

/**
 * @brief Free spatial join result
 */
void urbis_spatial_join_free(UrbisSpatialJoinResult *result);

/**
 * @brief Grid aggregation result
 */
typedef struct {
    double *values;        /**< Aggregated values (row-major) */
    size_t *counts;        /**< Object counts per cell */
    size_t rows;           /**< Number of rows */
    size_t cols;           /**< Number of columns */
    MBR bounds;            /**< Grid bounds */
    double cell_size;      /**< Cell size */
} UrbisGridResult;

/**
 * @brief Perform grid-based spatial aggregation
 * @param idx Index to aggregate
 * @param bounds Area to aggregate (NULL for full extent)
 * @param cell_size Size of grid cells
 * @param agg_type Aggregation type (0=count, 1=sum, 2=avg, 3=min, 4=max)
 * @return Grid result, or NULL on error
 */
UrbisGridResult* urbis_aggregate_grid(UrbisIndex *idx, const MBR *bounds,
                                       double cell_size, int agg_type);

/**
 * @brief Free grid aggregation result
 */
void urbis_grid_result_free(UrbisGridResult *result);

/**
 * @brief Create Voronoi diagram from points in index
 * @param idx Index containing points
 * @param bounds Clipping bounds (NULL for auto)
 * @return Voronoi diagram, or NULL on error
 */
VoronoiDiagram* urbis_voronoi(UrbisIndex *idx, const MBR *bounds);

/**
 * @brief Create Delaunay triangulation from points in index
 * @param idx Index containing points
 * @return Delaunay triangulation, or NULL on error
 */
DelaunayTriangulation* urbis_delaunay(UrbisIndex *idx);

/**
 * @brief Compute convex hull of objects in index
 * @param idx Index
 * @return Convex hull polygon, or NULL on error
 */
Polygon* urbis_convex_hull(UrbisIndex *idx);

/* ============================================================================
 * Result List Operations
 * ============================================================================ */

/**
 * @brief Free an object list
 */
void urbis_object_list_free(UrbisObjectList *list);

/**
 * @brief Free a page list
 */
void urbis_page_list_free(UrbisPageList *list);

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * @brief Create an MBR (bounding box)
 */
static inline MBR urbis_mbr(double min_x, double min_y, double max_x, double max_y) {
    return mbr_create(min_x, min_y, max_x, max_y);
}

/**
 * @brief Create a point
 */
static inline Point urbis_point(double x, double y) {
    return point_create(x, y);
}

/* ============================================================================
 * Real-Time Streaming API
 * ============================================================================ */

#include "streaming.h"

/**
 * @brief Opaque stream handle
 */
typedef struct UrbisStream UrbisStreamHandle;

/**
 * @brief Create a new streaming context
 * @param idx Associated spatial index (can be NULL for standalone use)
 * @return New stream context, or NULL on error
 */
UrbisStreamHandle* urbis_stream_create(UrbisIndex *idx);

/**
 * @brief Destroy a streaming context
 */
void urbis_stream_destroy(UrbisStreamHandle *stream);

/**
 * @brief Start stream processing
 */
int urbis_stream_start(UrbisStreamHandle *stream);

/**
 * @brief Stop stream processing
 */
int urbis_stream_stop(UrbisStreamHandle *stream);

/**
 * @brief Update object location
 * @param stream Stream context
 * @param object_id Object identifier
 * @param x Longitude/X coordinate
 * @param y Latitude/Y coordinate
 * @param timestamp Position timestamp in milliseconds
 * @return URBIS_OK on success
 */
int urbis_stream_update(UrbisStreamHandle *stream, uint64_t object_id,
                        double x, double y, uint64_t timestamp);

/**
 * @brief Update object location with speed and heading
 */
int urbis_stream_update_ex(UrbisStreamHandle *stream, uint64_t object_id,
                           double x, double y, uint64_t timestamp,
                           double speed, double heading);

/**
 * @brief Batch update multiple locations
 */
int urbis_stream_update_batch(UrbisStreamHandle *stream, 
                              const uint64_t *object_ids,
                              const double *x, const double *y,
                              const uint64_t *timestamps, size_t count);

/**
 * @brief Get tracked object state
 */
TrackedObject* urbis_stream_get_object(UrbisStreamHandle *stream, uint64_t object_id);

/**
 * @brief Remove tracked object
 */
int urbis_stream_remove_object(UrbisStreamHandle *stream, uint64_t object_id);

/* ----------------------------------------------------------------------------
 * Geofencing
 * --------------------------------------------------------------------------- */

/**
 * @brief Geofence zone info for public API
 */
typedef struct {
    uint64_t zone_id;
    const char *name;
    const Point *boundary;
    size_t boundary_count;
    uint64_t dwell_threshold;
} UrbisGeofenceZoneInfo;

/**
 * @brief Add a geofence zone
 * @param stream Stream context
 * @param zone_id Unique zone identifier
 * @param name Zone name
 * @param boundary Polygon boundary points
 * @param count Number of boundary points
 * @param dwell_threshold Time in ms before DWELL event (0 to disable)
 * @return URBIS_OK on success
 */
int urbis_geofence_add(UrbisStreamHandle *stream, uint64_t zone_id, 
                       const char *name, const Point *boundary, size_t count,
                       uint64_t dwell_threshold);

/**
 * @brief Remove a geofence zone
 */
int urbis_geofence_remove(UrbisStreamHandle *stream, uint64_t zone_id);

/**
 * @brief Get zones containing a point
 * @param stream Stream context
 * @param x X coordinate
 * @param y Y coordinate
 * @param count Output: number of zones found
 * @return Array of zone IDs (caller must free)
 */
uint64_t* urbis_geofence_check(UrbisStreamHandle *stream, double x, double y,
                               size_t *count);

/**
 * @brief Get objects currently in a zone
 */
uint64_t* urbis_geofence_objects(UrbisStreamHandle *stream, uint64_t zone_id,
                                  size_t *count);

/**
 * @brief Set geofence event callback
 */
typedef void (*UrbisGeofenceCallback)(uint64_t event_id, uint64_t object_id,
                                       uint64_t zone_id, int event_type,
                                       uint64_t timestamp, double x, double y,
                                       void *user_data);
int urbis_geofence_set_callback(UrbisStreamHandle *stream, 
                                 UrbisGeofenceCallback callback,
                                 void *user_data);

/* ----------------------------------------------------------------------------
 * Proximity Alerts
 * --------------------------------------------------------------------------- */

/**
 * @brief Add a proximity rule
 * @param stream Stream context
 * @param object_a First object (0 = any)
 * @param object_b Second object (0 = any)
 * @param threshold Distance threshold in meters
 * @param one_shot Fire only once per proximity event
 * @return Rule ID, or 0 on error
 */
uint64_t urbis_proximity_add_rule(UrbisStreamHandle *stream, 
                                   uint64_t object_a, uint64_t object_b,
                                   double threshold, bool one_shot);

/**
 * @brief Remove a proximity rule
 */
int urbis_proximity_remove_rule(UrbisStreamHandle *stream, uint64_t rule_id);

/**
 * @brief Find objects within distance of a point
 */
uint64_t* urbis_proximity_query(UrbisStreamHandle *stream, double x, double y,
                                 double distance, size_t *count);

/**
 * @brief Find objects within distance of another object
 */
uint64_t* urbis_proximity_query_object(UrbisStreamHandle *stream, 
                                        uint64_t object_id, double distance,
                                        size_t *count);

/**
 * @brief Set proximity event callback
 */
typedef void (*UrbisProximityCallback)(uint64_t event_id, uint64_t rule_id,
                                        uint64_t object_a, uint64_t object_b,
                                        double distance, uint64_t timestamp,
                                        void *user_data);
int urbis_proximity_set_callback(UrbisStreamHandle *stream,
                                  UrbisProximityCallback callback,
                                  void *user_data);

/* ----------------------------------------------------------------------------
 * Trajectory Analysis
 * --------------------------------------------------------------------------- */

/**
 * @brief Trajectory statistics result
 */
typedef struct {
    uint64_t object_id;
    double total_distance;
    double avg_speed;
    double max_speed;
    uint64_t total_time;
    uint64_t moving_time;
    uint64_t stopped_time;
    double start_x, start_y;
    double end_x, end_y;
    uint64_t start_time;
    uint64_t end_time;
    size_t point_count;
    size_t stop_count;
} UrbisTrajectoryStats;

/**
 * @brief Get trajectory statistics for an object
 * @param stream Stream context
 * @param object_id Object to analyze
 * @param start_time Start of time range
 * @param end_time End of time range
 * @return Statistics or NULL on error (caller must free)
 */
UrbisTrajectoryStats* urbis_trajectory_stats(UrbisStreamHandle *stream,
                                              uint64_t object_id,
                                              uint64_t start_time,
                                              uint64_t end_time);

/**
 * @brief Free trajectory statistics
 */
void urbis_trajectory_stats_free(UrbisTrajectoryStats *stats);

/**
 * @brief Get trajectory path as array of points
 * @param stream Stream context
 * @param object_id Object to analyze
 * @param start_time Start of time range
 * @param end_time End of time range
 * @param count Output: number of points
 * @return Array of points (caller must free)
 */
Point* urbis_trajectory_path(UrbisStreamHandle *stream, uint64_t object_id,
                              uint64_t start_time, uint64_t end_time,
                              size_t *count);

/**
 * @brief Get simplified trajectory path (Douglas-Peucker)
 * @param tolerance Simplification tolerance in meters
 */
Point* urbis_trajectory_simplified(UrbisStreamHandle *stream, uint64_t object_id,
                                    uint64_t start_time, uint64_t end_time,
                                    double tolerance, size_t *count);

/* ----------------------------------------------------------------------------
 * Event Handling
 * --------------------------------------------------------------------------- */

/**
 * @brief Stream event info
 */
typedef struct {
    int event_type;          /**< 1=geofence, 2=proximity, 3-6=movement */
    uint64_t timestamp;
    uint64_t object_id;
    uint64_t zone_id;        /**< For geofence events */
    uint64_t other_object;   /**< For proximity events */
    double x, y;
    double distance;
    double speed;
} UrbisStreamEvent;

/**
 * @brief Poll for next event (non-blocking)
 * @return Event or NULL if queue is empty (caller must free)
 */
UrbisStreamEvent* urbis_stream_poll_event(UrbisStreamHandle *stream);

/**
 * @brief Wait for next event (blocking)
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return Event or NULL on timeout (caller must free)
 */
UrbisStreamEvent* urbis_stream_wait_event(UrbisStreamHandle *stream, 
                                           uint64_t timeout_ms);

/**
 * @brief Get pending event count
 */
size_t urbis_stream_event_count(UrbisStreamHandle *stream);

/**
 * @brief Free a stream event
 */
void urbis_stream_event_free(UrbisStreamEvent *event);

/* ----------------------------------------------------------------------------
 * Statistics
 * --------------------------------------------------------------------------- */

/**
 * @brief Stream statistics
 */
typedef struct {
    size_t tracked_objects;
    size_t geofence_zones;
    size_t proximity_rules;
    size_t pending_events;
    uint64_t total_updates;
    uint64_t total_events;
} UrbisStreamStats;

/**
 * @brief Get stream statistics
 */
void urbis_stream_get_stats(UrbisStreamHandle *stream, UrbisStreamStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_H */

