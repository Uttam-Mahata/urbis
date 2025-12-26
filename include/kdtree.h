/**
 * @file kdtree.h
 * @brief KD-tree implementation for spatial partitioning
 * 
 * Provides a 2D KD-tree for partitioning city map data into blocks
 * based on object centroids. Used for both block partitioning and
 * page allocation within blocks.
 */

#ifndef URBIS_KDTREE_H
#define URBIS_KDTREE_H

#include "geometry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief KD-tree node
 */
typedef struct KDNode {
    Point point;              /**< Split point (centroid) */
    uint64_t object_id;       /**< Associated spatial object ID */
    void *data;               /**< User data pointer */
    int split_dim;            /**< Split dimension (0=x, 1=y) */
    struct KDNode *left;      /**< Left subtree (values < split) */
    struct KDNode *right;     /**< Right subtree (values >= split) */
    MBR bounds;               /**< Bounding box of this subtree */
    uint32_t subtree_size;    /**< Number of nodes in subtree */
} KDNode;

/**
 * @brief KD-tree structure
 */
typedef struct {
    KDNode *root;
    size_t size;              /**< Total number of nodes */
    MBR bounds;               /**< Overall bounds */
} KDTree;

/**
 * @brief Result list for range queries
 */
typedef struct {
    uint64_t *ids;            /**< Array of object IDs */
    Point *points;            /**< Array of points */
    void **data;              /**< Array of data pointers */
    size_t count;
    size_t capacity;
} KDQueryResult;

/**
 * @brief Point with associated data for bulk loading
 */
typedef struct {
    Point point;
    uint64_t object_id;
    void *data;
} KDPointData;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    KD_OK = 0,
    KD_ERR_NULL_PTR = -1,
    KD_ERR_ALLOC = -2,
    KD_ERR_EMPTY = -3,
    KD_ERR_NOT_FOUND = -4
} KDError;

/* ============================================================================
 * KD-Tree Operations
 * ============================================================================ */

/**
 * @brief Initialize an empty KD-tree
 */
int kdtree_init(KDTree *tree);

/**
 * @brief Free all KD-tree resources
 */
void kdtree_free(KDTree *tree);

/**
 * @brief Insert a point into the KD-tree
 */
int kdtree_insert(KDTree *tree, Point p, uint64_t object_id, void *data);

/**
 * @brief Bulk load points into the KD-tree (more balanced than sequential inserts)
 * @param points Array of point data to insert
 * @param count Number of points
 */
int kdtree_bulk_load(KDTree *tree, KDPointData *points, size_t count);

/**
 * @brief Find the nearest neighbor to a query point
 * @param tree The KD-tree
 * @param query Query point
 * @param nearest Output: nearest point found
 * @param object_id Output: ID of nearest object
 * @param data Output: data pointer of nearest node
 */
int kdtree_nearest(const KDTree *tree, Point query, Point *nearest, 
                   uint64_t *object_id, void **data);

/**
 * @brief Find k nearest neighbors
 * @param tree The KD-tree
 * @param query Query point
 * @param k Number of neighbors to find
 * @param result Output: query result (must be initialized)
 */
int kdtree_k_nearest(const KDTree *tree, Point query, size_t k, KDQueryResult *result);

/**
 * @brief Find all points within a bounding box
 */
int kdtree_range_query(const KDTree *tree, const MBR *range, KDQueryResult *result);

/**
 * @brief Find all points within a radius of query point
 */
int kdtree_radius_query(const KDTree *tree, Point query, double radius, KDQueryResult *result);

/**
 * @brief Get the leaf node containing a point
 * @return Pointer to leaf node, or NULL if tree is empty
 */
KDNode* kdtree_find_leaf(const KDTree *tree, Point p);

/**
 * @brief Partition the KD-tree into regions (blocks)
 * @param tree The KD-tree
 * @param max_points_per_block Maximum points per block (leaf)
 * @param block_count Output: number of blocks created
 * @param block_bounds Output: array of block bounds (caller must free)
 */
int kdtree_partition(const KDTree *tree, size_t max_points_per_block,
                     size_t *block_count, MBR **block_bounds);

/**
 * @brief Get the depth of the tree
 */
size_t kdtree_depth(const KDTree *tree);

/**
 * @brief Check if tree is balanced
 */
bool kdtree_is_balanced(const KDTree *tree);

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

/**
 * @brief Initialize a query result structure
 */
int kdresult_init(KDQueryResult *result, size_t capacity);

/**
 * @brief Free query result resources
 */
void kdresult_free(KDQueryResult *result);

/**
 * @brief Clear query result (keep capacity)
 */
void kdresult_clear(KDQueryResult *result);

/**
 * @brief Add a result entry
 */
int kdresult_add(KDQueryResult *result, Point p, uint64_t id, void *data);

/* ============================================================================
 * Node Operations (for advanced usage)
 * ============================================================================ */

/**
 * @brief Create a new KD node
 */
KDNode* kdnode_create(Point p, uint64_t object_id, void *data, int split_dim);

/**
 * @brief Free a node and all its children
 */
void kdnode_free(KDNode *node);

/**
 * @brief Update node bounds based on children
 */
void kdnode_update_bounds(KDNode *node);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_KDTREE_H */

