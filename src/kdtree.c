/**
 * @file kdtree.c
 * @brief KD-tree implementation for spatial partitioning
 */

#include "kdtree.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define GROWTH_FACTOR 2

/**
 * @brief Compare function for sorting points by X coordinate
 */
static int compare_by_x(const void *a, const void *b) {
    const KDPointData *pa = (const KDPointData *)a;
    const KDPointData *pb = (const KDPointData *)b;
    if (pa->point.x < pb->point.x) return -1;
    if (pa->point.x > pb->point.x) return 1;
    return 0;
}

/**
 * @brief Compare function for sorting points by Y coordinate
 */
static int compare_by_y(const void *a, const void *b) {
    const KDPointData *pa = (const KDPointData *)a;
    const KDPointData *pb = (const KDPointData *)b;
    if (pa->point.y < pb->point.y) return -1;
    if (pa->point.y > pb->point.y) return 1;
    return 0;
}

/**
 * @brief Recursively build a balanced KD-tree
 */
static KDNode* build_tree_recursive(KDPointData *points, size_t count, int depth) {
    if (count == 0) return NULL;
    
    int dim = depth % 2;
    
    /* Sort by current dimension */
    if (dim == 0) {
        qsort(points, count, sizeof(KDPointData), compare_by_x);
    } else {
        qsort(points, count, sizeof(KDPointData), compare_by_y);
    }
    
    /* Find median */
    size_t median = count / 2;
    
    /* Create node */
    KDNode *node = kdnode_create(points[median].point, points[median].object_id,
                                  points[median].data, dim);
    if (!node) return NULL;
    
    /* Build subtrees */
    node->left = build_tree_recursive(points, median, depth + 1);
    node->right = build_tree_recursive(points + median + 1, count - median - 1, depth + 1);
    
    /* Update bounds and size */
    kdnode_update_bounds(node);
    
    return node;
}

/**
 * @brief Insert a node into the tree recursively
 */
static KDNode* insert_recursive(KDNode *node, Point p, uint64_t object_id, 
                                 void *data, int depth) {
    if (!node) {
        return kdnode_create(p, object_id, data, depth % 2);
    }
    
    int dim = node->split_dim;
    double coord = (dim == 0) ? p.x : p.y;
    double split = (dim == 0) ? node->point.x : node->point.y;
    
    if (coord < split) {
        node->left = insert_recursive(node->left, p, object_id, data, depth + 1);
    } else {
        node->right = insert_recursive(node->right, p, object_id, data, depth + 1);
    }
    
    /* Update bounds */
    mbr_expand_point(&node->bounds, &p);
    node->subtree_size++;
    
    return node;
}

/**
 * @brief Nearest neighbor search state
 */
typedef struct {
    Point query;
    Point best;
    uint64_t best_id;
    void *best_data;
    double best_dist_sq;
} NNState;

/**
 * @brief Recursive nearest neighbor search
 */
static void nearest_recursive(const KDNode *node, NNState *state) {
    if (!node) return;
    
    /* Check current node */
    double dist_sq = point_distance_sq(&state->query, &node->point);
    if (dist_sq < state->best_dist_sq) {
        state->best_dist_sq = dist_sq;
        state->best = node->point;
        state->best_id = node->object_id;
        state->best_data = node->data;
    }
    
    /* Determine which subtree to search first */
    int dim = node->split_dim;
    double coord = (dim == 0) ? state->query.x : state->query.y;
    double split = (dim == 0) ? node->point.x : node->point.y;
    double diff = coord - split;
    
    KDNode *first = (diff < 0) ? node->left : node->right;
    KDNode *second = (diff < 0) ? node->right : node->left;
    
    /* Search closer subtree first */
    nearest_recursive(first, state);
    
    /* Only search other subtree if it could contain a closer point */
    if (diff * diff < state->best_dist_sq) {
        nearest_recursive(second, state);
    }
}

/**
 * @brief Range query recursive helper
 */
static void range_query_recursive(const KDNode *node, const MBR *range, 
                                   KDQueryResult *result) {
    if (!node) return;
    
    /* Skip if node bounds don't intersect query range */
    if (!mbr_intersects(&node->bounds, range)) return;
    
    /* Check current node */
    if (mbr_contains_point(range, &node->point)) {
        kdresult_add(result, node->point, node->object_id, node->data);
    }
    
    /* Recurse into children */
    range_query_recursive(node->left, range, result);
    range_query_recursive(node->right, range, result);
}

/**
 * @brief Calculate tree depth recursively
 */
static size_t depth_recursive(const KDNode *node) {
    if (!node) return 0;
    size_t left_depth = depth_recursive(node->left);
    size_t right_depth = depth_recursive(node->right);
    return 1 + (left_depth > right_depth ? left_depth : right_depth);
}

/**
 * @brief Collect leaf bounds for partitioning
 */
static void collect_leaf_bounds(const KDNode *node, size_t max_size, 
                                 MBR **bounds, size_t *count, size_t *capacity) {
    if (!node) return;
    
    /* If subtree is small enough, treat as a block */
    if (node->subtree_size <= max_size || (!node->left && !node->right)) {
        if (*count >= *capacity) {
            size_t new_cap = (*capacity) * 2;
            MBR *new_bounds = (MBR *)realloc(*bounds, new_cap * sizeof(MBR));
            if (!new_bounds) return;
            *bounds = new_bounds;
            *capacity = new_cap;
        }
        (*bounds)[(*count)++] = node->bounds;
        return;
    }
    
    collect_leaf_bounds(node->left, max_size, bounds, count, capacity);
    collect_leaf_bounds(node->right, max_size, bounds, count, capacity);
}

/* ============================================================================
 * KD-Tree Operations
 * ============================================================================ */

int kdtree_init(KDTree *tree) {
    if (!tree) return KD_ERR_NULL_PTR;
    
    tree->root = NULL;
    tree->size = 0;
    tree->bounds = mbr_empty();
    
    return KD_OK;
}

void kdtree_free(KDTree *tree) {
    if (!tree) return;
    kdnode_free(tree->root);
    tree->root = NULL;
    tree->size = 0;
    tree->bounds = mbr_empty();
}

int kdtree_insert(KDTree *tree, Point p, uint64_t object_id, void *data) {
    if (!tree) return KD_ERR_NULL_PTR;
    
    tree->root = insert_recursive(tree->root, p, object_id, data, 0);
    if (!tree->root) return KD_ERR_ALLOC;
    
    mbr_expand_point(&tree->bounds, &p);
    tree->size++;
    
    return KD_OK;
}

int kdtree_bulk_load(KDTree *tree, KDPointData *points, size_t count) {
    if (!tree) return KD_ERR_NULL_PTR;
    if (count == 0) return KD_OK;
    if (!points) return KD_ERR_NULL_PTR;
    
    /* Free existing tree */
    kdnode_free(tree->root);
    tree->root = NULL;
    tree->size = 0;
    tree->bounds = mbr_empty();
    
    /* Make a copy of points array (will be modified during sort) */
    KDPointData *points_copy = (KDPointData *)malloc(count * sizeof(KDPointData));
    if (!points_copy) return KD_ERR_ALLOC;
    memcpy(points_copy, points, count * sizeof(KDPointData));
    
    /* Build balanced tree */
    tree->root = build_tree_recursive(points_copy, count, 0);
    free(points_copy);
    
    if (!tree->root && count > 0) return KD_ERR_ALLOC;
    
    tree->size = count;
    
    /* Compute bounds */
    for (size_t i = 0; i < count; i++) {
        mbr_expand_point(&tree->bounds, &points[i].point);
    }
    
    return KD_OK;
}

int kdtree_nearest(const KDTree *tree, Point query, Point *nearest,
                   uint64_t *object_id, void **data) {
    if (!tree || !nearest) return KD_ERR_NULL_PTR;
    if (!tree->root) return KD_ERR_EMPTY;
    
    NNState state = {
        .query = query,
        .best = tree->root->point,
        .best_id = tree->root->object_id,
        .best_data = tree->root->data,
        .best_dist_sq = DBL_MAX
    };
    
    nearest_recursive(tree->root, &state);
    
    *nearest = state.best;
    if (object_id) *object_id = state.best_id;
    if (data) *data = state.best_data;
    
    return KD_OK;
}

int kdtree_k_nearest(const KDTree *tree, Point query, size_t k, KDQueryResult *result) {
    if (!tree || !result) return KD_ERR_NULL_PTR;
    if (!tree->root) return KD_ERR_EMPTY;
    if (k == 0) return KD_OK;
    
    kdresult_clear(result);
    
    /* Simple approach: collect all points and sort by distance */
    /* For production, use a max-heap for O(n log k) instead of O(n log n) */
    KDQueryResult all;
    int err = kdresult_init(&all, tree->size);
    if (err != KD_OK) return err;
    
    /* Range query entire bounds */
    range_query_recursive(tree->root, &tree->bounds, &all);
    
    /* Sort by distance to query point */
    typedef struct {
        size_t idx;
        double dist_sq;
    } DistEntry;
    
    DistEntry *distances = (DistEntry *)malloc(all.count * sizeof(DistEntry));
    if (!distances) {
        kdresult_free(&all);
        return KD_ERR_ALLOC;
    }
    
    for (size_t i = 0; i < all.count; i++) {
        distances[i].idx = i;
        distances[i].dist_sq = point_distance_sq(&query, &all.points[i]);
    }
    
    /* Simple selection sort for k smallest */
    for (size_t i = 0; i < k && i < all.count; i++) {
        size_t min_idx = i;
        for (size_t j = i + 1; j < all.count; j++) {
            if (distances[j].dist_sq < distances[min_idx].dist_sq) {
                min_idx = j;
            }
        }
        if (min_idx != i) {
            DistEntry tmp = distances[i];
            distances[i] = distances[min_idx];
            distances[min_idx] = tmp;
        }
        
        size_t idx = distances[i].idx;
        kdresult_add(result, all.points[idx], all.ids[idx], all.data[idx]);
    }
    
    free(distances);
    kdresult_free(&all);
    
    return KD_OK;
}

int kdtree_range_query(const KDTree *tree, const MBR *range, KDQueryResult *result) {
    if (!tree || !range || !result) return KD_ERR_NULL_PTR;
    
    kdresult_clear(result);
    
    if (!tree->root) return KD_OK;
    
    range_query_recursive(tree->root, range, result);
    
    return KD_OK;
}

int kdtree_radius_query(const KDTree *tree, Point query, double radius, 
                        KDQueryResult *result) {
    if (!tree || !result) return KD_ERR_NULL_PTR;
    
    /* First do a range query with bounding box */
    MBR range = mbr_create(query.x - radius, query.y - radius,
                           query.x + radius, query.y + radius);
    
    KDQueryResult box_result;
    int err = kdresult_init(&box_result, 64);
    if (err != KD_OK) return err;
    
    err = kdtree_range_query(tree, &range, &box_result);
    if (err != KD_OK) {
        kdresult_free(&box_result);
        return err;
    }
    
    /* Filter by actual distance */
    kdresult_clear(result);
    double radius_sq = radius * radius;
    
    for (size_t i = 0; i < box_result.count; i++) {
        if (point_distance_sq(&query, &box_result.points[i]) <= radius_sq) {
            kdresult_add(result, box_result.points[i], box_result.ids[i], 
                        box_result.data[i]);
        }
    }
    
    kdresult_free(&box_result);
    return KD_OK;
}

KDNode* kdtree_find_leaf(const KDTree *tree, Point p) {
    if (!tree || !tree->root) return NULL;
    
    KDNode *node = tree->root;
    
    while (node) {
        if (!node->left && !node->right) {
            return node;
        }
        
        int dim = node->split_dim;
        double coord = (dim == 0) ? p.x : p.y;
        double split = (dim == 0) ? node->point.x : node->point.y;
        
        if (coord < split) {
            if (!node->left) return node;
            node = node->left;
        } else {
            if (!node->right) return node;
            node = node->right;
        }
    }
    
    return NULL;
}

int kdtree_partition(const KDTree *tree, size_t max_points_per_block,
                     size_t *block_count, MBR **block_bounds) {
    if (!tree || !block_count || !block_bounds) return KD_ERR_NULL_PTR;
    
    *block_count = 0;
    *block_bounds = NULL;
    
    if (!tree->root) return KD_OK;
    
    size_t capacity = 16;
    *block_bounds = (MBR *)malloc(capacity * sizeof(MBR));
    if (!*block_bounds) return KD_ERR_ALLOC;
    
    collect_leaf_bounds(tree->root, max_points_per_block, block_bounds, 
                        block_count, &capacity);
    
    return KD_OK;
}

size_t kdtree_depth(const KDTree *tree) {
    if (!tree) return 0;
    return depth_recursive(tree->root);
}

bool kdtree_is_balanced(const KDTree *tree) {
    if (!tree || !tree->root) return true;
    
    size_t depth = kdtree_depth(tree);
    size_t optimal = (size_t)ceil(log2((double)(tree->size + 1)));
    
    /* Allow some slack (within factor of 2 of optimal) */
    return depth <= 2 * optimal;
}

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

int kdresult_init(KDQueryResult *result, size_t capacity) {
    if (!result) return KD_ERR_NULL_PTR;
    
    result->count = 0;
    result->capacity = capacity > 0 ? capacity : 16;
    
    result->ids = (uint64_t *)malloc(result->capacity * sizeof(uint64_t));
    result->points = (Point *)malloc(result->capacity * sizeof(Point));
    result->data = (void **)malloc(result->capacity * sizeof(void *));
    
    if (!result->ids || !result->points || !result->data) {
        free(result->ids);
        free(result->points);
        free(result->data);
        result->ids = NULL;
        result->points = NULL;
        result->data = NULL;
        result->capacity = 0;
        return KD_ERR_ALLOC;
    }
    
    return KD_OK;
}

void kdresult_free(KDQueryResult *result) {
    if (!result) return;
    free(result->ids);
    free(result->points);
    free(result->data);
    result->ids = NULL;
    result->points = NULL;
    result->data = NULL;
    result->count = 0;
    result->capacity = 0;
}

void kdresult_clear(KDQueryResult *result) {
    if (!result) return;
    result->count = 0;
}

int kdresult_add(KDQueryResult *result, Point p, uint64_t id, void *data) {
    if (!result) return KD_ERR_NULL_PTR;
    
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * GROWTH_FACTOR;
        
        uint64_t *new_ids = (uint64_t *)realloc(result->ids, new_cap * sizeof(uint64_t));
        Point *new_points = (Point *)realloc(result->points, new_cap * sizeof(Point));
        void **new_data = (void **)realloc(result->data, new_cap * sizeof(void *));
        
        if (!new_ids || !new_points || !new_data) {
            return KD_ERR_ALLOC;
        }
        
        result->ids = new_ids;
        result->points = new_points;
        result->data = new_data;
        result->capacity = new_cap;
    }
    
    result->ids[result->count] = id;
    result->points[result->count] = p;
    result->data[result->count] = data;
    result->count++;
    
    return KD_OK;
}

/* ============================================================================
 * Node Operations
 * ============================================================================ */

KDNode* kdnode_create(Point p, uint64_t object_id, void *data, int split_dim) {
    KDNode *node = (KDNode *)calloc(1, sizeof(KDNode));
    if (!node) return NULL;
    
    node->point = p;
    node->object_id = object_id;
    node->data = data;
    node->split_dim = split_dim;
    node->left = NULL;
    node->right = NULL;
    node->bounds = mbr_create(p.x, p.y, p.x, p.y);
    node->subtree_size = 1;
    
    return node;
}

void kdnode_free(KDNode *node) {
    if (!node) return;
    kdnode_free(node->left);
    kdnode_free(node->right);
    free(node);
}

void kdnode_update_bounds(KDNode *node) {
    if (!node) return;
    
    node->bounds = mbr_create(node->point.x, node->point.y, 
                               node->point.x, node->point.y);
    node->subtree_size = 1;
    
    if (node->left) {
        mbr_expand_mbr(&node->bounds, &node->left->bounds);
        node->subtree_size += node->left->subtree_size;
    }
    
    if (node->right) {
        mbr_expand_mbr(&node->bounds, &node->right->bounds);
        node->subtree_size += node->right->subtree_size;
    }
}

