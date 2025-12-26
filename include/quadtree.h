/**
 * @file quadtree.h
 * @brief Quadtree implementation for adjacent page lookups
 * 
 * Provides a quadtree structure for efficient spatial queries,
 * particularly for finding adjacent pages in the disk-based
 * spatial index.
 */

#ifndef URBIS_QUADTREE_H
#define URBIS_QUADTREE_H

#include "geometry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define QT_MAX_DEPTH 20           /**< Maximum quadtree depth */
#define QT_DEFAULT_CAPACITY 8     /**< Default items per node before split */

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Quadrant enumeration
 */
typedef enum {
    QT_NW = 0,  /**< Northwest (top-left) */
    QT_NE = 1,  /**< Northeast (top-right) */
    QT_SW = 2,  /**< Southwest (bottom-left) */
    QT_SE = 3   /**< Southeast (bottom-right) */
} Quadrant;

/**
 * @brief Item stored in quadtree
 */
typedef struct {
    uint64_t id;          /**< Unique identifier */
    MBR bounds;           /**< Spatial bounds of item */
    Point centroid;       /**< Centroid for point queries */
    void *data;           /**< User data */
} QTItem;

/**
 * @brief Quadtree node
 */
typedef struct QTNode {
    MBR bounds;                   /**< Spatial bounds of this node */
    QTItem *items;                /**< Items stored in this node */
    size_t item_count;            /**< Number of items */
    size_t item_capacity;         /**< Capacity of items array */
    struct QTNode *children[4];   /**< Child nodes (NW, NE, SW, SE) */
    int depth;                    /**< Depth in tree */
    bool is_leaf;                 /**< True if this is a leaf node */
} QTNode;

/**
 * @brief Quadtree structure
 */
typedef struct {
    QTNode *root;
    size_t total_items;           /**< Total items in tree */
    size_t node_capacity;         /**< Items per node before split */
    int max_depth;                /**< Maximum allowed depth */
} QuadTree;

/**
 * @brief Query result for quadtree operations
 */
typedef struct {
    QTItem *items;
    size_t count;
    size_t capacity;
} QTQueryResult;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    QT_OK = 0,
    QT_ERR_NULL_PTR = -1,
    QT_ERR_ALLOC = -2,
    QT_ERR_BOUNDS = -3,
    QT_ERR_NOT_FOUND = -4,
    QT_ERR_FULL = -5
} QTError;

/* ============================================================================
 * Quadtree Operations
 * ============================================================================ */

/**
 * @brief Create a new quadtree
 * @param bounds Overall spatial bounds
 * @param node_capacity Items per node before splitting
 * @param max_depth Maximum tree depth
 */
QuadTree* quadtree_create(MBR bounds, size_t node_capacity, int max_depth);

/**
 * @brief Initialize an existing quadtree structure
 */
int quadtree_init(QuadTree *qt, MBR bounds, size_t node_capacity, int max_depth);

/**
 * @brief Free all quadtree resources
 */
void quadtree_free(QuadTree *qt);

/**
 * @brief Destroy a quadtree created with quadtree_create
 */
void quadtree_destroy(QuadTree *qt);

/**
 * @brief Insert an item into the quadtree
 */
int quadtree_insert(QuadTree *qt, uint64_t id, MBR bounds, void *data);

/**
 * @brief Insert an item with explicit centroid
 */
int quadtree_insert_with_centroid(QuadTree *qt, uint64_t id, MBR bounds, 
                                   Point centroid, void *data);

/**
 * @brief Remove an item from the quadtree
 */
int quadtree_remove(QuadTree *qt, uint64_t id);

/**
 * @brief Find all items intersecting a query region
 */
int quadtree_query_range(const QuadTree *qt, const MBR *range, QTQueryResult *result);

/**
 * @brief Find all items containing a point
 */
int quadtree_query_point(const QuadTree *qt, Point p, QTQueryResult *result);

/**
 * @brief Find adjacent items to a given item (items with intersecting/touching bounds)
 * @param qt The quadtree
 * @param id ID of the reference item
 * @param result Output: adjacent items
 */
int quadtree_find_adjacent(const QuadTree *qt, uint64_t id, QTQueryResult *result);

/**
 * @brief Find adjacent items to a region
 */
int quadtree_find_adjacent_to_region(const QuadTree *qt, const MBR *region, 
                                      QTQueryResult *result);

/**
 * @brief Find all items within a distance of a point
 */
int quadtree_query_radius(const QuadTree *qt, Point center, double radius,
                          QTQueryResult *result);

/**
 * @brief Get item by ID
 */
int quadtree_get(const QuadTree *qt, uint64_t id, QTItem *item);

/**
 * @brief Update item bounds (removes and reinserts)
 */
int quadtree_update(QuadTree *qt, uint64_t id, MBR new_bounds);

/**
 * @brief Get all items in the quadtree
 */
int quadtree_get_all(const QuadTree *qt, QTQueryResult *result);

/**
 * @brief Get quadtree statistics
 */
void quadtree_stats(const QuadTree *qt, size_t *total_items, size_t *total_nodes,
                    size_t *max_depth, size_t *leaf_count);

/**
 * @brief Clear all items from quadtree
 */
void quadtree_clear(QuadTree *qt);

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

/**
 * @brief Initialize a query result
 */
int qtresult_init(QTQueryResult *result, size_t capacity);

/**
 * @brief Free query result resources
 */
void qtresult_free(QTQueryResult *result);

/**
 * @brief Clear query result (keep capacity)
 */
void qtresult_clear(QTQueryResult *result);

/**
 * @brief Add an item to query result
 */
int qtresult_add(QTQueryResult *result, const QTItem *item);

/* ============================================================================
 * Node Operations (for advanced usage)
 * ============================================================================ */

/**
 * @brief Create a new quadtree node
 */
QTNode* qtnode_create(MBR bounds, size_t capacity, int depth);

/**
 * @brief Free a node and all children
 */
void qtnode_free(QTNode *node);

/**
 * @brief Get the quadrant of a point within a node's bounds
 */
Quadrant qtnode_get_quadrant(const QTNode *node, Point p);

/**
 * @brief Get bounds of a specific quadrant
 */
MBR qtnode_quadrant_bounds(const QTNode *node, Quadrant q);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_QUADTREE_H */

