/**
 * @file test_spatial_ops.c
 * @brief Unit tests for advanced spatial operations
 */

#include "../include/spatial_ops.h"
#include "../include/geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define EPSILON 1e-6

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Testing %s... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED\n"); \
            printf("    Assertion failed: %s\n", #expr); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabs((a) - (b)) < (eps))

/* ============================================================================
 * Buffer Tests
 * ============================================================================ */

TEST(buffer_point) {
    Point center = {10.0, 20.0};
    double distance = 5.0;
    int segments = 32;
    
    Polygon *buffer = buffer_point(&center, distance, segments);
    ASSERT(buffer != NULL);
    ASSERT(buffer->ext_count == (size_t)(segments + 1));  // Closed polygon
    
    // Check that all points are at the correct distance from center
    for (size_t i = 0; i < buffer->ext_count - 1; i++) {
        double dist = point_distance(&center, &buffer->exterior[i]);
        ASSERT_NEAR(dist, distance, 0.01);
    }
    
    polygon_free(buffer);
    free(buffer);
}

TEST(buffer_point_small_segments) {
    Point center = {0.0, 0.0};
    
    // Minimum segments should work
    Polygon *buffer = buffer_point(&center, 1.0, 3);
    ASSERT(buffer != NULL);
    ASSERT(buffer->ext_count >= 4);  // Triangle + closure
    
    polygon_free(buffer);
    free(buffer);
}

TEST(buffer_point_invalid) {
    Point center = {0.0, 0.0};
    
    // Zero distance should fail
    Polygon *buffer = buffer_point(&center, 0.0, 32);
    ASSERT(buffer == NULL);
    
    // Negative distance should fail
    buffer = buffer_point(&center, -5.0, 32);
    ASSERT(buffer == NULL);
    
    // NULL center should fail
    buffer = buffer_point(NULL, 5.0, 32);
    ASSERT(buffer == NULL);
}

/* ============================================================================
 * Spatial Predicate Tests
 * ============================================================================ */

TEST(point_in_polygon_simple) {
    // Create a square polygon
    Polygon poly;
    polygon_init(&poly, 5);
    
    polygon_add_exterior_point(&poly, (Point){0, 0});
    polygon_add_exterior_point(&poly, (Point){10, 0});
    polygon_add_exterior_point(&poly, (Point){10, 10});
    polygon_add_exterior_point(&poly, (Point){0, 10});
    polygon_add_exterior_point(&poly, (Point){0, 0});
    
    // Point inside
    Point inside = {5, 5};
    ASSERT(point_in_polygon(&inside, &poly));
    
    // Point outside
    Point outside = {15, 5};
    ASSERT(!point_in_polygon(&outside, &poly));
    
    // Point on edge (may or may not be considered inside depending on implementation)
    Point on_edge = {5, 0};
    // Just test it doesn't crash
    point_in_polygon(&on_edge, &poly);
    
    polygon_free(&poly);
}

TEST(point_in_polygon_complex) {
    // L-shaped polygon
    Polygon poly;
    polygon_init(&poly, 7);
    
    polygon_add_exterior_point(&poly, (Point){0, 0});
    polygon_add_exterior_point(&poly, (Point){10, 0});
    polygon_add_exterior_point(&poly, (Point){10, 5});
    polygon_add_exterior_point(&poly, (Point){5, 5});
    polygon_add_exterior_point(&poly, (Point){5, 10});
    polygon_add_exterior_point(&poly, (Point){0, 10});
    polygon_add_exterior_point(&poly, (Point){0, 0});
    
    // Inside L
    Point inside = {2, 2};
    ASSERT(point_in_polygon(&inside, &poly));
    
    // In the "cut out" area
    Point cutout = {7, 7};
    ASSERT(!point_in_polygon(&cutout, &poly));
    
    polygon_free(&poly);
}

TEST(segments_intersect_basic) {
    // Crossing segments
    Point a1 = {0, 0}, a2 = {10, 10};
    Point b1 = {0, 10}, b2 = {10, 0};
    ASSERT(segments_intersect(&a1, &a2, &b1, &b2));
    
    // Parallel segments
    Point c1 = {0, 0}, c2 = {10, 0};
    Point d1 = {0, 5}, d2 = {10, 5};
    ASSERT(!segments_intersect(&c1, &c2, &d1, &d2));
    
    // Non-intersecting segments
    Point e1 = {0, 0}, e2 = {5, 0};
    Point f1 = {6, 0}, f2 = {10, 0};
    ASSERT(!segments_intersect(&e1, &e2, &f1, &f2));
}

TEST(segment_intersection_point_test) {
    Point a1 = {0, 0}, a2 = {10, 10};
    Point b1 = {0, 10}, b2 = {10, 0};
    Point intersection;
    
    ASSERT(segment_intersection_point(&a1, &a2, &b1, &b2, &intersection));
    ASSERT_NEAR(intersection.x, 5.0, EPSILON);
    ASSERT_NEAR(intersection.y, 5.0, EPSILON);
}

/* ============================================================================
 * Spatial Intersects Tests
 * ============================================================================ */

TEST(spatial_intersects_points) {
    SpatialObject a, b;
    spatial_object_init_point(&a, 1, (Point){5, 5});
    spatial_object_init_point(&b, 2, (Point){5, 5});  // Same point
    
    ASSERT(spatial_intersects(&a, &b));
    
    b.geom.point = (Point){10, 10};
    spatial_object_update_derived(&b);
    ASSERT(!spatial_intersects(&a, &b));
    
    spatial_object_free(&a);
    spatial_object_free(&b);
}

TEST(spatial_intersects_point_polygon) {
    SpatialObject point_obj, poly_obj;
    spatial_object_init_point(&point_obj, 1, (Point){5, 5});
    spatial_object_init_polygon(&poly_obj, 2, 5);
    
    polygon_add_exterior_point(&poly_obj.geom.polygon, (Point){0, 0});
    polygon_add_exterior_point(&poly_obj.geom.polygon, (Point){10, 0});
    polygon_add_exterior_point(&poly_obj.geom.polygon, (Point){10, 10});
    polygon_add_exterior_point(&poly_obj.geom.polygon, (Point){0, 10});
    polygon_add_exterior_point(&poly_obj.geom.polygon, (Point){0, 0});
    spatial_object_update_derived(&poly_obj);
    
    ASSERT(spatial_intersects(&point_obj, &poly_obj));
    
    point_obj.geom.point = (Point){15, 15};
    spatial_object_update_derived(&point_obj);
    ASSERT(!spatial_intersects(&point_obj, &poly_obj));
    
    spatial_object_free(&point_obj);
    spatial_object_free(&poly_obj);
}

/* ============================================================================
 * Convex Hull Tests
 * ============================================================================ */

TEST(convex_hull_square) {
    Point points[] = {
        {0, 0}, {10, 0}, {10, 10}, {0, 10},
        {5, 5}  // Interior point
    };
    
    size_t hull_count;
    Point *hull = convex_hull(points, 5, &hull_count);
    
    ASSERT(hull != NULL);
    ASSERT(hull_count == 5);  // Square has 4 vertices + closure
    
    free(hull);
}

TEST(convex_hull_triangle) {
    Point points[] = {
        {0, 0}, {10, 0}, {5, 10},
        {5, 3}, {4, 4}  // Interior points
    };
    
    size_t hull_count;
    Point *hull = convex_hull(points, 5, &hull_count);
    
    ASSERT(hull != NULL);
    ASSERT(hull_count >= 3);
    
    free(hull);
}

/* ============================================================================
 * Grid Aggregation Tests
 * ============================================================================ */

TEST(grid_aggregation_create) {
    MBR bounds = {0, 0, 100, 100};
    double cell_size = 10;
    
    GridAggregation *grid = grid_aggregation_create(&bounds, cell_size);
    
    ASSERT(grid != NULL);
    ASSERT(grid->rows == 10);
    ASSERT(grid->cols == 10);
    ASSERT_NEAR(grid->cell_width, 10.0, EPSILON);
    ASSERT_NEAR(grid->cell_height, 10.0, EPSILON);
    
    grid_aggregation_free(grid);
}

TEST(grid_aggregation_add_count) {
    MBR bounds = {0, 0, 100, 100};
    GridAggregation *grid = grid_aggregation_create(&bounds, 10);
    
    // Create object in cell (0, 0)
    SpatialObject obj;
    spatial_object_init_point(&obj, 1, (Point){5, 5});
    
    grid_aggregation_add(grid, &obj, 1.0, AGG_COUNT);
    
    // Check cell (0, 0)
    GridCell *cell = grid_aggregation_get_cell(grid, 0, 0);
    ASSERT(cell != NULL);
    ASSERT(cell->count == 1);
    
    // Add another in same cell
    grid_aggregation_add(grid, &obj, 1.0, AGG_COUNT);
    ASSERT(cell->count == 2);
    
    spatial_object_free(&obj);
    grid_aggregation_free(grid);
}

TEST(grid_aggregation_get_cell_at) {
    MBR bounds = {0, 0, 100, 100};
    GridAggregation *grid = grid_aggregation_create(&bounds, 10);
    
    // Get cell at point (55, 35)
    GridCell *cell = grid_aggregation_get_cell_at(grid, 55, 35);
    ASSERT(cell != NULL);
    
    // Should be cell (3, 5) - row 3, col 5
    GridCell *expected = grid_aggregation_get_cell(grid, 3, 5);
    ASSERT(cell == expected);
    
    // Point outside bounds should return NULL
    cell = grid_aggregation_get_cell_at(grid, 150, 50);
    ASSERT(cell == NULL);
    
    grid_aggregation_free(grid);
}

/* ============================================================================
 * Join Result Tests
 * ============================================================================ */

TEST(join_result_create_add) {
    SpatialJoinResult *result = join_result_create(10);
    
    ASSERT(result != NULL);
    ASSERT(result->count == 0);
    ASSERT(result->capacity == 10);
    
    // Add pairs
    ASSERT(join_result_add(result, 1, 2, 5.0) == 0);
    ASSERT(join_result_add(result, 3, 4, 10.0) == 0);
    
    ASSERT(result->count == 2);
    ASSERT(result->pairs[0].id_a == 1);
    ASSERT(result->pairs[0].id_b == 2);
    ASSERT_NEAR(result->pairs[0].distance, 5.0, EPSILON);
    
    join_result_free(result);
}

TEST(join_result_grow) {
    SpatialJoinResult *result = join_result_create(2);
    
    // Add more than initial capacity
    for (int i = 0; i < 10; i++) {
        ASSERT(join_result_add(result, i, i + 100, i * 1.5) == 0);
    }
    
    ASSERT(result->count == 10);
    ASSERT(result->capacity >= 10);
    
    join_result_free(result);
}

/* ============================================================================
 * Delaunay Tests
 * ============================================================================ */

TEST(delaunay_simple) {
    Point points[] = {
        {0, 0}, {10, 0}, {5, 10}, {5, 5}
    };
    
    DelaunayTriangulation *tri = delaunay_create(points, 4);
    
    ASSERT(tri != NULL);
    ASSERT(tri->count > 0);
    ASSERT(tri->num_points == 4);
    
    delaunay_free(tri);
}

TEST(delaunay_collinear) {
    // Collinear points should still work
    Point points[] = {
        {0, 0}, {5, 0}, {10, 0}, {5, 5}
    };
    
    DelaunayTriangulation *tri = delaunay_create(points, 4);
    
    ASSERT(tri != NULL);
    
    delaunay_free(tri);
}

/* ============================================================================
 * Voronoi Tests
 * ============================================================================ */

TEST(voronoi_simple) {
    Point points[] = {
        {0, 0}, {10, 0}, {5, 10}
    };
    
    MBR bounds = {-5, -5, 15, 15};
    VoronoiDiagram *vor = voronoi_create(points, NULL, 3, &bounds);
    
    ASSERT(vor != NULL);
    ASSERT(vor->count == 3);
    
    voronoi_free(vor);
}

TEST(voronoi_find_cell) {
    Point points[] = {
        {0, 0}, {10, 0}, {0, 10}, {10, 10}
    };
    
    VoronoiDiagram *vor = voronoi_create(points, NULL, 4, NULL);
    ASSERT(vor != NULL);
    
    // Point near (0, 0) should be in cell 0
    Point test = {1, 1};
    int cell = voronoi_find_cell(vor, &test);
    ASSERT(cell == 0);
    
    // Point near (10, 10) should be in cell 3
    test = (Point){9, 9};
    cell = voronoi_find_cell(vor, &test);
    ASSERT(cell == 3);
    
    voronoi_free(vor);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== Spatial Operations Tests ===\n\n");
    
    printf("Buffer Tests:\n");
    run_test_buffer_point();
    run_test_buffer_point_small_segments();
    run_test_buffer_point_invalid();
    
    printf("\nSpatial Predicate Tests:\n");
    run_test_point_in_polygon_simple();
    run_test_point_in_polygon_complex();
    run_test_segments_intersect_basic();
    run_test_segment_intersection_point_test();
    
    printf("\nSpatial Intersects Tests:\n");
    run_test_spatial_intersects_points();
    run_test_spatial_intersects_point_polygon();
    
    printf("\nConvex Hull Tests:\n");
    run_test_convex_hull_square();
    run_test_convex_hull_triangle();
    
    printf("\nGrid Aggregation Tests:\n");
    run_test_grid_aggregation_create();
    run_test_grid_aggregation_add_count();
    run_test_grid_aggregation_get_cell_at();
    
    printf("\nJoin Result Tests:\n");
    run_test_join_result_create_add();
    run_test_join_result_grow();
    
    printf("\nDelaunay Tests:\n");
    run_test_delaunay_simple();
    run_test_delaunay_collinear();
    
    printf("\nVoronoi Tests:\n");
    run_test_voronoi_simple();
    run_test_voronoi_find_cell();
    
    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}

