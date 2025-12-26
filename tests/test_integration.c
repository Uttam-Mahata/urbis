/**
 * @file test_integration.c
 * @brief Integration tests for the Urbis spatial index
 */

#include "../include/urbis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define EPSILON 1e-6
#define ASSERT_NEAR(a, b) assert(fabs((a) - (b)) < EPSILON)

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Testing " #name "... "); \
        test_##name(); \
        printf("PASSED\n"); \
        tests_passed++; \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST(basic_workflow) {
    /* Create index */
    UrbisIndex *idx = urbis_create(NULL);
    assert(idx != NULL);
    
    /* Insert points */
    uint64_t id1 = urbis_insert_point(idx, 10, 20);
    uint64_t id2 = urbis_insert_point(idx, 30, 40);
    uint64_t id3 = urbis_insert_point(idx, 50, 60);
    
    assert(id1 > 0);
    assert(id2 > 0);
    assert(id3 > 0);
    
    /* Check count */
    assert(urbis_count(idx) == 3);
    
    /* Build index */
    int err = urbis_build(idx);
    assert(err == URBIS_OK);
    
    /* Query range */
    MBR range = urbis_mbr(0, 0, 35, 45);
    UrbisObjectList *result = urbis_query_range(idx, &range);
    assert(result != NULL);
    assert(result->count == 2);  /* Points (10,20) and (30,40) */
    
    urbis_object_list_free(result);
    urbis_destroy(idx);
}

TEST(linestring_workflow) {
    UrbisIndex *idx = urbis_create(NULL);
    
    /* Insert linestring */
    Point road[] = {
        point_create(0, 0),
        point_create(100, 0),
        point_create(100, 100),
        point_create(0, 100)
    };
    
    uint64_t id = urbis_insert_linestring(idx, road, 4);
    assert(id > 0);
    
    urbis_build(idx);
    
    /* Query should find the linestring */
    MBR range = urbis_mbr(40, -10, 60, 10);
    UrbisObjectList *result = urbis_query_range(idx, &range);
    assert(result != NULL);
    assert(result->count == 1);
    
    urbis_object_list_free(result);
    urbis_destroy(idx);
}

TEST(polygon_workflow) {
    UrbisIndex *idx = urbis_create(NULL);
    
    /* Insert polygon (building) */
    Point building[] = {
        point_create(10, 10),
        point_create(30, 10),
        point_create(30, 30),
        point_create(10, 30),
        point_create(10, 10)
    };
    
    uint64_t id = urbis_insert_polygon(idx, building, 5);
    assert(id > 0);
    
    /* Get the object back */
    SpatialObject *obj = urbis_get(idx, id);
    assert(obj != NULL);
    assert(obj->type == GEOM_POLYGON);
    
    /* Centroid should be at (20, 20) */
    ASSERT_NEAR(obj->centroid.x, 20);
    ASSERT_NEAR(obj->centroid.y, 20);
    
    urbis_destroy(idx);
}

TEST(geojson_loading) {
    UrbisIndex *idx = urbis_create(NULL);
    
    const char *geojson = "{"
        "\"type\": \"FeatureCollection\","
        "\"features\": ["
        "  {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [10, 20]}},"
        "  {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [30, 40]}},"
        "  {\"type\": \"Feature\", \"geometry\": {\"type\": \"LineString\", \"coordinates\": [[0,0],[50,50]]}}"
        "]"
    "}";
    
    int err = urbis_load_geojson_string(idx, geojson);
    assert(err == URBIS_OK);
    assert(urbis_count(idx) == 3);
    
    urbis_destroy(idx);
}

TEST(adjacent_pages) {
    UrbisConfig config = urbis_default_config();
    config.page_capacity = 4;  /* Small pages to force multiple pages */
    
    UrbisIndex *idx = urbis_create(&config);
    
    /* Insert many points across a large area */
    for (int i = 0; i < 50; i++) {
        urbis_insert_point(idx, (i % 10) * 100, (i / 10) * 100);
    }
    
    /* Build index */
    urbis_build(idx);
    
    /* Find adjacent pages in a region */
    MBR region = urbis_mbr(150, 150, 350, 350);
    UrbisPageList *pages = urbis_find_adjacent_pages(idx, &region);
    
    /* Should have found some pages */
    if (pages) {
        printf("(found %zu pages, ~%zu seeks) ", pages->count, pages->estimated_seeks);
        urbis_page_list_free(pages);
    }
    
    urbis_destroy(idx);
}

TEST(knn_query) {
    UrbisIndex *idx = urbis_create(NULL);
    
    /* Insert points */
    urbis_insert_point(idx, 0, 0);
    urbis_insert_point(idx, 1, 1);
    urbis_insert_point(idx, 2, 2);
    urbis_insert_point(idx, 10, 10);
    urbis_insert_point(idx, 20, 20);
    
    urbis_build(idx);
    
    /* Find 3 nearest to origin */
    UrbisObjectList *result = urbis_query_knn(idx, 0.5, 0.5, 3);
    assert(result != NULL);
    assert(result->count == 3);
    
    urbis_object_list_free(result);
    urbis_destroy(idx);
}

TEST(query_adjacent) {
    UrbisIndex *idx = urbis_create(NULL);
    
    /* Create a grid of points */
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            urbis_insert_point(idx, i * 10, j * 10);
        }
    }
    
    urbis_build(idx);
    
    /* Query adjacent to a region */
    MBR region = urbis_mbr(25, 25, 45, 45);
    UrbisObjectList *result = urbis_query_adjacent(idx, &region);
    
    /* Should find points in that area */
    assert(result != NULL);
    assert(result->count > 0);
    
    urbis_object_list_free(result);
    urbis_destroy(idx);
}

TEST(remove_object) {
    UrbisIndex *idx = urbis_create(NULL);
    
    uint64_t id1 = urbis_insert_point(idx, 10, 10);
    uint64_t id2 = urbis_insert_point(idx, 20, 20);
    uint64_t id3 = urbis_insert_point(idx, 30, 30);
    
    assert(urbis_count(idx) == 3);
    
    /* Remove middle point */
    int err = urbis_remove(idx, id2);
    assert(err == URBIS_OK);
    assert(urbis_count(idx) == 2);
    
    /* Object should be gone */
    assert(urbis_get(idx, id2) == NULL);
    
    /* Others should still exist */
    assert(urbis_get(idx, id1) != NULL);
    assert(urbis_get(idx, id3) != NULL);
    
    urbis_destroy(idx);
}

TEST(bounds) {
    UrbisIndex *idx = urbis_create(NULL);
    
    urbis_insert_point(idx, -100, -50);
    urbis_insert_point(idx, 200, 150);
    
    MBR bounds = urbis_bounds(idx);
    
    ASSERT_NEAR(bounds.min_x, -100);
    ASSERT_NEAR(bounds.min_y, -50);
    ASSERT_NEAR(bounds.max_x, 200);
    ASSERT_NEAR(bounds.max_y, 150);
    
    urbis_destroy(idx);
}

TEST(stats) {
    UrbisIndex *idx = urbis_create(NULL);
    
    for (int i = 0; i < 100; i++) {
        urbis_insert_point(idx, i * 10, i * 5);
    }
    
    urbis_build(idx);
    
    UrbisStats stats;
    urbis_get_stats(idx, &stats);
    
    assert(stats.total_objects == 100);
    assert(stats.total_pages > 0);
    
    /* Print stats for manual inspection */
    printf("\n");
    urbis_print_stats(idx, stdout);
    
    urbis_destroy(idx);
}

TEST(wkt_loading) {
    UrbisIndex *idx = urbis_create(NULL);
    
    int err = urbis_load_wkt(idx, "POINT (10 20)");
    assert(err == URBIS_OK);
    
    err = urbis_load_wkt(idx, "LINESTRING (0 0, 10 10, 20 0)");
    assert(err == URBIS_OK);
    
    err = urbis_load_wkt(idx, "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))");
    assert(err == URBIS_OK);
    
    assert(urbis_count(idx) == 3);
    
    urbis_destroy(idx);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running integration tests...\n\n");
    
    printf("Version: %s\n\n", urbis_version());
    
    RUN_TEST(basic_workflow);
    RUN_TEST(linestring_workflow);
    RUN_TEST(polygon_workflow);
    RUN_TEST(geojson_loading);
    RUN_TEST(adjacent_pages);
    RUN_TEST(knn_query);
    RUN_TEST(query_adjacent);
    RUN_TEST(remove_object);
    RUN_TEST(bounds);
    RUN_TEST(stats);
    RUN_TEST(wkt_loading);
    
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}

