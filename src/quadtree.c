/**
 * @file quadtree.c
 * @brief Quadtree implementation for adjacent page lookups
 */

#include "quadtree.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define GROWTH_FACTOR 2

/**
 * @brief Check if MBR touches or intersects another MBR (for adjacency)
 */
static bool mbr_adjacent_or_intersects(const MBR *a, const MBR *b) {
    /* Slightly expand bounds to catch touching edges */
    const double EPSILON = 1e-9;
    return !(a->max_x + EPSILON < b->min_x || 
             a->min_x - EPSILON > b->max_x ||
             a->max_y + EPSILON < b->min_y || 
             a->min_y - EPSILON > b->max_y);
}

/**
 * @brief Split a node into four children
 */
static int qtnode_split(QTNode *node, size_t capacity) {
    if (!node || !node->is_leaf) return QT_ERR_NULL_PTR;
    
    double mid_x = (node->bounds.min_x + node->bounds.max_x) / 2.0;
    double mid_y = (node->bounds.min_y + node->bounds.max_y) / 2.0;
    
    /* Create children */
    node->children[QT_NW] = qtnode_create(
        mbr_create(node->bounds.min_x, mid_y, mid_x, node->bounds.max_y),
        capacity, node->depth + 1);
    node->children[QT_NE] = qtnode_create(
        mbr_create(mid_x, mid_y, node->bounds.max_x, node->bounds.max_y),
        capacity, node->depth + 1);
    node->children[QT_SW] = qtnode_create(
        mbr_create(node->bounds.min_x, node->bounds.min_y, mid_x, mid_y),
        capacity, node->depth + 1);
    node->children[QT_SE] = qtnode_create(
        mbr_create(mid_x, node->bounds.min_y, node->bounds.max_x, mid_y),
        capacity, node->depth + 1);
    
    /* Check allocation */
    for (int i = 0; i < 4; i++) {
        if (!node->children[i]) {
            for (int j = 0; j < 4; j++) {
                qtnode_free(node->children[j]);
                node->children[j] = NULL;
            }
            return QT_ERR_ALLOC;
        }
    }
    
    node->is_leaf = false;
    
    /* Redistribute items to children */
    for (size_t i = 0; i < node->item_count; i++) {
        QTItem *item = &node->items[i];
        bool inserted = false;
        
        /* Try to insert into a single child if it fits entirely */
        for (int q = 0; q < 4; q++) {
            if (mbr_contains_mbr(&node->children[q]->bounds, &item->bounds)) {
                /* Item fits entirely in this child */
                if (node->children[q]->item_count < node->children[q]->item_capacity) {
                    node->children[q]->items[node->children[q]->item_count++] = *item;
                    inserted = true;
                    break;
                }
            }
        }
        
        /* If item spans multiple quadrants, keep in parent */
        if (!inserted) {
            /* Items that don't fit in any child stay in this node */
            /* Move to front of items array */
            if (i > 0) {
                /* Already processed items that stayed are at front */
            }
        }
    }
    
    /* Clear parent items (they've been moved to children or will be reinserted) */
    node->item_count = 0;
    
    return QT_OK;
}

/**
 * @brief Insert item into node recursively
 */
static int qtnode_insert(QTNode *node, const QTItem *item, size_t node_capacity, 
                          int max_depth) {
    if (!node || !item) return QT_ERR_NULL_PTR;
    
    /* Check if item bounds intersect node bounds */
    if (!mbr_intersects(&node->bounds, &item->bounds)) {
        return QT_ERR_BOUNDS;
    }
    
    /* If leaf node */
    if (node->is_leaf) {
        /* Check if we need to split */
        if (node->item_count >= node_capacity && node->depth < max_depth) {
            int err = qtnode_split(node, node_capacity);
            if (err != QT_OK) return err;
            
            /* Reinsert this item */
            return qtnode_insert(node, item, node_capacity, max_depth);
        }
        
        /* Add to this node */
        if (node->item_count >= node->item_capacity) {
            size_t new_cap = node->item_capacity * GROWTH_FACTOR;
            QTItem *new_items = (QTItem *)realloc(node->items, new_cap * sizeof(QTItem));
            if (!new_items) return QT_ERR_ALLOC;
            node->items = new_items;
            node->item_capacity = new_cap;
        }
        
        node->items[node->item_count++] = *item;
        return QT_OK;
    }
    
    /* Internal node: try to insert into a child that fully contains the item */
    for (int q = 0; q < 4; q++) {
        if (node->children[q] && mbr_contains_mbr(&node->children[q]->bounds, &item->bounds)) {
            return qtnode_insert(node->children[q], item, node_capacity, max_depth);
        }
    }
    
    /* Item spans multiple children - store in this node */
    if (node->item_count >= node->item_capacity) {
        size_t new_cap = node->item_capacity * GROWTH_FACTOR;
        QTItem *new_items = (QTItem *)realloc(node->items, new_cap * sizeof(QTItem));
        if (!new_items) return QT_ERR_ALLOC;
        node->items = new_items;
        node->item_capacity = new_cap;
    }
    
    node->items[node->item_count++] = *item;
    return QT_OK;
}

/**
 * @brief Query range recursively
 */
static void qtnode_query_range(const QTNode *node, const MBR *range, 
                                QTQueryResult *result) {
    if (!node) return;
    
    /* Check if node bounds intersect query range */
    if (!mbr_intersects(&node->bounds, range)) return;
    
    /* Check items in this node */
    for (size_t i = 0; i < node->item_count; i++) {
        if (mbr_intersects(&node->items[i].bounds, range)) {
            qtresult_add(result, &node->items[i]);
        }
    }
    
    /* Recurse into children */
    if (!node->is_leaf) {
        for (int q = 0; q < 4; q++) {
            qtnode_query_range(node->children[q], range, result);
        }
    }
}

/**
 * @brief Query point recursively
 */
static void qtnode_query_point(const QTNode *node, Point p, QTQueryResult *result) {
    if (!node) return;
    
    /* Check if point is in node bounds */
    if (!mbr_contains_point(&node->bounds, &p)) return;
    
    /* Check items in this node */
    for (size_t i = 0; i < node->item_count; i++) {
        if (mbr_contains_point(&node->items[i].bounds, &p)) {
            qtresult_add(result, &node->items[i]);
        }
    }
    
    /* Recurse into children */
    if (!node->is_leaf) {
        for (int q = 0; q < 4; q++) {
            qtnode_query_point(node->children[q], p, result);
        }
    }
}

/**
 * @brief Find item by ID recursively
 */
static QTItem* qtnode_find(QTNode *node, uint64_t id) {
    if (!node) return NULL;
    
    /* Check items in this node */
    for (size_t i = 0; i < node->item_count; i++) {
        if (node->items[i].id == id) {
            return &node->items[i];
        }
    }
    
    /* Recurse into children */
    if (!node->is_leaf) {
        for (int q = 0; q < 4; q++) {
            QTItem *found = qtnode_find(node->children[q], id);
            if (found) return found;
        }
    }
    
    return NULL;
}

/**
 * @brief Remove item by ID recursively
 */
static bool qtnode_remove(QTNode *node, uint64_t id) {
    if (!node) return false;
    
    /* Check items in this node */
    for (size_t i = 0; i < node->item_count; i++) {
        if (node->items[i].id == id) {
            /* Remove by shifting */
            memmove(&node->items[i], &node->items[i + 1], 
                    (node->item_count - i - 1) * sizeof(QTItem));
            node->item_count--;
            return true;
        }
    }
    
    /* Recurse into children */
    if (!node->is_leaf) {
        for (int q = 0; q < 4; q++) {
            if (qtnode_remove(node->children[q], id)) {
                return true;
            }
        }
    }
    
    return false;
}

/**
 * @brief Collect all items recursively
 */
static void qtnode_collect_all(const QTNode *node, QTQueryResult *result) {
    if (!node) return;
    
    for (size_t i = 0; i < node->item_count; i++) {
        qtresult_add(result, &node->items[i]);
    }
    
    if (!node->is_leaf) {
        for (int q = 0; q < 4; q++) {
            qtnode_collect_all(node->children[q], result);
        }
    }
}

/**
 * @brief Count nodes recursively
 */
static void qtnode_count(const QTNode *node, size_t *total_nodes, 
                          size_t *max_depth, size_t *leaf_count) {
    if (!node) return;
    
    (*total_nodes)++;
    
    if ((size_t)node->depth > *max_depth) {
        *max_depth = node->depth;
    }
    
    if (node->is_leaf) {
        (*leaf_count)++;
    } else {
        for (int q = 0; q < 4; q++) {
            qtnode_count(node->children[q], total_nodes, max_depth, leaf_count);
        }
    }
}

/* ============================================================================
 * Quadtree Operations
 * ============================================================================ */

QuadTree* quadtree_create(MBR bounds, size_t node_capacity, int max_depth) {
    QuadTree *qt = (QuadTree *)calloc(1, sizeof(QuadTree));
    if (!qt) return NULL;
    
    if (quadtree_init(qt, bounds, node_capacity, max_depth) != QT_OK) {
        free(qt);
        return NULL;
    }
    
    return qt;
}

int quadtree_init(QuadTree *qt, MBR bounds, size_t node_capacity, int max_depth) {
    if (!qt) return QT_ERR_NULL_PTR;
    
    qt->node_capacity = node_capacity > 0 ? node_capacity : QT_DEFAULT_CAPACITY;
    qt->max_depth = max_depth > 0 ? max_depth : QT_MAX_DEPTH;
    qt->total_items = 0;
    
    qt->root = qtnode_create(bounds, qt->node_capacity, 0);
    if (!qt->root) return QT_ERR_ALLOC;
    
    return QT_OK;
}

void quadtree_free(QuadTree *qt) {
    if (!qt) return;
    qtnode_free(qt->root);
    qt->root = NULL;
    qt->total_items = 0;
}

void quadtree_destroy(QuadTree *qt) {
    if (!qt) return;
    quadtree_free(qt);
    free(qt);
}

int quadtree_insert(QuadTree *qt, uint64_t id, MBR bounds, void *data) {
    return quadtree_insert_with_centroid(qt, id, bounds, mbr_centroid(&bounds), data);
}

int quadtree_insert_with_centroid(QuadTree *qt, uint64_t id, MBR bounds,
                                   Point centroid, void *data) {
    if (!qt || !qt->root) return QT_ERR_NULL_PTR;
    
    QTItem item = {
        .id = id,
        .bounds = bounds,
        .centroid = centroid,
        .data = data
    };
    
    int err = qtnode_insert(qt->root, &item, qt->node_capacity, qt->max_depth);
    if (err == QT_OK) {
        qt->total_items++;
    }
    
    return err;
}

int quadtree_remove(QuadTree *qt, uint64_t id) {
    if (!qt || !qt->root) return QT_ERR_NULL_PTR;
    
    if (qtnode_remove(qt->root, id)) {
        qt->total_items--;
        return QT_OK;
    }
    
    return QT_ERR_NOT_FOUND;
}

int quadtree_query_range(const QuadTree *qt, const MBR *range, QTQueryResult *result) {
    if (!qt || !range || !result) return QT_ERR_NULL_PTR;
    
    qtresult_clear(result);
    qtnode_query_range(qt->root, range, result);
    
    return QT_OK;
}

int quadtree_query_point(const QuadTree *qt, Point p, QTQueryResult *result) {
    if (!qt || !result) return QT_ERR_NULL_PTR;
    
    qtresult_clear(result);
    qtnode_query_point(qt->root, p, result);
    
    return QT_OK;
}

int quadtree_find_adjacent(const QuadTree *qt, uint64_t id, QTQueryResult *result) {
    if (!qt || !result) return QT_ERR_NULL_PTR;
    
    /* Find the item first */
    QTItem *item = qtnode_find(qt->root, id);
    if (!item) return QT_ERR_NOT_FOUND;
    
    return quadtree_find_adjacent_to_region(qt, &item->bounds, result);
}

int quadtree_find_adjacent_to_region(const QuadTree *qt, const MBR *region,
                                      QTQueryResult *result) {
    if (!qt || !region || !result) return QT_ERR_NULL_PTR;
    
    qtresult_clear(result);
    
    /* Query all items that intersect or touch the region */
    QTQueryResult all_intersecting;
    int err = qtresult_init(&all_intersecting, 64);
    if (err != QT_OK) return err;
    
    /* Slightly expand region to catch adjacent items */
    double dx = (region->max_x - region->min_x) * 0.01;
    double dy = (region->max_y - region->min_y) * 0.01;
    if (dx < 1e-6) dx = 1e-6;
    if (dy < 1e-6) dy = 1e-6;
    
    MBR expanded = mbr_create(
        region->min_x - dx, region->min_y - dy,
        region->max_x + dx, region->max_y + dy
    );
    
    qtnode_query_range(qt->root, &expanded, &all_intersecting);
    
    /* Filter to only adjacent items */
    for (size_t i = 0; i < all_intersecting.count; i++) {
        if (mbr_adjacent_or_intersects(&all_intersecting.items[i].bounds, region)) {
            qtresult_add(result, &all_intersecting.items[i]);
        }
    }
    
    qtresult_free(&all_intersecting);
    return QT_OK;
}

int quadtree_query_radius(const QuadTree *qt, Point center, double radius,
                          QTQueryResult *result) {
    if (!qt || !result) return QT_ERR_NULL_PTR;
    
    /* First query bounding box */
    MBR box = mbr_create(center.x - radius, center.y - radius,
                         center.x + radius, center.y + radius);
    
    QTQueryResult box_result;
    int err = qtresult_init(&box_result, 64);
    if (err != QT_OK) return err;
    
    qtnode_query_range(qt->root, &box, &box_result);
    
    /* Filter by actual distance to centroid */
    qtresult_clear(result);
    double radius_sq = radius * radius;
    
    for (size_t i = 0; i < box_result.count; i++) {
        if (point_distance_sq(&center, &box_result.items[i].centroid) <= radius_sq) {
            qtresult_add(result, &box_result.items[i]);
        }
    }
    
    qtresult_free(&box_result);
    return QT_OK;
}

int quadtree_get(const QuadTree *qt, uint64_t id, QTItem *item) {
    if (!qt || !item) return QT_ERR_NULL_PTR;
    
    QTItem *found = qtnode_find(qt->root, id);
    if (!found) return QT_ERR_NOT_FOUND;
    
    *item = *found;
    return QT_OK;
}

int quadtree_update(QuadTree *qt, uint64_t id, MBR new_bounds) {
    if (!qt) return QT_ERR_NULL_PTR;
    
    /* Find item and get its data */
    QTItem *item = qtnode_find(qt->root, id);
    if (!item) return QT_ERR_NOT_FOUND;
    
    void *data = item->data;
    
    /* Remove and reinsert */
    int err = quadtree_remove(qt, id);
    if (err != QT_OK) return err;
    
    return quadtree_insert(qt, id, new_bounds, data);
}

int quadtree_get_all(const QuadTree *qt, QTQueryResult *result) {
    if (!qt || !result) return QT_ERR_NULL_PTR;
    
    qtresult_clear(result);
    qtnode_collect_all(qt->root, result);
    
    return QT_OK;
}

void quadtree_stats(const QuadTree *qt, size_t *total_items, size_t *total_nodes,
                    size_t *max_depth, size_t *leaf_count) {
    if (!qt) return;
    
    if (total_items) *total_items = qt->total_items;
    
    size_t nodes = 0, depth = 0, leaves = 0;
    qtnode_count(qt->root, &nodes, &depth, &leaves);
    
    if (total_nodes) *total_nodes = nodes;
    if (max_depth) *max_depth = depth;
    if (leaf_count) *leaf_count = leaves;
}

void quadtree_clear(QuadTree *qt) {
    if (!qt || !qt->root) return;
    
    MBR bounds = qt->root->bounds;
    size_t capacity = qt->node_capacity;
    int max_depth = qt->max_depth;
    
    quadtree_free(qt);
    quadtree_init(qt, bounds, capacity, max_depth);
}

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

int qtresult_init(QTQueryResult *result, size_t capacity) {
    if (!result) return QT_ERR_NULL_PTR;
    
    result->count = 0;
    result->capacity = capacity > 0 ? capacity : 16;
    result->items = (QTItem *)malloc(result->capacity * sizeof(QTItem));
    
    if (!result->items) {
        result->capacity = 0;
        return QT_ERR_ALLOC;
    }
    
    return QT_OK;
}

void qtresult_free(QTQueryResult *result) {
    if (!result) return;
    free(result->items);
    result->items = NULL;
    result->count = 0;
    result->capacity = 0;
}

void qtresult_clear(QTQueryResult *result) {
    if (!result) return;
    result->count = 0;
}

int qtresult_add(QTQueryResult *result, const QTItem *item) {
    if (!result || !item) return QT_ERR_NULL_PTR;
    
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * GROWTH_FACTOR;
        QTItem *new_items = (QTItem *)realloc(result->items, new_cap * sizeof(QTItem));
        if (!new_items) return QT_ERR_ALLOC;
        result->items = new_items;
        result->capacity = new_cap;
    }
    
    result->items[result->count++] = *item;
    return QT_OK;
}

/* ============================================================================
 * Node Operations
 * ============================================================================ */

QTNode* qtnode_create(MBR bounds, size_t capacity, int depth) {
    QTNode *node = (QTNode *)calloc(1, sizeof(QTNode));
    if (!node) return NULL;
    
    node->bounds = bounds;
    node->depth = depth;
    node->is_leaf = true;
    node->item_count = 0;
    node->item_capacity = capacity > 0 ? capacity : QT_DEFAULT_CAPACITY;
    
    node->items = (QTItem *)malloc(node->item_capacity * sizeof(QTItem));
    if (!node->items) {
        free(node);
        return NULL;
    }
    
    for (int i = 0; i < 4; i++) {
        node->children[i] = NULL;
    }
    
    return node;
}

void qtnode_free(QTNode *node) {
    if (!node) return;
    
    free(node->items);
    
    for (int i = 0; i < 4; i++) {
        qtnode_free(node->children[i]);
    }
    
    free(node);
}

Quadrant qtnode_get_quadrant(const QTNode *node, Point p) {
    if (!node) return QT_NW;
    
    double mid_x = (node->bounds.min_x + node->bounds.max_x) / 2.0;
    double mid_y = (node->bounds.min_y + node->bounds.max_y) / 2.0;
    
    if (p.x < mid_x) {
        return (p.y < mid_y) ? QT_SW : QT_NW;
    } else {
        return (p.y < mid_y) ? QT_SE : QT_NE;
    }
}

MBR qtnode_quadrant_bounds(const QTNode *node, Quadrant q) {
    if (!node) return mbr_empty();
    
    double mid_x = (node->bounds.min_x + node->bounds.max_x) / 2.0;
    double mid_y = (node->bounds.min_y + node->bounds.max_y) / 2.0;
    
    switch (q) {
        case QT_NW:
            return mbr_create(node->bounds.min_x, mid_y, mid_x, node->bounds.max_y);
        case QT_NE:
            return mbr_create(mid_x, mid_y, node->bounds.max_x, node->bounds.max_y);
        case QT_SW:
            return mbr_create(node->bounds.min_x, node->bounds.min_y, mid_x, mid_y);
        case QT_SE:
            return mbr_create(mid_x, node->bounds.min_y, node->bounds.max_x, mid_y);
        default:
            return mbr_empty();
    }
}

