/**
 * @file spatial_index.h
 * @brief High-level spatial index coordinating all indexing components
 * 
 * Provides the main spatial indexing functionality that coordinates
 * KD-tree block partitioning, quadtree adjacency lookups, and
 * disk-aware page management.
 */

#ifndef URBIS_SPATIAL_INDEX_H
#define URBIS_SPATIAL_INDEX_H

#include "geometry.h"
#include "kdtree.h"
#include "quadtree.h"
#include "page.h"
#include "disk_manager.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define SI_DEFAULT_BLOCK_SIZE 1024     /**< Default objects per block */
#define SI_DEFAULT_PAGE_CAPACITY 64    /**< Default objects per page */

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Spatial index configuration
 */
typedef struct {
    size_t block_size;                 /**< Max objects per block */
    size_t page_capacity;              /**< Max objects per page */
    size_t cache_size;                 /**< Page cache size */
    bool build_quadtree;               /**< Build quadtree for adjacency */
    bool persist;                      /**< Persist to disk */
    char *data_path;                   /**< Path for data file */
} SpatialIndexConfig;

/**
 * @brief Block representing a partition of the spatial data
 */
typedef struct {
    uint32_t block_id;                 /**< Unique block identifier */
    MBR bounds;                        /**< Spatial bounds of block */
    Point centroid;                    /**< Block centroid */
    DiskTrack *track;                  /**< Associated disk track */
    size_t object_count;               /**< Number of objects in block */
} SpatialBlock;

/**
 * @brief Query result containing spatial objects
 */
typedef struct {
    SpatialObject **objects;           /**< Array of object pointers */
    size_t count;                      /**< Number of objects */
    size_t capacity;                   /**< Array capacity */
    uint32_t *page_ids;                /**< Pages accessed */
    size_t pages_accessed;             /**< Number of pages accessed */
} SpatialQueryResult;

/**
 * @brief Adjacent pages result
 */
typedef struct {
    Page **pages;                      /**< Array of adjacent pages */
    size_t count;                      /**< Number of pages */
    uint32_t *track_ids;               /**< Track IDs (for seek estimation) */
} AdjacentPagesResult;

/**
 * @brief Index statistics
 */
typedef struct {
    size_t total_objects;              /**< Total spatial objects */
    size_t total_blocks;               /**< Number of blocks */
    size_t total_pages;                /**< Number of pages */
    size_t total_tracks;               /**< Number of tracks */
    size_t kdtree_depth;               /**< KD-tree depth */
    size_t quadtree_depth;             /**< Quadtree depth */
    double avg_objects_per_page;       /**< Average objects per page */
    double page_utilization;           /**< Average page utilization */
    MBR bounds;                        /**< Overall spatial bounds */
} SpatialIndexStats;

/**
 * @brief Main spatial index structure
 */
typedef struct {
    SpatialIndexConfig config;
    KDTree block_tree;                 /**< KD-tree for block partitioning */
    QuadTree *page_tree;               /**< Quadtree for page adjacency */
    DiskManager disk;                  /**< Disk manager */
    SpatialBlock *blocks;              /**< Array of blocks */
    size_t block_count;                /**< Number of blocks */
    size_t block_capacity;             /**< Block array capacity */
    uint64_t next_object_id;           /**< Next object ID */
    uint32_t next_block_id;            /**< Next block ID */
    bool is_built;                     /**< True if index is built */
    MBR bounds;                        /**< Overall bounds */
} SpatialIndex;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    SI_OK = 0,
    SI_ERR_NULL_PTR = -1,
    SI_ERR_ALLOC = -2,
    SI_ERR_NOT_BUILT = -3,
    SI_ERR_NOT_FOUND = -4,
    SI_ERR_FULL = -5,
    SI_ERR_IO = -6,
    SI_ERR_INVALID = -7
} SpatialIndexError;

/* ============================================================================
 * Spatial Index Operations
 * ============================================================================ */

/**
 * @brief Get default configuration
 */
SpatialIndexConfig spatial_index_default_config(void);

/**
 * @brief Create a new spatial index
 */
SpatialIndex* spatial_index_create(const SpatialIndexConfig *config);

/**
 * @brief Initialize an existing spatial index structure
 */
int spatial_index_init(SpatialIndex *idx, const SpatialIndexConfig *config);

/**
 * @brief Free spatial index resources
 */
void spatial_index_free(SpatialIndex *idx);

/**
 * @brief Destroy a spatial index created with spatial_index_create
 */
void spatial_index_destroy(SpatialIndex *idx);

/**
 * @brief Insert a spatial object into the index
 */
int spatial_index_insert(SpatialIndex *idx, SpatialObject *obj);

/**
 * @brief Bulk insert spatial objects
 */
int spatial_index_bulk_insert(SpatialIndex *idx, SpatialObject *objects, size_t count);

/**
 * @brief Remove a spatial object by ID
 */
int spatial_index_remove(SpatialIndex *idx, uint64_t object_id);

/**
 * @brief Build/rebuild the spatial index
 * This partitions data into blocks using KD-tree and builds the quadtree
 */
int spatial_index_build(SpatialIndex *idx);

/**
 * @brief Find all objects intersecting a region
 */
int spatial_index_query_range(SpatialIndex *idx, const MBR *range,
                               SpatialQueryResult *result);

/**
 * @brief Find objects at a specific point
 */
int spatial_index_query_point(SpatialIndex *idx, Point p,
                               SpatialQueryResult *result);

/**
 * @brief Find k nearest neighbors to a point
 */
int spatial_index_query_knn(SpatialIndex *idx, Point p, size_t k,
                             SpatialQueryResult *result);

/**
 * @brief Find adjacent pages to a region (uses quadtree)
 */
int spatial_index_find_adjacent_pages(SpatialIndex *idx, const MBR *region,
                                       AdjacentPagesResult *result);

/**
 * @brief Get object by ID
 */
SpatialObject* spatial_index_get(SpatialIndex *idx, uint64_t object_id);

/**
 * @brief Update an object's geometry
 */
int spatial_index_update(SpatialIndex *idx, uint64_t object_id,
                          const SpatialObject *new_obj);

/* ============================================================================
 * Block Operations
 * ============================================================================ */

/**
 * @brief Get block containing a point
 */
SpatialBlock* spatial_index_get_block(SpatialIndex *idx, Point p);

/**
 * @brief Get blocks intersecting a region
 */
int spatial_index_query_blocks(SpatialIndex *idx, const MBR *region,
                                SpatialBlock ***blocks, size_t *count);

/**
 * @brief Get all blocks
 */
int spatial_index_get_all_blocks(SpatialIndex *idx, 
                                  SpatialBlock ***blocks, size_t *count);

/* ============================================================================
 * Statistics and Utilities
 * ============================================================================ */

/**
 * @brief Get index statistics
 */
void spatial_index_stats(const SpatialIndex *idx, SpatialIndexStats *stats);

/**
 * @brief Optimize index for better query performance
 */
int spatial_index_optimize(SpatialIndex *idx);

/**
 * @brief Save index to disk
 */
int spatial_index_save(SpatialIndex *idx, const char *path);

/**
 * @brief Load index from disk
 */
int spatial_index_load(SpatialIndex *idx, const char *path);

/**
 * @brief Clear all data from index
 */
void spatial_index_clear(SpatialIndex *idx);

/**
 * @brief Print index statistics
 */
void spatial_index_print_stats(const SpatialIndex *idx, FILE *out);

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

/**
 * @brief Initialize a query result
 */
int spatial_result_init(SpatialQueryResult *result, size_t capacity);

/**
 * @brief Free query result resources
 */
void spatial_result_free(SpatialQueryResult *result);

/**
 * @brief Clear query result
 */
void spatial_result_clear(SpatialQueryResult *result);

/**
 * @brief Add an object to query result
 */
int spatial_result_add(SpatialQueryResult *result, SpatialObject *obj);

/* ============================================================================
 * Adjacent Pages Result Operations
 * ============================================================================ */

/**
 * @brief Initialize adjacent pages result
 */
int adjacent_result_init(AdjacentPagesResult *result, size_t capacity);

/**
 * @brief Free adjacent pages result
 */
void adjacent_result_free(AdjacentPagesResult *result);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_SPATIAL_INDEX_H */

