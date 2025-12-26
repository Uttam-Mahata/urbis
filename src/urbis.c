/**
 * @file urbis.c
 * @brief Main public API implementation
 */

#include "urbis.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

UrbisConfig urbis_default_config(void) {
    UrbisConfig config = {
        .block_size = SI_DEFAULT_BLOCK_SIZE,
        .page_capacity = SI_DEFAULT_PAGE_CAPACITY,
        .cache_size = DM_DEFAULT_CACHE_SIZE,
        .enable_quadtree = true,
        .persist = false,
        .data_path = NULL
    };
    return config;
}

UrbisIndex* urbis_create(const UrbisConfig *config) {
    SpatialIndexConfig si_config = spatial_index_default_config();
    
    if (config) {
        si_config.block_size = config->block_size;
        si_config.page_capacity = config->page_capacity;
        si_config.cache_size = config->cache_size;
        si_config.build_quadtree = config->enable_quadtree;
        si_config.persist = config->persist;
        if (config->data_path) {
            si_config.data_path = strdup(config->data_path);
        }
    }
    
    return spatial_index_create(&si_config);
}

void urbis_destroy(UrbisIndex *idx) {
    spatial_index_destroy(idx);
}

const char* urbis_version(void) {
    return URBIS_VERSION_STRING;
}

/* ============================================================================
 * Data Loading
 * ============================================================================ */

int urbis_load_geojson(UrbisIndex *idx, const char *path) {
    if (!idx || !path) return URBIS_ERR_NULL;
    
    FeatureCollection fc;
    int err = geojson_parse_file(path, &fc);
    if (err != PARSE_OK) return URBIS_ERR_PARSE;
    
    /* Insert all features */
    for (size_t i = 0; i < fc.count; i++) {
        err = spatial_index_insert(idx, &fc.features[i].object);
        if (err != SI_OK) {
            feature_collection_free(&fc);
            return URBIS_ERR_ALLOC;
        }
    }
    
    feature_collection_free(&fc);
    return URBIS_OK;
}

int urbis_load_geojson_string(UrbisIndex *idx, const char *json) {
    if (!idx || !json) return URBIS_ERR_NULL;
    
    FeatureCollection fc;
    int err = geojson_parse_string(json, &fc);
    if (err != PARSE_OK) return URBIS_ERR_PARSE;
    
    for (size_t i = 0; i < fc.count; i++) {
        err = spatial_index_insert(idx, &fc.features[i].object);
        if (err != SI_OK) {
            feature_collection_free(&fc);
            return URBIS_ERR_ALLOC;
        }
    }
    
    feature_collection_free(&fc);
    return URBIS_OK;
}

int urbis_load_wkt(UrbisIndex *idx, const char *wkt) {
    if (!idx || !wkt) return URBIS_ERR_NULL;
    
    SpatialObject obj;
    int err = wkt_parse(wkt, &obj);
    if (err != PARSE_OK) return URBIS_ERR_PARSE;
    
    err = spatial_index_insert(idx, &obj);
    spatial_object_free(&obj);
    
    return (err == SI_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

/* ============================================================================
 * Object Operations
 * ============================================================================ */

uint64_t urbis_insert(UrbisIndex *idx, const SpatialObject *obj) {
    if (!idx || !obj) return 0;
    
    SpatialObject copy;
    if (spatial_object_copy(&copy, obj) != GEOM_OK) return 0;
    
    int err = spatial_index_insert(idx, &copy);
    if (err != SI_OK) {
        spatial_object_free(&copy);
        return 0;
    }
    
    return copy.id;
}

uint64_t urbis_insert_point(UrbisIndex *idx, double x, double y) {
    if (!idx) return 0;
    
    SpatialObject obj;
    if (spatial_object_init_point(&obj, 0, point_create(x, y)) != GEOM_OK) {
        return 0;
    }
    
    int err = spatial_index_insert(idx, &obj);
    uint64_t id = obj.id;
    
    if (err != SI_OK) {
        spatial_object_free(&obj);
        return 0;
    }
    
    return id;
}

uint64_t urbis_insert_linestring(UrbisIndex *idx, const Point *points, size_t count) {
    if (!idx || !points || count == 0) return 0;
    
    SpatialObject obj;
    if (spatial_object_init_linestring(&obj, 0, count) != GEOM_OK) {
        return 0;
    }
    
    for (size_t i = 0; i < count; i++) {
        linestring_add_point(&obj.geom.line, points[i]);
    }
    
    spatial_object_update_derived(&obj);
    
    int err = spatial_index_insert(idx, &obj);
    uint64_t id = obj.id;
    
    /* Always free the local object - spatial_index_insert makes a copy */
    spatial_object_free(&obj);
    
    if (err != SI_OK) {
        return 0;
    }
    
    return id;
}

uint64_t urbis_insert_polygon(UrbisIndex *idx, const Point *exterior, size_t count) {
    if (!idx || !exterior || count < 3) return 0;
    
    SpatialObject obj;
    if (spatial_object_init_polygon(&obj, 0, count) != GEOM_OK) {
        return 0;
    }
    
    for (size_t i = 0; i < count; i++) {
        polygon_add_exterior_point(&obj.geom.polygon, exterior[i]);
    }
    
    spatial_object_update_derived(&obj);
    
    int err = spatial_index_insert(idx, &obj);
    uint64_t id = obj.id;
    
    /* Always free the local object - spatial_index_insert makes a copy */
    spatial_object_free(&obj);
    
    if (err != SI_OK) {
        return 0;
    }
    
    return id;
}

int urbis_remove(UrbisIndex *idx, uint64_t object_id) {
    if (!idx) return URBIS_ERR_NULL;
    
    int err = spatial_index_remove(idx, object_id);
    return (err == SI_OK) ? URBIS_OK : URBIS_ERR_NOT_FOUND;
}

SpatialObject* urbis_get(UrbisIndex *idx, uint64_t object_id) {
    if (!idx) return NULL;
    return spatial_index_get(idx, object_id);
}

/* ============================================================================
 * Index Building
 * ============================================================================ */

int urbis_build(UrbisIndex *idx) {
    if (!idx) return URBIS_ERR_NULL;
    
    int err = spatial_index_build(idx);
    return (err == SI_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

int urbis_optimize(UrbisIndex *idx) {
    if (!idx) return URBIS_ERR_NULL;
    
    int err = spatial_index_optimize(idx);
    return (err == SI_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

/* ============================================================================
 * Spatial Queries
 * ============================================================================ */

UrbisObjectList* urbis_query_range(UrbisIndex *idx, const MBR *range) {
    if (!idx || !range) return NULL;
    
    UrbisObjectList *list = (UrbisObjectList *)calloc(1, sizeof(UrbisObjectList));
    if (!list) return NULL;
    
    SpatialQueryResult result;
    if (spatial_result_init(&result, 64) != SI_OK) {
        free(list);
        return NULL;
    }
    
    int err = spatial_index_query_range(idx, range, &result);
    if (err != SI_OK) {
        spatial_result_free(&result);
        free(list);
        return NULL;
    }
    
    list->objects = result.objects;
    list->count = result.count;
    
    /* Don't free result.objects since we're transferring ownership */
    free(result.page_ids);
    
    return list;
}

UrbisObjectList* urbis_query_point(UrbisIndex *idx, double x, double y) {
    if (!idx) return NULL;
    
    UrbisObjectList *list = (UrbisObjectList *)calloc(1, sizeof(UrbisObjectList));
    if (!list) return NULL;
    
    SpatialQueryResult result;
    if (spatial_result_init(&result, 16) != SI_OK) {
        free(list);
        return NULL;
    }
    
    Point p = point_create(x, y);
    int err = spatial_index_query_point(idx, p, &result);
    if (err != SI_OK) {
        spatial_result_free(&result);
        free(list);
        return NULL;
    }
    
    list->objects = result.objects;
    list->count = result.count;
    
    free(result.page_ids);
    
    return list;
}

UrbisObjectList* urbis_query_knn(UrbisIndex *idx, double x, double y, size_t k) {
    if (!idx || k == 0) return NULL;
    
    UrbisObjectList *list = (UrbisObjectList *)calloc(1, sizeof(UrbisObjectList));
    if (!list) return NULL;
    
    SpatialQueryResult result;
    if (spatial_result_init(&result, k) != SI_OK) {
        free(list);
        return NULL;
    }
    
    Point p = point_create(x, y);
    int err = spatial_index_query_knn(idx, p, k, &result);
    if (err != SI_OK) {
        spatial_result_free(&result);
        free(list);
        return NULL;
    }
    
    list->objects = result.objects;
    list->count = result.count;
    
    free(result.page_ids);
    
    return list;
}

UrbisPageList* urbis_find_adjacent_pages(UrbisIndex *idx, const MBR *region) {
    if (!idx || !region) return NULL;
    
    UrbisPageList *list = (UrbisPageList *)calloc(1, sizeof(UrbisPageList));
    if (!list) return NULL;
    
    AdjacentPagesResult result;
    if (adjacent_result_init(&result, 64) != SI_OK) {
        free(list);
        return NULL;
    }
    
    int err = spatial_index_find_adjacent_pages(idx, region, &result);
    if (err != SI_OK) {
        adjacent_result_free(&result);
        free(list);
        return NULL;
    }
    
    list->count = result.count;
    list->page_ids = (uint32_t *)malloc(result.count * sizeof(uint32_t));
    list->track_ids = (uint32_t *)malloc(result.count * sizeof(uint32_t));
    
    if (!list->page_ids || !list->track_ids) {
        free(list->page_ids);
        free(list->track_ids);
        adjacent_result_free(&result);
        free(list);
        return NULL;
    }
    
    /* Extract page IDs and copy track IDs */
    for (size_t i = 0; i < result.count; i++) {
        list->page_ids[i] = result.pages[i]->header.page_id;
        list->track_ids[i] = result.track_ids[i];
    }
    
    /* Estimate seeks (count track transitions) */
    list->estimated_seeks = 0;
    uint32_t last_track = 0;
    for (size_t i = 0; i < result.count; i++) {
        if (list->track_ids[i] != last_track && last_track != 0) {
            list->estimated_seeks++;
        }
        last_track = list->track_ids[i];
    }
    
    /* Free the result - we've copied everything we need */
    adjacent_result_free(&result);
    
    return list;
}

UrbisObjectList* urbis_query_adjacent(UrbisIndex *idx, const MBR *region) {
    if (!idx || !region) return NULL;
    
    /* First get adjacent pages */
    AdjacentPagesResult pages;
    memset(&pages, 0, sizeof(pages));
    if (adjacent_result_init(&pages, 64) != SI_OK) return NULL;
    
    int err = spatial_index_find_adjacent_pages(idx, region, &pages);
    if (err != SI_OK) {
        adjacent_result_free(&pages);
        return NULL;
    }
    
    /* Collect objects from adjacent pages */
    UrbisObjectList *list = (UrbisObjectList *)calloc(1, sizeof(UrbisObjectList));
    if (!list) {
        adjacent_result_free(&pages);
        return NULL;
    }
    
    /* Count total objects */
    size_t total = 0;
    for (size_t i = 0; i < pages.count; i++) {
        if (pages.pages[i]) {
            total += pages.pages[i]->header.object_count;
        }
    }
    
    if (total == 0) {
        list->objects = NULL;
        list->count = 0;
        adjacent_result_free(&pages);
        return list;
    }
    
    list->objects = (SpatialObject **)malloc(total * sizeof(SpatialObject *));
    if (!list->objects) {
        adjacent_result_free(&pages);
        free(list);
        return NULL;
    }
    
    /* Collect objects that intersect the region */
    size_t count = 0;
    for (size_t i = 0; i < pages.count; i++) {
        Page *page = pages.pages[i];
        if (!page) continue;
        for (size_t j = 0; j < page->header.object_count; j++) {
            SpatialObject *obj = &page->objects[j];
            if (mbr_intersects(&obj->mbr, region)) {
                list->objects[count++] = obj;
            }
        }
    }
    
    list->count = count;
    
    adjacent_result_free(&pages);
    
    return list;
}

/* ============================================================================
 * Persistence
 * ============================================================================ */

int urbis_save(UrbisIndex *idx, const char *path) {
    if (!idx || !path) return URBIS_ERR_NULL;
    
    int err = spatial_index_save(idx, path);
    return (err == SI_OK) ? URBIS_OK : URBIS_ERR_IO;
}

UrbisIndex* urbis_load(const char *path) {
    if (!path) return NULL;
    
    UrbisIndex *idx = urbis_create(NULL);
    if (!idx) return NULL;
    
    int err = spatial_index_load(idx, path);
    if (err != SI_OK) {
        urbis_destroy(idx);
        return NULL;
    }
    
    return idx;
}

int urbis_sync(UrbisIndex *idx) {
    if (!idx) return URBIS_ERR_NULL;
    
    int err = disk_manager_sync(&idx->disk);
    return (err == DM_OK) ? URBIS_OK : URBIS_ERR_IO;
}

/* ============================================================================
 * Statistics and Utilities
 * ============================================================================ */

void urbis_get_stats(const UrbisIndex *idx, UrbisStats *stats) {
    if (!idx || !stats) return;
    
    SpatialIndexStats si_stats;
    spatial_index_stats(idx, &si_stats);
    
    stats->total_objects = si_stats.total_objects;
    stats->total_blocks = si_stats.total_blocks;
    stats->total_pages = si_stats.total_pages;
    stats->total_tracks = si_stats.total_tracks;
    stats->avg_objects_per_page = si_stats.avg_objects_per_page;
    stats->page_utilization = si_stats.page_utilization;
    stats->kdtree_depth = si_stats.kdtree_depth;
    stats->quadtree_depth = si_stats.quadtree_depth;
    stats->bounds = si_stats.bounds;
}

size_t urbis_count(const UrbisIndex *idx) {
    if (!idx) return 0;
    
    UrbisStats stats;
    urbis_get_stats(idx, &stats);
    return stats.total_objects;
}

MBR urbis_bounds(const UrbisIndex *idx) {
    if (!idx) return mbr_empty();
    return idx->bounds;
}

void urbis_print_stats(const UrbisIndex *idx, FILE *out) {
    if (!idx || !out) return;
    
    fprintf(out, "=== Urbis Spatial Index ===\n");
    fprintf(out, "Version: %s\n\n", urbis_version());
    
    spatial_index_print_stats(idx, out);
}

size_t urbis_estimate_seeks(const UrbisIndex *idx,
                            const MBR *regions, size_t count) {
    if (!idx || !regions || count == 0) return 0;
    
    size_t total_seeks = 0;
    
    for (size_t i = 0; i < count; i++) {
        UrbisPageList *pages = urbis_find_adjacent_pages((UrbisIndex *)idx, &regions[i]);
        if (pages) {
            total_seeks += pages->estimated_seeks;
            urbis_page_list_free(pages);
        }
    }
    
    return total_seeks;
}

/* ============================================================================
 * Result List Operations
 * ============================================================================ */

void urbis_object_list_free(UrbisObjectList *list) {
    if (!list) return;
    free(list->objects);
    free(list);
}

void urbis_page_list_free(UrbisPageList *list) {
    if (!list) return;
    free(list->page_ids);
    free(list->track_ids);
    free(list);
}

