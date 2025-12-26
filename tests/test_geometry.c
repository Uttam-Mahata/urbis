/**
 * @file test_geometry.c
 * @brief Unit tests for geometry primitives
 */

#include "../include/geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define EPSILON 1e-6
#define ASSERT_NEAR(a, b) assert(fabs((a) - (b)) < EPSILON)

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
 * Point Tests
 * ============================================================================ */

TEST(point_create) {
    Point p = point_create(10.5, 20.3);
    ASSERT_NEAR(p.x, 10.5);
    ASSERT_NEAR(p.y, 20.3);
}

TEST(point_distance) {
    Point a = point_create(0, 0);
    Point b = point_create(3, 4);
    
    double dist = point_distance(&a, &b);
    ASSERT_NEAR(dist, 5.0);
}

TEST(point_distance_sq) {
    Point a = point_create(0, 0);
    Point b = point_create(3, 4);
    
    double dist_sq = point_distance_sq(&a, &b);
    ASSERT_NEAR(dist_sq, 25.0);
}

TEST(point_equals) {
    Point a = point_create(10.0, 20.0);
    Point b = point_create(10.0 + 1e-12, 20.0 - 1e-12);
    Point c = point_create(10.1, 20.0);
    
    assert(point_equals(&a, &b, 1e-6));
    assert(!point_equals(&a, &c, 1e-6));
}

/* ============================================================================
 * LineString Tests
 * ============================================================================ */

TEST(linestring_init) {
    LineString ls;
    int err = linestring_init(&ls, 16);
    assert(err == GEOM_OK);
    assert(ls.points != NULL);
    assert(ls.capacity >= 16);
    assert(ls.count == 0);
    linestring_free(&ls);
}

TEST(linestring_add_point) {
    LineString ls;
    linestring_init(&ls, 4);
    
    linestring_add_point(&ls, point_create(0, 0));
    linestring_add_point(&ls, point_create(1, 1));
    linestring_add_point(&ls, point_create(2, 0));
    
    assert(ls.count == 3);
    ASSERT_NEAR(ls.points[0].x, 0);
    ASSERT_NEAR(ls.points[1].x, 1);
    ASSERT_NEAR(ls.points[2].x, 2);
    
    linestring_free(&ls);
}

TEST(linestring_centroid) {
    LineString ls;
    linestring_init(&ls, 4);
    
    /* Create a simple line: (0,0) -> (10,0) */
    linestring_add_point(&ls, point_create(0, 0));
    linestring_add_point(&ls, point_create(10, 0));
    
    Point centroid;
    int err = linestring_centroid(&ls, &centroid);
    assert(err == GEOM_OK);
    ASSERT_NEAR(centroid.x, 5.0);
    ASSERT_NEAR(centroid.y, 0.0);
    
    linestring_free(&ls);
}

TEST(linestring_mbr) {
    LineString ls;
    linestring_init(&ls, 4);
    
    linestring_add_point(&ls, point_create(-5, 10));
    linestring_add_point(&ls, point_create(15, -3));
    linestring_add_point(&ls, point_create(8, 20));
    
    MBR mbr;
    int err = linestring_mbr(&ls, &mbr);
    assert(err == GEOM_OK);
    ASSERT_NEAR(mbr.min_x, -5);
    ASSERT_NEAR(mbr.min_y, -3);
    ASSERT_NEAR(mbr.max_x, 15);
    ASSERT_NEAR(mbr.max_y, 20);
    
    linestring_free(&ls);
}

TEST(linestring_length) {
    LineString ls;
    linestring_init(&ls, 4);
    
    linestring_add_point(&ls, point_create(0, 0));
    linestring_add_point(&ls, point_create(3, 4));
    linestring_add_point(&ls, point_create(6, 0));
    
    double len = linestring_length(&ls);
    ASSERT_NEAR(len, 10.0);  /* 5 + 5 */
    
    linestring_free(&ls);
}

/* ============================================================================
 * Polygon Tests
 * ============================================================================ */

TEST(polygon_init) {
    Polygon poly;
    int err = polygon_init(&poly, 16);
    assert(err == GEOM_OK);
    assert(poly.exterior != NULL);
    assert(poly.ext_capacity >= 16);
    polygon_free(&poly);
}

TEST(polygon_add_exterior) {
    Polygon poly;
    polygon_init(&poly, 4);
    
    polygon_add_exterior_point(&poly, point_create(0, 0));
    polygon_add_exterior_point(&poly, point_create(10, 0));
    polygon_add_exterior_point(&poly, point_create(10, 10));
    polygon_add_exterior_point(&poly, point_create(0, 10));
    polygon_add_exterior_point(&poly, point_create(0, 0));
    
    assert(poly.ext_count == 5);
    
    polygon_free(&poly);
}

TEST(polygon_centroid) {
    Polygon poly;
    polygon_init(&poly, 5);
    
    /* Create a square: (0,0) -> (10,0) -> (10,10) -> (0,10) */
    polygon_add_exterior_point(&poly, point_create(0, 0));
    polygon_add_exterior_point(&poly, point_create(10, 0));
    polygon_add_exterior_point(&poly, point_create(10, 10));
    polygon_add_exterior_point(&poly, point_create(0, 10));
    polygon_add_exterior_point(&poly, point_create(0, 0));
    
    Point centroid;
    int err = polygon_centroid(&poly, &centroid);
    assert(err == GEOM_OK);
    ASSERT_NEAR(centroid.x, 5.0);
    ASSERT_NEAR(centroid.y, 5.0);
    
    polygon_free(&poly);
}

TEST(polygon_area) {
    Polygon poly;
    polygon_init(&poly, 5);
    
    /* Create a square 10x10 */
    polygon_add_exterior_point(&poly, point_create(0, 0));
    polygon_add_exterior_point(&poly, point_create(10, 0));
    polygon_add_exterior_point(&poly, point_create(10, 10));
    polygon_add_exterior_point(&poly, point_create(0, 10));
    polygon_add_exterior_point(&poly, point_create(0, 0));
    
    double area = polygon_area(&poly);
    ASSERT_NEAR(area, 100.0);
    
    polygon_free(&poly);
}

TEST(polygon_add_hole) {
    Polygon poly;
    polygon_init(&poly, 5);
    
    /* Exterior */
    polygon_add_exterior_point(&poly, point_create(0, 0));
    polygon_add_exterior_point(&poly, point_create(10, 0));
    polygon_add_exterior_point(&poly, point_create(10, 10));
    polygon_add_exterior_point(&poly, point_create(0, 10));
    polygon_add_exterior_point(&poly, point_create(0, 0));
    
    /* Add hole */
    int err = polygon_add_hole(&poly, 5);
    assert(err == GEOM_OK);
    
    polygon_add_hole_point(&poly, 0, point_create(2, 2));
    polygon_add_hole_point(&poly, 0, point_create(8, 2));
    polygon_add_hole_point(&poly, 0, point_create(8, 8));
    polygon_add_hole_point(&poly, 0, point_create(2, 8));
    polygon_add_hole_point(&poly, 0, point_create(2, 2));
    
    assert(poly.num_holes == 1);
    assert(poly.hole_counts[0] == 5);
    
    double area = polygon_area(&poly);
    ASSERT_NEAR(area, 64.0);  /* 100 - 36 */
    
    polygon_free(&poly);
}

/* ============================================================================
 * MBR Tests
 * ============================================================================ */

TEST(mbr_create) {
    MBR mbr = mbr_create(0, 0, 10, 10);
    ASSERT_NEAR(mbr.min_x, 0);
    ASSERT_NEAR(mbr.min_y, 0);
    ASSERT_NEAR(mbr.max_x, 10);
    ASSERT_NEAR(mbr.max_y, 10);
}

TEST(mbr_empty) {
    MBR mbr = mbr_empty();
    assert(mbr_is_empty(&mbr));
}

TEST(mbr_expand_point) {
    MBR mbr = mbr_empty();
    Point p1 = point_create(5, 5);
    Point p2 = point_create(-3, 10);
    
    mbr_expand_point(&mbr, &p1);
    mbr_expand_point(&mbr, &p2);
    
    ASSERT_NEAR(mbr.min_x, -3);
    ASSERT_NEAR(mbr.min_y, 5);
    ASSERT_NEAR(mbr.max_x, 5);
    ASSERT_NEAR(mbr.max_y, 10);
}

TEST(mbr_intersects) {
    MBR a = mbr_create(0, 0, 10, 10);
    MBR b = mbr_create(5, 5, 15, 15);
    MBR c = mbr_create(20, 20, 30, 30);
    
    assert(mbr_intersects(&a, &b));
    assert(!mbr_intersects(&a, &c));
}

TEST(mbr_contains_point) {
    MBR mbr = mbr_create(0, 0, 10, 10);
    Point inside = point_create(5, 5);
    Point outside = point_create(15, 5);
    
    assert(mbr_contains_point(&mbr, &inside));
    assert(!mbr_contains_point(&mbr, &outside));
}

TEST(mbr_centroid) {
    MBR mbr = mbr_create(0, 0, 10, 20);
    Point c = mbr_centroid(&mbr);
    ASSERT_NEAR(c.x, 5);
    ASSERT_NEAR(c.y, 10);
}

TEST(mbr_area) {
    MBR mbr = mbr_create(0, 0, 10, 20);
    double area = mbr_area(&mbr);
    ASSERT_NEAR(area, 200.0);
}

/* ============================================================================
 * SpatialObject Tests
 * ============================================================================ */

TEST(spatial_object_point) {
    SpatialObject obj;
    int err = spatial_object_init_point(&obj, 42, point_create(5, 10));
    assert(err == GEOM_OK);
    assert(obj.id == 42);
    assert(obj.type == GEOM_POINT);
    ASSERT_NEAR(obj.centroid.x, 5);
    ASSERT_NEAR(obj.centroid.y, 10);
    spatial_object_free(&obj);
}

TEST(spatial_object_linestring) {
    SpatialObject obj;
    int err = spatial_object_init_linestring(&obj, 100, 4);
    assert(err == GEOM_OK);
    
    linestring_add_point(&obj.geom.line, point_create(0, 0));
    linestring_add_point(&obj.geom.line, point_create(10, 10));
    
    err = spatial_object_update_derived(&obj);
    assert(err == GEOM_OK);
    
    ASSERT_NEAR(obj.centroid.x, 5);
    ASSERT_NEAR(obj.centroid.y, 5);
    
    spatial_object_free(&obj);
}

TEST(spatial_object_copy) {
    SpatialObject src;
    spatial_object_init_point(&src, 99, point_create(7, 8));
    
    SpatialObject dest;
    int err = spatial_object_copy(&dest, &src);
    assert(err == GEOM_OK);
    
    assert(dest.id == 99);
    ASSERT_NEAR(dest.centroid.x, 7);
    ASSERT_NEAR(dest.centroid.y, 8);
    
    spatial_object_free(&src);
    spatial_object_free(&dest);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running geometry tests...\n\n");
    
    printf("Point tests:\n");
    RUN_TEST(point_create);
    RUN_TEST(point_distance);
    RUN_TEST(point_distance_sq);
    RUN_TEST(point_equals);
    
    printf("\nLineString tests:\n");
    RUN_TEST(linestring_init);
    RUN_TEST(linestring_add_point);
    RUN_TEST(linestring_centroid);
    RUN_TEST(linestring_mbr);
    RUN_TEST(linestring_length);
    
    printf("\nPolygon tests:\n");
    RUN_TEST(polygon_init);
    RUN_TEST(polygon_add_exterior);
    RUN_TEST(polygon_centroid);
    RUN_TEST(polygon_area);
    RUN_TEST(polygon_add_hole);
    
    printf("\nMBR tests:\n");
    RUN_TEST(mbr_create);
    RUN_TEST(mbr_empty);
    RUN_TEST(mbr_expand_point);
    RUN_TEST(mbr_intersects);
    RUN_TEST(mbr_contains_point);
    RUN_TEST(mbr_centroid);
    RUN_TEST(mbr_area);
    
    printf("\nSpatialObject tests:\n");
    RUN_TEST(spatial_object_point);
    RUN_TEST(spatial_object_linestring);
    RUN_TEST(spatial_object_copy);
    
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}

