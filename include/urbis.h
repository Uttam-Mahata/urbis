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

#ifdef __cplusplus
}
#endif

#endif /* URBIS_H */

