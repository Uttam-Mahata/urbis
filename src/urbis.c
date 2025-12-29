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

/* ============================================================================
 * Advanced Spatial Operations
 * ============================================================================ */

Polygon* urbis_buffer(UrbisIndex *idx, uint64_t object_id, double distance, int segments) {
    if (!idx) return NULL;
    
    SpatialObject *obj = spatial_index_get(idx, object_id);
    if (!obj) return NULL;
    
    return spatial_buffer(obj, distance, segments > 0 ? segments : BUFFER_DEFAULT_SEGMENTS);
}

Polygon* urbis_buffer_point(double x, double y, double distance, int segments) {
    Point center = point_create(x, y);
    return buffer_point(&center, distance, segments > 0 ? segments : BUFFER_DEFAULT_SEGMENTS);
}

bool urbis_intersects(UrbisIndex *idx, uint64_t id_a, uint64_t id_b) {
    if (!idx) return false;
    
    SpatialObject *obj_a = spatial_index_get(idx, id_a);
    SpatialObject *obj_b = spatial_index_get(idx, id_b);
    
    if (!obj_a || !obj_b) return false;
    
    return spatial_intersects(obj_a, obj_b);
}

bool urbis_contains(UrbisIndex *idx, uint64_t container_id, uint64_t contained_id) {
    if (!idx) return false;
    
    SpatialObject *container = spatial_index_get(idx, container_id);
    SpatialObject *contained = spatial_index_get(idx, contained_id);
    
    if (!container || !contained) return false;
    
    return spatial_contains(container, contained);
}

double urbis_distance(UrbisIndex *idx, uint64_t id_a, uint64_t id_b) {
    if (!idx) return -1;
    
    SpatialObject *obj_a = spatial_index_get(idx, id_a);
    SpatialObject *obj_b = spatial_index_get(idx, id_b);
    
    if (!obj_a || !obj_b) return -1;
    
    return spatial_distance(obj_a, obj_b);
}

UrbisSpatialJoinResult* urbis_spatial_join(UrbisIndex *idx_a, UrbisIndex *idx_b,
                                            int join_type, double distance) {
    if (!idx_a || !idx_b) return NULL;
    
    SpatialJoinResult *join = spatial_index_join(idx_a, idx_b, 
                                                  (SpatialJoinType)join_type, distance);
    if (!join) return NULL;
    
    UrbisSpatialJoinResult *result = malloc(sizeof(UrbisSpatialJoinResult));
    if (!result) {
        join_result_free(join);
        return NULL;
    }
    
    result->count = join->count;
    result->ids_a = malloc(join->count * sizeof(uint64_t));
    result->ids_b = malloc(join->count * sizeof(uint64_t));
    result->distances = malloc(join->count * sizeof(double));
    
    if (!result->ids_a || !result->ids_b || !result->distances) {
        free(result->ids_a);
        free(result->ids_b);
        free(result->distances);
        free(result);
        join_result_free(join);
        return NULL;
    }
    
    for (size_t i = 0; i < join->count; i++) {
        result->ids_a[i] = join->pairs[i].id_a;
        result->ids_b[i] = join->pairs[i].id_b;
        result->distances[i] = join->pairs[i].distance;
    }
    
    join_result_free(join);
    return result;
}

void urbis_spatial_join_free(UrbisSpatialJoinResult *result) {
    if (!result) return;
    free(result->ids_a);
    free(result->ids_b);
    free(result->distances);
    free(result);
}

UrbisGridResult* urbis_aggregate_grid(UrbisIndex *idx, const MBR *bounds,
                                       double cell_size, int agg_type) {
    if (!idx || cell_size <= 0) return NULL;
    
    GridAggregation *grid = spatial_index_aggregate_grid(idx, bounds, cell_size,
                                                          (AggregationType)agg_type);
    if (!grid) return NULL;
    
    UrbisGridResult *result = malloc(sizeof(UrbisGridResult));
    if (!result) {
        grid_aggregation_free(grid);
        return NULL;
    }
    
    size_t total = grid->rows * grid->cols;
    result->rows = grid->rows;
    result->cols = grid->cols;
    result->bounds = grid->bounds;
    result->cell_size = grid->cell_width;
    
    result->values = malloc(total * sizeof(double));
    result->counts = malloc(total * sizeof(size_t));
    
    if (!result->values || !result->counts) {
        free(result->values);
        free(result->counts);
        free(result);
        grid_aggregation_free(grid);
        return NULL;
    }
    
    for (size_t i = 0; i < total; i++) {
        result->values[i] = grid->cells[i].value;
        result->counts[i] = grid->cells[i].count;
    }
    
    grid_aggregation_free(grid);
    return result;
}

void urbis_grid_result_free(UrbisGridResult *result) {
    if (!result) return;
    free(result->values);
    free(result->counts);
    free(result);
}

VoronoiDiagram* urbis_voronoi(UrbisIndex *idx, const MBR *bounds) {
    if (!idx) return NULL;
    
    /* Collect all points from the index */
    size_t count = urbis_count(idx);
    if (count == 0) return NULL;
    
    Point *points = malloc(count * sizeof(Point));
    uint64_t *ids = malloc(count * sizeof(uint64_t));
    if (!points || !ids) {
        free(points);
        free(ids);
        return NULL;
    }
    
    size_t pt_count = 0;
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        for (size_t j = 0; j < page->header.object_count; j++) {
            SpatialObject *obj = &page->objects[j];
            if (obj->type == GEOM_POINT) {
                points[pt_count] = obj->geom.point;
                ids[pt_count] = obj->id;
                pt_count++;
            } else {
                /* Use centroid for non-point objects */
                points[pt_count] = obj->centroid;
                ids[pt_count] = obj->id;
                pt_count++;
            }
        }
    }
    
    VoronoiDiagram *diagram = voronoi_create(points, ids, pt_count, bounds);
    
    free(points);
    free(ids);
    
    return diagram;
}

DelaunayTriangulation* urbis_delaunay(UrbisIndex *idx) {
    if (!idx) return NULL;
    
    /* Collect all points from the index */
    size_t count = urbis_count(idx);
    if (count < 3) return NULL;
    
    Point *points = malloc(count * sizeof(Point));
    if (!points) return NULL;
    
    size_t pt_count = 0;
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        for (size_t j = 0; j < page->header.object_count; j++) {
            SpatialObject *obj = &page->objects[j];
            if (obj->type == GEOM_POINT) {
                points[pt_count++] = obj->geom.point;
            } else {
                points[pt_count++] = obj->centroid;
            }
        }
    }
    
    DelaunayTriangulation *tri = delaunay_create(points, pt_count);
    
    free(points);
    
    return tri;
}

Polygon* urbis_convex_hull(UrbisIndex *idx) {
    if (!idx) return NULL;
    
    /* Collect all points/centroids from the index */
    size_t count = urbis_count(idx);
    if (count < 3) return NULL;
    
    Point *points = malloc(count * sizeof(Point));
    if (!points) return NULL;
    
    size_t pt_count = 0;
    for (size_t i = 0; i < idx->disk.pool.page_count; i++) {
        Page *page = idx->disk.pool.pages[i];
        for (size_t j = 0; j < page->header.object_count; j++) {
            SpatialObject *obj = &page->objects[j];
            if (obj->type == GEOM_POINT) {
                points[pt_count++] = obj->geom.point;
            } else {
                points[pt_count++] = obj->centroid;
            }
        }
    }
    
    size_t hull_count = 0;
    Point *hull_points = convex_hull(points, pt_count, &hull_count);
    free(points);
    
    if (!hull_points || hull_count < 3) {
        free(hull_points);
        return NULL;
    }
    
    Polygon *hull = malloc(sizeof(Polygon));
    if (!hull) {
        free(hull_points);
        return NULL;
    }
    
    if (polygon_init(hull, hull_count) != GEOM_OK) {
        free(hull_points);
        free(hull);
        return NULL;
    }
    
    for (size_t i = 0; i < hull_count; i++) {
        polygon_add_exterior_point(hull, hull_points[i]);
    }
    
    free(hull_points);
    return hull;
}

/* ============================================================================
 * Real-Time Streaming Implementation
 * ============================================================================ */

UrbisStreamHandle* urbis_stream_create(UrbisIndex *idx) {
    return stream_create(idx);
}

void urbis_stream_destroy(UrbisStreamHandle *stream) {
    stream_destroy(stream);
}

int urbis_stream_start(UrbisStreamHandle *stream) {
    if (!stream) return URBIS_ERR_NULL;
    return stream_start(stream) == STREAM_OK ? URBIS_OK : URBIS_ERR_INVALID;
}

int urbis_stream_stop(UrbisStreamHandle *stream) {
    if (!stream) return URBIS_ERR_NULL;
    return stream_stop(stream) == STREAM_OK ? URBIS_OK : URBIS_ERR_INVALID;
}

int urbis_stream_update(UrbisStreamHandle *stream, uint64_t object_id,
                        double x, double y, uint64_t timestamp) {
    if (!stream) return URBIS_ERR_NULL;
    int err = stream_update_location(stream, object_id, x, y, timestamp);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

int urbis_stream_update_ex(UrbisStreamHandle *stream, uint64_t object_id,
                           double x, double y, uint64_t timestamp,
                           double speed, double heading) {
    if (!stream) return URBIS_ERR_NULL;
    int err = stream_update_location_ex(stream, object_id, x, y, timestamp, 
                                         speed, heading);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

int urbis_stream_update_batch(UrbisStreamHandle *stream, 
                              const uint64_t *object_ids,
                              const double *x, const double *y,
                              const uint64_t *timestamps, size_t count) {
    if (!stream || !object_ids || !x || !y || !timestamps) return URBIS_ERR_NULL;
    int err = stream_update_batch(stream, object_ids, x, y, timestamps, count);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_ALLOC;
}

TrackedObject* urbis_stream_get_object(UrbisStreamHandle *stream, uint64_t object_id) {
    if (!stream) return NULL;
    return stream_get_object(stream, object_id);
}

int urbis_stream_remove_object(UrbisStreamHandle *stream, uint64_t object_id) {
    if (!stream) return URBIS_ERR_NULL;
    int err = stream_remove_object(stream, object_id);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_NOT_FOUND;
}

/* ----------------------------------------------------------------------------
 * Geofencing
 * --------------------------------------------------------------------------- */

int urbis_geofence_add(UrbisStreamHandle *stream, uint64_t zone_id, 
                       const char *name, const Point *boundary, size_t count,
                       uint64_t dwell_threshold) {
    if (!stream || !boundary || count < 3) return URBIS_ERR_NULL;
    
    GeofenceZone zone;
    zone.zone_id = zone_id;
    strncpy(zone.name, name ? name : "", sizeof(zone.name) - 1);
    zone.name[sizeof(zone.name) - 1] = '\0';
    zone.active = true;
    zone.dwell_threshold = dwell_threshold;
    zone.user_data = NULL;
    
    /* Initialize polygon */
    if (polygon_init(&zone.boundary, count) != GEOM_OK) {
        return URBIS_ERR_ALLOC;
    }
    
    for (size_t i = 0; i < count; i++) {
        polygon_add_exterior_point(&zone.boundary, boundary[i]);
    }
    
    polygon_mbr(&zone.boundary, &zone.mbr);
    
    int err = stream_geofence_add(stream, &zone);
    polygon_free(&zone.boundary);
    
    return (err == STREAM_OK) ? URBIS_OK : 
           (err == STREAM_ERR_EXISTS) ? URBIS_ERR_INVALID : URBIS_ERR_ALLOC;
}

int urbis_geofence_remove(UrbisStreamHandle *stream, uint64_t zone_id) {
    if (!stream) return URBIS_ERR_NULL;
    int err = stream_geofence_remove(stream, zone_id);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_NOT_FOUND;
}

uint64_t* urbis_geofence_check(UrbisStreamHandle *stream, double x, double y,
                               size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    Point p = point_create(x, y);
    return stream_geofence_check_point(stream, &p, count);
}

uint64_t* urbis_geofence_objects(UrbisStreamHandle *stream, uint64_t zone_id,
                                  size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    return stream_geofence_objects_in_zone(stream, zone_id, count);
}

/* Wrapper for geofence callback */
static UrbisGeofenceCallback g_geofence_cb = NULL;
static void *g_geofence_cb_data = NULL;

static void geofence_callback_wrapper(const GeofenceEvent *event, void *user_data) {
    (void)user_data;
    if (g_geofence_cb) {
        g_geofence_cb(event->event_id, event->object_id, event->zone_id,
                      (int)event->type, event->timestamp,
                      event->position.x, event->position.y, g_geofence_cb_data);
    }
}

int urbis_geofence_set_callback(UrbisStreamHandle *stream, 
                                 UrbisGeofenceCallback callback,
                                 void *user_data) {
    if (!stream) return URBIS_ERR_NULL;
    
    g_geofence_cb = callback;
    g_geofence_cb_data = user_data;
    
    return stream_geofence_set_callback(stream, callback ? geofence_callback_wrapper : NULL, 
                                         user_data) == STREAM_OK ? URBIS_OK : URBIS_ERR_INVALID;
}

/* ----------------------------------------------------------------------------
 * Proximity Alerts
 * --------------------------------------------------------------------------- */

uint64_t urbis_proximity_add_rule(UrbisStreamHandle *stream, 
                                   uint64_t object_a, uint64_t object_b,
                                   double threshold, bool one_shot) {
    if (!stream || threshold <= 0) return 0;
    
    ProximityRule rule = {
        .rule_id = 0,  /* Will be assigned */
        .object_a = object_a,
        .object_b = object_b,
        .threshold = threshold,
        .one_shot = one_shot,
        .active = true,
        .user_data = NULL
    };
    
    int err = stream_proximity_add_rule(stream, &rule);
    if (err != STREAM_OK) return 0;
    
    /* Return the last rule ID (simple implementation) */
    pthread_rwlock_rdlock(&stream->proximity.lock);
    uint64_t rule_id = stream->proximity.next_rule_id - 1;
    pthread_rwlock_unlock(&stream->proximity.lock);
    
    return rule_id;
}

int urbis_proximity_remove_rule(UrbisStreamHandle *stream, uint64_t rule_id) {
    if (!stream) return URBIS_ERR_NULL;
    int err = stream_proximity_remove_rule(stream, rule_id);
    return (err == STREAM_OK) ? URBIS_OK : URBIS_ERR_NOT_FOUND;
}

uint64_t* urbis_proximity_query(UrbisStreamHandle *stream, double x, double y,
                                 double distance, size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    Point p = point_create(x, y);
    return stream_proximity_query(stream, &p, distance, count);
}

uint64_t* urbis_proximity_query_object(UrbisStreamHandle *stream, 
                                        uint64_t object_id, double distance,
                                        size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    return stream_proximity_query_object(stream, object_id, distance, count);
}

/* Wrapper for proximity callback */
static UrbisProximityCallback g_proximity_cb = NULL;
static void *g_proximity_cb_data = NULL;

static void proximity_callback_wrapper(const ProximityEvent *event, void *user_data) {
    (void)user_data;
    if (g_proximity_cb) {
        g_proximity_cb(event->event_id, event->rule_id, event->object_a,
                       event->object_b, event->distance, event->timestamp,
                       g_proximity_cb_data);
    }
}

int urbis_proximity_set_callback(UrbisStreamHandle *stream,
                                  UrbisProximityCallback callback,
                                  void *user_data) {
    if (!stream) return URBIS_ERR_NULL;
    
    g_proximity_cb = callback;
    g_proximity_cb_data = user_data;
    
    return stream_proximity_set_callback(stream, callback ? proximity_callback_wrapper : NULL,
                                          user_data) == STREAM_OK ? URBIS_OK : URBIS_ERR_INVALID;
}

/* ----------------------------------------------------------------------------
 * Trajectory Analysis
 * --------------------------------------------------------------------------- */

UrbisTrajectoryStats* urbis_trajectory_stats(UrbisStreamHandle *stream,
                                              uint64_t object_id,
                                              uint64_t start_time,
                                              uint64_t end_time) {
    if (!stream) return NULL;
    
    TrajectoryStats *internal_stats = stream_trajectory_stats(stream, object_id,
                                                               start_time, end_time);
    if (!internal_stats) return NULL;
    
    UrbisTrajectoryStats *stats = malloc(sizeof(UrbisTrajectoryStats));
    if (!stats) {
        stream_trajectory_stats_free(internal_stats);
        return NULL;
    }
    
    stats->object_id = internal_stats->object_id;
    stats->total_distance = internal_stats->total_distance;
    stats->avg_speed = internal_stats->avg_speed;
    stats->max_speed = internal_stats->max_speed;
    stats->total_time = internal_stats->total_time;
    stats->moving_time = internal_stats->moving_time;
    stats->stopped_time = internal_stats->stopped_time;
    stats->start_x = internal_stats->start_point.x;
    stats->start_y = internal_stats->start_point.y;
    stats->end_x = internal_stats->end_point.x;
    stats->end_y = internal_stats->end_point.y;
    stats->start_time = internal_stats->start_time;
    stats->end_time = internal_stats->end_time;
    stats->point_count = internal_stats->point_count;
    stats->stop_count = internal_stats->stop_count;
    
    stream_trajectory_stats_free(internal_stats);
    return stats;
}

void urbis_trajectory_stats_free(UrbisTrajectoryStats *stats) {
    free(stats);
}

Point* urbis_trajectory_path(UrbisStreamHandle *stream, uint64_t object_id,
                              uint64_t start_time, uint64_t end_time,
                              size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    LineString *ls = stream_trajectory_path(stream, object_id, start_time, end_time);
    if (!ls) {
        *count = 0;
        return NULL;
    }
    
    Point *points = malloc(ls->count * sizeof(Point));
    if (!points) {
        linestring_free(ls);
        free(ls);
        *count = 0;
        return NULL;
    }
    
    memcpy(points, ls->points, ls->count * sizeof(Point));
    *count = ls->count;
    
    linestring_free(ls);
    free(ls);
    
    return points;
}

Point* urbis_trajectory_simplified(UrbisStreamHandle *stream, uint64_t object_id,
                                    uint64_t start_time, uint64_t end_time,
                                    double tolerance, size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    LineString *ls = stream_trajectory_simplified(stream, object_id, 
                                                   start_time, end_time, tolerance);
    if (!ls) {
        *count = 0;
        return NULL;
    }
    
    Point *points = malloc(ls->count * sizeof(Point));
    if (!points) {
        linestring_free(ls);
        free(ls);
        *count = 0;
        return NULL;
    }
    
    memcpy(points, ls->points, ls->count * sizeof(Point));
    *count = ls->count;
    
    linestring_free(ls);
    free(ls);
    
    return points;
}

/* ----------------------------------------------------------------------------
 * Event Handling
 * --------------------------------------------------------------------------- */

UrbisStreamEvent* urbis_stream_poll_event(UrbisStreamHandle *stream) {
    if (!stream) return NULL;
    
    StreamEvent *internal = stream_poll_event(stream);
    if (!internal) return NULL;
    
    UrbisStreamEvent *event = malloc(sizeof(UrbisStreamEvent));
    if (!event) {
        stream_event_free(internal);
        return NULL;
    }
    
    event->event_type = (int)internal->type;
    event->timestamp = internal->timestamp;
    
    switch (internal->type) {
        case EVENT_GEOFENCE:
            event->object_id = internal->data.geofence.object_id;
            event->zone_id = internal->data.geofence.zone_id;
            event->x = internal->data.geofence.position.x;
            event->y = internal->data.geofence.position.y;
            event->distance = 0;
            event->speed = 0;
            event->other_object = 0;
            break;
            
        case EVENT_PROXIMITY:
            event->object_id = internal->data.proximity.object_a;
            event->other_object = internal->data.proximity.object_b;
            event->x = internal->data.proximity.position_a.x;
            event->y = internal->data.proximity.position_a.y;
            event->distance = internal->data.proximity.distance;
            event->zone_id = 0;
            event->speed = 0;
            break;
            
        default:
            event->object_id = internal->data.movement.object_id;
            event->x = internal->data.movement.position.x;
            event->y = internal->data.movement.position.y;
            event->speed = internal->data.movement.speed;
            event->zone_id = 0;
            event->other_object = 0;
            event->distance = 0;
            break;
    }
    
    stream_event_free(internal);
    return event;
}

UrbisStreamEvent* urbis_stream_wait_event(UrbisStreamHandle *stream, 
                                           uint64_t timeout_ms) {
    if (!stream) return NULL;
    
    StreamEvent *internal = stream_wait_event(stream, timeout_ms);
    if (!internal) return NULL;
    
    UrbisStreamEvent *event = malloc(sizeof(UrbisStreamEvent));
    if (!event) {
        stream_event_free(internal);
        return NULL;
    }
    
    event->event_type = (int)internal->type;
    event->timestamp = internal->timestamp;
    
    switch (internal->type) {
        case EVENT_GEOFENCE:
            event->object_id = internal->data.geofence.object_id;
            event->zone_id = internal->data.geofence.zone_id;
            event->x = internal->data.geofence.position.x;
            event->y = internal->data.geofence.position.y;
            event->distance = 0;
            event->speed = 0;
            event->other_object = 0;
            break;
            
        case EVENT_PROXIMITY:
            event->object_id = internal->data.proximity.object_a;
            event->other_object = internal->data.proximity.object_b;
            event->x = internal->data.proximity.position_a.x;
            event->y = internal->data.proximity.position_a.y;
            event->distance = internal->data.proximity.distance;
            event->zone_id = 0;
            event->speed = 0;
            break;
            
        default:
            event->object_id = internal->data.movement.object_id;
            event->x = internal->data.movement.position.x;
            event->y = internal->data.movement.position.y;
            event->speed = internal->data.movement.speed;
            event->zone_id = 0;
            event->other_object = 0;
            event->distance = 0;
            break;
    }
    
    stream_event_free(internal);
    return event;
}

size_t urbis_stream_event_count(UrbisStreamHandle *stream) {
    if (!stream) return 0;
    return stream_event_count(stream);
}

void urbis_stream_event_free(UrbisStreamEvent *event) {
    free(event);
}

/* ----------------------------------------------------------------------------
 * Statistics
 * --------------------------------------------------------------------------- */

void urbis_stream_get_stats(UrbisStreamHandle *stream, UrbisStreamStats *stats) {
    if (!stream || !stats) return;
    
    StreamStats internal;
    stream_get_stats(stream, &internal);
    
    stats->tracked_objects = internal.tracked_objects;
    stats->geofence_zones = internal.geofence_zones;
    stats->proximity_rules = internal.proximity_rules;
    stats->pending_events = internal.pending_events;
    stats->total_updates = internal.total_updates;
    stats->total_events = internal.total_events;
}

