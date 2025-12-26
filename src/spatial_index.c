/**
 * @file spatial_index.c
 * @brief High-level spatial index implementation
 */

#include "spatial_index.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define GROWTH_FACTOR 2

/**
 * @brief Find page containing an object by ID
 */
static Page* find_page_for_object(SpatialIndex *idx, uint64_t object_id) {
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        if (page_find_object(page, object_id)) {
            return page;
        }
    }
    return NULL;
}

/**
 * @brief Find or create a page for inserting an object
 */
static Page* get_page_for_insert(SpatialIndex *idx, const SpatialObject *obj) {
    /* Use KD-tree to find nearest page */
    Point nearest;
    uint64_t page_id;
    void *data;
    
    if (idx->disk.allocation_tree.size > 0) {
        int err = kdtree_nearest(&idx->disk.allocation_tree, obj->centroid,
                                  &nearest, &page_id, &data);
        if (err == KD_OK && data) {
            Page *page = (Page *)data;
            if (!page_is_full(page)) {
                return page;
            }
        }
    }
    
    /* Allocate new page */
    return disk_manager_alloc_page(&idx->disk, obj->centroid);
}

/**
 * @brief Create a new block
 */
static SpatialBlock* create_block(SpatialIndex *idx, MBR bounds) {
    /* Grow blocks array if needed */
    if (idx->block_count >= idx->block_capacity) {
        size_t new_cap = idx->block_capacity * GROWTH_FACTOR;
        SpatialBlock *new_blocks = (SpatialBlock *)realloc(idx->blocks,
                                                           new_cap * sizeof(SpatialBlock));
        if (!new_blocks) return NULL;
        idx->blocks = new_blocks;
        idx->block_capacity = new_cap;
    }
    
    SpatialBlock *block = &idx->blocks[idx->block_count];
    block->block_id = idx->next_block_id++;
    block->bounds = bounds;
    block->centroid = mbr_centroid(&bounds);
    block->track = disk_manager_create_track(&idx->disk);
    block->object_count = 0;
    
    idx->block_count++;
    
    return block;
}

/**
 * @brief Build quadtree from pages
 */
static int build_page_quadtree(SpatialIndex *idx) {
    if (!idx->config.build_quadtree) return SI_OK;
    
    /* Free existing quadtree */
    if (idx->page_tree) {
        quadtree_destroy(idx->page_tree);
        idx->page_tree = NULL;
    }
    
    if (idx->disk.pool.page_count == 0) return SI_OK;
    
    /* Create quadtree with overall bounds */
    idx->page_tree = quadtree_create(idx->bounds, 8, QT_MAX_DEPTH);
    if (!idx->page_tree) return SI_ERR_ALLOC;
    
    /* Insert all pages */
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        if (page->header.object_count > 0) {
            quadtree_insert_with_centroid(idx->page_tree,
                                          page->header.page_id,
                                          page->header.extent,
                                          page->header.centroid,
                                          page);
        }
    }
    
    return SI_OK;
}

/* ============================================================================
 * Spatial Index Operations
 * ============================================================================ */

SpatialIndexConfig spatial_index_default_config(void) {
    SpatialIndexConfig config = {
        .block_size = SI_DEFAULT_BLOCK_SIZE,
        .page_capacity = SI_DEFAULT_PAGE_CAPACITY,
        .cache_size = DM_DEFAULT_CACHE_SIZE,
        .build_quadtree = true,
        .persist = false,
        .data_path = NULL
    };
    return config;
}

SpatialIndex* spatial_index_create(const SpatialIndexConfig *config) {
    SpatialIndex *idx = (SpatialIndex *)calloc(1, sizeof(SpatialIndex));
    if (!idx) return NULL;
    
    if (spatial_index_init(idx, config) != SI_OK) {
        free(idx);
        return NULL;
    }
    
    return idx;
}

int spatial_index_init(SpatialIndex *idx, const SpatialIndexConfig *config) {
    if (!idx) return SI_ERR_NULL_PTR;
    
    memset(idx, 0, sizeof(SpatialIndex));
    
    if (config) {
        idx->config = *config;
    } else {
        idx->config = spatial_index_default_config();
    }
    
    /* Initialize KD-tree for blocks */
    int err = kdtree_init(&idx->block_tree);
    if (err != KD_OK) return SI_ERR_ALLOC;
    
    /* Initialize disk manager */
    DiskManagerConfig dm_config = disk_manager_default_config();
    dm_config.cache_size = idx->config.cache_size;
    
    err = disk_manager_init(&idx->disk, &dm_config);
    if (err != DM_OK) {
        kdtree_free(&idx->block_tree);
        return SI_ERR_ALLOC;
    }
    
    /* Initialize blocks array */
    idx->block_capacity = 16;
    idx->blocks = (SpatialBlock *)calloc(idx->block_capacity, sizeof(SpatialBlock));
    if (!idx->blocks) {
        disk_manager_free(&idx->disk);
        kdtree_free(&idx->block_tree);
        return SI_ERR_ALLOC;
    }
    
    idx->next_object_id = 1;
    idx->next_block_id = 1;
    idx->bounds = mbr_empty();
    idx->is_built = false;
    idx->page_tree = NULL;
    
    return SI_OK;
}

void spatial_index_free(SpatialIndex *idx) {
    if (!idx) return;
    
    kdtree_free(&idx->block_tree);
    
    if (idx->page_tree) {
        quadtree_destroy(idx->page_tree);
    }
    
    disk_manager_free(&idx->disk);
    free(idx->blocks);
    free(idx->config.data_path);
    
    memset(idx, 0, sizeof(SpatialIndex));
}

void spatial_index_destroy(SpatialIndex *idx) {
    if (!idx) return;
    spatial_index_free(idx);
    free(idx);
}

int spatial_index_insert(SpatialIndex *idx, SpatialObject *obj) {
    if (!idx || !obj) return SI_ERR_NULL_PTR;
    
    /* Assign ID if not set */
    if (obj->id == 0) {
        obj->id = idx->next_object_id++;
    }
    
    /* Update derived properties */
    spatial_object_update_derived(obj);
    
    /* Find or create page for this object */
    Page *page = get_page_for_insert(idx, obj);
    if (!page) return SI_ERR_ALLOC;
    
    /* Add object to page */
    int err = page_add_object(page, obj);
    if (err != PAGE_OK) {
        /* Page full - try to allocate new page */
        page = disk_manager_alloc_page(&idx->disk, obj->centroid);
        if (!page) return SI_ERR_ALLOC;
        
        err = page_add_object(page, obj);
        if (err != PAGE_OK) return SI_ERR_FULL;
    }
    
    /* Update page derived properties */
    page_update_derived(page);
    
    /* Update allocation tree */
    disk_manager_rebuild_allocation_tree(&idx->disk);
    
    /* Expand overall bounds */
    mbr_expand_mbr(&idx->bounds, &obj->mbr);
    
    /* Invalidate built state */
    idx->is_built = false;
    
    return SI_OK;
}

int spatial_index_bulk_insert(SpatialIndex *idx, SpatialObject *objects, size_t count) {
    if (!idx || !objects) return SI_ERR_NULL_PTR;
    
    for (size_t i = 0; i < count; i++) {
        int err = spatial_index_insert(idx, &objects[i]);
        if (err != SI_OK) return err;
    }
    
    return SI_OK;
}

int spatial_index_remove(SpatialIndex *idx, uint64_t object_id) {
    if (!idx) return SI_ERR_NULL_PTR;
    
    Page *page = find_page_for_object(idx, object_id);
    if (!page) return SI_ERR_NOT_FOUND;
    
    int err = page_remove_object(page, object_id);
    if (err != PAGE_OK) return SI_ERR_NOT_FOUND;
    
    page_update_derived(page);
    disk_manager_rebuild_allocation_tree(&idx->disk);
    
    idx->is_built = false;
    
    return SI_OK;
}

int spatial_index_build(SpatialIndex *idx) {
    if (!idx) return SI_ERR_NULL_PTR;
    
    /* Collect all objects for partitioning */
    size_t total_objects = 0;
    page_pool_stats(&idx->disk.pool, NULL, NULL, &total_objects);
    
    if (total_objects == 0) {
        idx->is_built = true;
        return SI_OK;
    }
    
    /* Build KD-tree from object centroids for block partitioning */
    KDPointData *points = (KDPointData *)malloc(total_objects * sizeof(KDPointData));
    if (!points) return SI_ERR_ALLOC;
    
    size_t point_idx = 0;
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        for (size_t j = 0; j < page->header.object_count; j++) {
            points[point_idx].point = page->objects[j].centroid;
            points[point_idx].object_id = page->objects[j].id;
            points[point_idx].data = &page->objects[j];
            point_idx++;
        }
    }
    
    /* Build block tree */
    kdtree_free(&idx->block_tree);
    kdtree_init(&idx->block_tree);
    int err = kdtree_bulk_load(&idx->block_tree, points, point_idx);
    free(points);
    
    if (err != KD_OK) return SI_ERR_ALLOC;
    
    /* Partition into blocks */
    MBR *block_bounds = NULL;
    size_t block_count = 0;
    
    err = kdtree_partition(&idx->block_tree, idx->config.block_size,
                           &block_count, &block_bounds);
    if (err != KD_OK) return SI_ERR_ALLOC;
    
    /* Create blocks from partitions */
    free(idx->blocks);
    idx->blocks = NULL;
    idx->block_count = 0;
    idx->block_capacity = block_count > 0 ? block_count : 16;
    idx->blocks = (SpatialBlock *)calloc(idx->block_capacity, sizeof(SpatialBlock));
    
    if (!idx->blocks) {
        free(block_bounds);
        return SI_ERR_ALLOC;
    }
    
    for (size_t i = 0; i < block_count; i++) {
        create_block(idx, block_bounds[i]);
    }
    
    free(block_bounds);
    
    /* Build page quadtree */
    err = build_page_quadtree(idx);
    if (err != SI_OK) return err;
    
    idx->is_built = true;
    
    return SI_OK;
}

int spatial_index_query_range(SpatialIndex *idx, const MBR *range,
                               SpatialQueryResult *result) {
    if (!idx || !range || !result) return SI_ERR_NULL_PTR;
    
    spatial_result_clear(result);
    
    /* Get pages intersecting range */
    Page **pages = NULL;
    size_t page_count = 0;
    
    int err = page_pool_query_region(&idx->disk.pool, range, &pages, &page_count);
    if (err != PAGE_OK) return SI_ERR_IO;
    
    /* Collect objects from matching pages */
    for (size_t i = 0; i < page_count; i++) {
        Page *page = pages[i];
        for (size_t j = 0; j < page->header.object_count; j++) {
            SpatialObject *obj = &page->objects[j];
            if (mbr_intersects(&obj->mbr, range)) {
                spatial_result_add(result, obj);
            }
        }
    }
    
    free(pages);
    
    return SI_OK;
}

int spatial_index_query_point(SpatialIndex *idx, Point p,
                               SpatialQueryResult *result) {
    if (!idx || !result) return SI_ERR_NULL_PTR;
    
    /* Create tiny MBR around point */
    MBR range = mbr_create(p.x, p.y, p.x, p.y);
    
    return spatial_index_query_range(idx, &range, result);
}

int spatial_index_query_knn(SpatialIndex *idx, Point p, size_t k,
                             SpatialQueryResult *result) {
    if (!idx || !result) return SI_ERR_NULL_PTR;
    if (k == 0) return SI_OK;
    
    spatial_result_clear(result);
    
    /* Use KD-tree for k-NN query */
    KDQueryResult kd_result;
    int err = kdresult_init(&kd_result, k);
    if (err != KD_OK) return SI_ERR_ALLOC;
    
    err = kdtree_k_nearest(&idx->block_tree, p, k, &kd_result);
    if (err != KD_OK) {
        kdresult_free(&kd_result);
        return SI_ERR_NOT_FOUND;
    }
    
    /* Convert results */
    for (size_t i = 0; i < kd_result.count; i++) {
        if (kd_result.data[i]) {
            spatial_result_add(result, (SpatialObject *)kd_result.data[i]);
        }
    }
    
    kdresult_free(&kd_result);
    
    return SI_OK;
}

int spatial_index_find_adjacent_pages(SpatialIndex *idx, const MBR *region,
                                       AdjacentPagesResult *result) {
    if (!idx || !region || !result) return SI_ERR_NULL_PTR;
    
    /* Initialize result to empty */
    result->pages = NULL;
    result->track_ids = NULL;
    result->count = 0;
    
    if (!idx->page_tree) {
        /* Quadtree not built - build it now */
        int err = build_page_quadtree(idx);
        if (err != SI_OK) return err;
    }
    
    if (!idx->page_tree) return SI_ERR_NOT_BUILT;
    
    /* Query quadtree for adjacent pages */
    QTQueryResult qt_result;
    int err = qtresult_init(&qt_result, 64);
    if (err != QT_OK) return SI_ERR_ALLOC;
    
    err = quadtree_find_adjacent_to_region(idx->page_tree, region, &qt_result);
    if (err != QT_OK) {
        qtresult_free(&qt_result);
        return SI_ERR_NOT_FOUND;
    }
    
    if (qt_result.count == 0) {
        qtresult_free(&qt_result);
        return SI_OK;
    }
    
    /* Convert to result */
    result->count = qt_result.count;
    result->pages = (Page **)malloc(qt_result.count * sizeof(Page *));
    result->track_ids = (uint32_t *)malloc(qt_result.count * sizeof(uint32_t));
    
    if (!result->pages || !result->track_ids) {
        free(result->pages);
        free(result->track_ids);
        result->pages = NULL;
        result->track_ids = NULL;
        result->count = 0;
        qtresult_free(&qt_result);
        return SI_ERR_ALLOC;
    }
    
    for (size_t i = 0; i < qt_result.count; i++) {
        Page *page = (Page *)qt_result.items[i].data;
        result->pages[i] = page;
        result->track_ids[i] = page ? page->header.track_id : 0;
    }
    
    qtresult_free(&qt_result);
    
    return SI_OK;
}

SpatialObject* spatial_index_get(SpatialIndex *idx, uint64_t object_id) {
    if (!idx) return NULL;
    
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        SpatialObject *obj = page_find_object(idx->disk.pool.pages[i], object_id);
        if (obj) return obj;
    }
    
    return NULL;
}

int spatial_index_update(SpatialIndex *idx, uint64_t object_id,
                          const SpatialObject *new_obj) {
    if (!idx || !new_obj) return SI_ERR_NULL_PTR;
    
    /* Remove old object */
    int err = spatial_index_remove(idx, object_id);
    if (err != SI_OK) return err;
    
    /* Insert new object with same ID */
    SpatialObject obj_copy;
    spatial_object_copy(&obj_copy, new_obj);
    obj_copy.id = object_id;
    
    err = spatial_index_insert(idx, &obj_copy);
    spatial_object_free(&obj_copy);
    
    return err;
}

/* ============================================================================
 * Block Operations
 * ============================================================================ */

SpatialBlock* spatial_index_get_block(SpatialIndex *idx, Point p) {
    if (!idx) return NULL;
    
    for (size_t i = 0; i < idx->block_count; i++) {
        if (mbr_contains_point(&idx->blocks[i].bounds, &p)) {
            return &idx->blocks[i];
        }
    }
    
    return NULL;
}

int spatial_index_query_blocks(SpatialIndex *idx, const MBR *region,
                                SpatialBlock ***blocks, size_t *count) {
    if (!idx || !region || !blocks || !count) return SI_ERR_NULL_PTR;
    
    *blocks = NULL;
    *count = 0;
    
    /* Count matching blocks */
    size_t matching = 0;
    for (size_t i = 0; i < idx->block_count; i++) {
        if (mbr_intersects(&idx->blocks[i].bounds, region)) {
            matching++;
        }
    }
    
    if (matching == 0) return SI_OK;
    
    *blocks = (SpatialBlock **)malloc(matching * sizeof(SpatialBlock *));
    if (!*blocks) return SI_ERR_ALLOC;
    
    size_t idx_pos = 0;
    for (size_t i = 0; i < idx->block_count; i++) {
        if (mbr_intersects(&idx->blocks[i].bounds, region)) {
            (*blocks)[idx_pos++] = &idx->blocks[i];
        }
    }
    
    *count = idx_pos;
    return SI_OK;
}

int spatial_index_get_all_blocks(SpatialIndex *idx,
                                  SpatialBlock ***blocks, size_t *count) {
    if (!idx || !blocks || !count) return SI_ERR_NULL_PTR;
    
    *blocks = (SpatialBlock **)malloc(idx->block_count * sizeof(SpatialBlock *));
    if (!*blocks && idx->block_count > 0) return SI_ERR_ALLOC;
    
    for (size_t i = 0; i < idx->block_count; i++) {
        (*blocks)[i] = &idx->blocks[i];
    }
    
    *count = idx->block_count;
    return SI_OK;
}

/* ============================================================================
 * Statistics and Utilities
 * ============================================================================ */

void spatial_index_stats(const SpatialIndex *idx, SpatialIndexStats *stats) {
    if (!idx || !stats) return;
    
    memset(stats, 0, sizeof(SpatialIndexStats));
    
    page_pool_stats(&idx->disk.pool, &stats->total_pages, 
                    &stats->total_tracks, &stats->total_objects);
    
    stats->total_blocks = idx->block_count;
    stats->kdtree_depth = kdtree_depth(&idx->block_tree);
    
    if (idx->page_tree) {
        size_t qt_depth = 0;
        quadtree_stats(idx->page_tree, NULL, NULL, &qt_depth, NULL);
        stats->quadtree_depth = qt_depth;
    }
    
    if (stats->total_pages > 0) {
        stats->avg_objects_per_page = (double)stats->total_objects / stats->total_pages;
        
        double total_util = 0;
        for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
            total_util += page_utilization(idx->disk.pool.pages[i]);
        }
        stats->page_utilization = total_util / stats->total_pages;
    }
    
    stats->bounds = idx->bounds;
}

int spatial_index_optimize(SpatialIndex *idx) {
    if (!idx) return SI_ERR_NULL_PTR;
    
    /* Rebuild index */
    return spatial_index_build(idx);
}

int spatial_index_save(SpatialIndex *idx, const char *path) {
    if (!idx || !path) return SI_ERR_NULL_PTR;
    
    /* Create or open data file */
    int err = disk_manager_create(&idx->disk, path);
    if (err != DM_OK) return SI_ERR_IO;
    
    /* Sync all data */
    err = disk_manager_sync(&idx->disk);
    if (err != DM_OK) return SI_ERR_IO;
    
    return SI_OK;
}

int spatial_index_load(SpatialIndex *idx, const char *path) {
    if (!idx || !path) return SI_ERR_NULL_PTR;
    
    int err = disk_manager_open(&idx->disk, path);
    if (err != DM_OK) return SI_ERR_IO;
    
    /* Rebuild index structures */
    idx->bounds = idx->disk.header.bounds;
    
    err = spatial_index_build(idx);
    if (err != SI_OK) return err;
    
    return SI_OK;
}

void spatial_index_clear(SpatialIndex *idx) {
    if (!idx) return;
    
    kdtree_free(&idx->block_tree);
    kdtree_init(&idx->block_tree);
    
    if (idx->page_tree) {
        quadtree_clear(idx->page_tree);
    }
    
    /* Clear pages */
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        page_free(idx->disk.pool.pages[i]);
    }
    idx->disk.pool.page_count = 0;
    
    /* Clear tracks */
    for (size_t i = 0; i < idx->disk.pool.track_count; i++) {
        track_free(idx->disk.pool.tracks[i]);
    }
    idx->disk.pool.track_count = 0;
    
    /* Clear blocks */
    idx->block_count = 0;
    
    idx->bounds = mbr_empty();
    idx->is_built = false;
}

void spatial_index_print_stats(const SpatialIndex *idx, FILE *out) {
    if (!idx || !out) return;
    
    SpatialIndexStats stats;
    spatial_index_stats(idx, &stats);
    
    fprintf(out, "=== Spatial Index Statistics ===\n");
    fprintf(out, "Objects: %zu\n", stats.total_objects);
    fprintf(out, "Blocks: %zu\n", stats.total_blocks);
    fprintf(out, "Pages: %zu\n", stats.total_pages);
    fprintf(out, "Tracks: %zu\n", stats.total_tracks);
    fprintf(out, "\n");
    fprintf(out, "KD-tree depth: %zu\n", stats.kdtree_depth);
    fprintf(out, "Quadtree depth: %zu\n", stats.quadtree_depth);
    fprintf(out, "\n");
    fprintf(out, "Avg objects/page: %.2f\n", stats.avg_objects_per_page);
    fprintf(out, "Page utilization: %.1f%%\n", stats.page_utilization * 100);
    fprintf(out, "\n");
    fprintf(out, "Bounds: (%.2f, %.2f) - (%.2f, %.2f)\n",
            stats.bounds.min_x, stats.bounds.min_y,
            stats.bounds.max_x, stats.bounds.max_y);
}

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

int spatial_result_init(SpatialQueryResult *result, size_t capacity) {
    if (!result) return SI_ERR_NULL_PTR;
    
    memset(result, 0, sizeof(SpatialQueryResult));
    
    result->capacity = capacity > 0 ? capacity : 64;
    result->objects = (SpatialObject **)malloc(result->capacity * sizeof(SpatialObject *));
    result->page_ids = (uint32_t *)malloc(result->capacity * sizeof(uint32_t));
    
    if (!result->objects || !result->page_ids) {
        free(result->objects);
        free(result->page_ids);
        return SI_ERR_ALLOC;
    }
    
    return SI_OK;
}

void spatial_result_free(SpatialQueryResult *result) {
    if (!result) return;
    free(result->objects);
    free(result->page_ids);
    memset(result, 0, sizeof(SpatialQueryResult));
}

void spatial_result_clear(SpatialQueryResult *result) {
    if (!result) return;
    result->count = 0;
    result->pages_accessed = 0;
}

int spatial_result_add(SpatialQueryResult *result, SpatialObject *obj) {
    if (!result || !obj) return SI_ERR_NULL_PTR;
    
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * GROWTH_FACTOR;
        SpatialObject **new_objs = (SpatialObject **)realloc(result->objects,
                                                              new_cap * sizeof(SpatialObject *));
        uint32_t *new_ids = (uint32_t *)realloc(result->page_ids,
                                                 new_cap * sizeof(uint32_t));
        
        if (!new_objs || !new_ids) return SI_ERR_ALLOC;
        
        result->objects = new_objs;
        result->page_ids = new_ids;
        result->capacity = new_cap;
    }
    
    result->objects[result->count++] = obj;
    
    return SI_OK;
}

/* ============================================================================
 * Adjacent Pages Result Operations
 * ============================================================================ */

int adjacent_result_init(AdjacentPagesResult *result, size_t capacity) {
    if (!result) return SI_ERR_NULL_PTR;
    
    (void)capacity;  /* Not used - allocation happens in find_adjacent_pages */
    
    memset(result, 0, sizeof(AdjacentPagesResult));
    
    return SI_OK;
}

void adjacent_result_free(AdjacentPagesResult *result) {
    if (!result) return;
    free(result->pages);
    free(result->track_ids);
    memset(result, 0, sizeof(AdjacentPagesResult));
}

