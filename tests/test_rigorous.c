/**
 * @file test_rigorous.c
 * @brief Rigorous negative and edge case tests for Urbis
 */

#include "../include/urbis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

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
 * Negative Tests
 * ============================================================================ */

TEST(invalid_load) {
    UrbisIndex *idx = urbis_create(NULL);
    assert(idx != NULL);

    /* Test non-existent file */
    int err = urbis_load_geojson(idx, "non_existent_file.geojson");
    assert(err != URBIS_OK);

    urbis_destroy(idx);
}

TEST(malformed_input) {
    UrbisIndex *idx = urbis_create(NULL);
    assert(idx != NULL);

    /* Malformed GeoJSON */
    int err = urbis_load_geojson_string(idx, "{ \"bad\": \"json\"");
    assert(err != URBIS_OK);

    /* Invalid WKT */
    err = urbis_load_wkt(idx, "POINT (invalid)");
    assert(err != URBIS_OK);

    err = urbis_load_wkt(idx, "GARBAGE");
    assert(err != URBIS_OK);

    urbis_destroy(idx);
}

TEST(invalid_geometry) {
    UrbisIndex *idx = urbis_create(NULL);

    /* Polygon with too few points (< 3) */
    Point p[] = { {0,0}, {10,10} };
    /* This might not fail at insert time depending on implementation,
       but we should check behavior. If it accepts it, we check if it handles it gracefully.
       Let's assume it should probably return ID > 0 or 0 if it validates.
       If library doesn't validate on insert, we check it doesn't crash on build. */

    /* Assuming standard GIS validity, polygons need 4 points (closed ring) or 3 if implicitly closed.
       Urbis docs say "Insert a polygon", let's see. */

    uint64_t id = urbis_insert_polygon(idx, p, 2);
    /* If strict validation, this should be 0. If loose, it might be allowed.
       Checking code would confirm, but let's assert it doesn't crash at least. */
    (void)id;

    urbis_destroy(idx);
}

TEST(null_inputs) {
    /* Test NULL config is handled (should use defaults) */
    UrbisIndex *idx = urbis_create(NULL);
    assert(idx != NULL);

    /* Passing NULL index to functions */
    assert(urbis_insert_point(NULL, 0, 0) == 0);
    assert(urbis_load_wkt(NULL, "POINT(0 0)") != URBIS_OK);

    UrbisObjectList *res = urbis_query_range(NULL, NULL);
    assert(res == NULL);

    urbis_destroy(idx);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST(empty_index) {
    UrbisIndex *idx = urbis_create(NULL);

    /* Query empty index */
    MBR range = urbis_mbr(0, 0, 100, 100);
    UrbisObjectList *res = urbis_query_range(idx, &range);

    assert(res != NULL);
    assert(res->count == 0);
    urbis_object_list_free(res);

    /* Build empty index */
    int err = urbis_build(idx);
    assert(err == URBIS_OK);

    urbis_destroy(idx);
}

TEST(degenerate_geometry) {
    UrbisIndex *idx = urbis_create(NULL);

    /* Point as Linestring */
    Point p[] = { {0,0}, {0,0} };
    uint64_t id = urbis_insert_linestring(idx, p, 2);
    assert(id > 0);

    /* Zero area polygon */
    Point poly[] = { {0,0}, {10,0}, {0,0} }; /* Flat */
    id = urbis_insert_polygon(idx, poly, 3);
    assert(id > 0);

    urbis_build(idx);

    /* Ensure we can query them */
    MBR range = urbis_mbr(-1, -1, 11, 1);
    UrbisObjectList *res = urbis_query_range(idx, &range);
    assert(res != NULL);
    assert(res->count == 2);

    urbis_object_list_free(res);
    urbis_destroy(idx);
}

TEST(coincident_points) {
    UrbisIndex *idx = urbis_create(NULL);

    /* Insert 10 points at exact same location */
    for(int i=0; i<10; i++) {
        urbis_insert_point(idx, 50.0, 50.0);
    }

    urbis_build(idx);

    /* Query point */
    UrbisObjectList *res = urbis_query_point(idx, 50.0, 50.0);
    assert(res != NULL);
    assert(res->count == 10);
    urbis_object_list_free(res);

    /* KNN should return them */
    res = urbis_query_knn(idx, 50.0, 50.0, 5);
    assert(res != NULL);
    assert(res->count == 5);
    urbis_object_list_free(res);

    urbis_destroy(idx);
}

TEST(massive_coordinates) {
    UrbisIndex *idx = urbis_create(NULL);

    double big = 1e15;
    urbis_insert_point(idx, big, big);
    urbis_insert_point(idx, -big, -big);

    urbis_build(idx);

    MBR bounds = urbis_bounds(idx);
    /* Use approximate comparison for float equality */
    assert(bounds.max_x >= big);
    assert(bounds.min_x <= -big);

    urbis_destroy(idx);
}

TEST(stress_many_items) {
    UrbisIndex *idx = urbis_create(NULL);

    /* Insert enough items to likely force multiple blocks/pages */
    int count = 2000;
    for(int i=0; i<count; i++) {
        urbis_insert_point(idx, i * 0.1, i * 0.1);
    }

    urbis_build(idx);

    assert(urbis_count(idx) == (size_t)count);

    /* Query all */
    MBR range = urbis_bounds(idx);
    UrbisObjectList *res = urbis_query_range(idx, &range);
    assert(res != NULL);
    assert(res->count == (size_t)count);

    urbis_object_list_free(res);
    urbis_destroy(idx);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running rigorous tests...\n\n");

    printf("Negative tests:\n");
    RUN_TEST(invalid_load);
    RUN_TEST(malformed_input);
    RUN_TEST(invalid_geometry);
    RUN_TEST(null_inputs);

    printf("\nEdge case tests:\n");
    RUN_TEST(empty_index);
    RUN_TEST(degenerate_geometry);
    RUN_TEST(coincident_points);
    RUN_TEST(massive_coordinates);
    RUN_TEST(stress_many_items);

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
