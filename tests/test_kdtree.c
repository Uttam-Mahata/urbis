/**
 * @file test_kdtree.c
 * @brief Unit tests for KD-tree implementation
 */

#include "../include/kdtree.h"
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
 * KD-Tree Tests
 * ============================================================================ */

TEST(kdtree_init) {
    KDTree tree;
    int err = kdtree_init(&tree);
    assert(err == KD_OK);
    assert(tree.root == NULL);
    assert(tree.size == 0);
    kdtree_free(&tree);
}

TEST(kdtree_insert) {
    KDTree tree;
    kdtree_init(&tree);
    
    int err = kdtree_insert(&tree, point_create(5, 5), 1, NULL);
    assert(err == KD_OK);
    assert(tree.size == 1);
    
    err = kdtree_insert(&tree, point_create(2, 3), 2, NULL);
    assert(err == KD_OK);
    assert(tree.size == 2);
    
    err = kdtree_insert(&tree, point_create(8, 7), 3, NULL);
    assert(err == KD_OK);
    assert(tree.size == 3);
    
    kdtree_free(&tree);
}

TEST(kdtree_bulk_load) {
    KDTree tree;
    kdtree_init(&tree);
    
    KDPointData points[] = {
        { point_create(5, 5), 1, NULL },
        { point_create(2, 3), 2, NULL },
        { point_create(8, 7), 3, NULL },
        { point_create(1, 1), 4, NULL },
        { point_create(9, 9), 5, NULL },
        { point_create(4, 6), 6, NULL },
        { point_create(7, 2), 7, NULL },
    };
    
    int err = kdtree_bulk_load(&tree, points, 7);
    assert(err == KD_OK);
    assert(tree.size == 7);
    
    /* Bulk loaded tree should be more balanced */
    assert(kdtree_is_balanced(&tree));
    
    kdtree_free(&tree);
}

TEST(kdtree_nearest) {
    KDTree tree;
    kdtree_init(&tree);
    
    kdtree_insert(&tree, point_create(5, 5), 1, NULL);
    kdtree_insert(&tree, point_create(2, 3), 2, NULL);
    kdtree_insert(&tree, point_create(8, 7), 3, NULL);
    kdtree_insert(&tree, point_create(1, 1), 4, NULL);
    kdtree_insert(&tree, point_create(9, 9), 5, NULL);
    
    Point query = point_create(2.5, 3.5);
    Point nearest;
    uint64_t id;
    
    int err = kdtree_nearest(&tree, query, &nearest, &id, NULL);
    assert(err == KD_OK);
    assert(id == 2);  /* Point (2,3) is nearest to (2.5, 3.5) */
    ASSERT_NEAR(nearest.x, 2);
    ASSERT_NEAR(nearest.y, 3);
    
    kdtree_free(&tree);
}

TEST(kdtree_range_query) {
    KDTree tree;
    kdtree_init(&tree);
    
    kdtree_insert(&tree, point_create(5, 5), 1, NULL);
    kdtree_insert(&tree, point_create(2, 3), 2, NULL);
    kdtree_insert(&tree, point_create(8, 7), 3, NULL);
    kdtree_insert(&tree, point_create(1, 1), 4, NULL);
    kdtree_insert(&tree, point_create(9, 9), 5, NULL);
    
    KDQueryResult result;
    kdresult_init(&result, 16);
    
    MBR range = mbr_create(0, 0, 6, 6);
    int err = kdtree_range_query(&tree, &range, &result);
    assert(err == KD_OK);
    
    /* Points in range: (5,5), (2,3), (1,1) */
    assert(result.count == 3);
    
    kdresult_free(&result);
    kdtree_free(&tree);
}

TEST(kdtree_radius_query) {
    KDTree tree;
    kdtree_init(&tree);
    
    kdtree_insert(&tree, point_create(0, 0), 1, NULL);
    kdtree_insert(&tree, point_create(1, 0), 2, NULL);
    kdtree_insert(&tree, point_create(0, 1), 3, NULL);
    kdtree_insert(&tree, point_create(10, 10), 4, NULL);
    
    KDQueryResult result;
    kdresult_init(&result, 16);
    
    int err = kdtree_radius_query(&tree, point_create(0, 0), 1.5, &result);
    assert(err == KD_OK);
    
    /* Points within radius 1.5 of origin: (0,0), (1,0), (0,1) */
    assert(result.count == 3);
    
    kdresult_free(&result);
    kdtree_free(&tree);
}

TEST(kdtree_k_nearest) {
    KDTree tree;
    kdtree_init(&tree);
    
    kdtree_insert(&tree, point_create(0, 0), 1, NULL);
    kdtree_insert(&tree, point_create(1, 1), 2, NULL);
    kdtree_insert(&tree, point_create(2, 2), 3, NULL);
    kdtree_insert(&tree, point_create(10, 10), 4, NULL);
    kdtree_insert(&tree, point_create(20, 20), 5, NULL);
    
    KDQueryResult result;
    kdresult_init(&result, 16);
    
    int err = kdtree_k_nearest(&tree, point_create(0.5, 0.5), 2, &result);
    assert(err == KD_OK);
    
    /* 2 nearest to (0.5, 0.5) are (0,0) and (1,1) */
    assert(result.count == 2);
    
    kdresult_free(&result);
    kdtree_free(&tree);
}

TEST(kdtree_partition) {
    KDTree tree;
    kdtree_init(&tree);
    
    /* Create points in a grid */
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            kdtree_insert(&tree, point_create(i, j), i * 10 + j, NULL);
        }
    }
    
    MBR *block_bounds = NULL;
    size_t block_count = 0;
    
    int err = kdtree_partition(&tree, 25, &block_count, &block_bounds);
    assert(err == KD_OK);
    assert(block_count >= 4);  /* At least 100/25 = 4 blocks */
    
    free(block_bounds);
    kdtree_free(&tree);
}

TEST(kdtree_depth) {
    KDTree tree;
    kdtree_init(&tree);
    
    assert(kdtree_depth(&tree) == 0);
    
    kdtree_insert(&tree, point_create(5, 5), 1, NULL);
    assert(kdtree_depth(&tree) == 1);
    
    kdtree_insert(&tree, point_create(2, 3), 2, NULL);
    kdtree_insert(&tree, point_create(8, 7), 3, NULL);
    assert(kdtree_depth(&tree) >= 2);
    
    kdtree_free(&tree);
}

TEST(kdresult_operations) {
    KDQueryResult result;
    int err = kdresult_init(&result, 4);
    assert(err == KD_OK);
    
    kdresult_add(&result, point_create(1, 1), 1, NULL);
    kdresult_add(&result, point_create(2, 2), 2, NULL);
    assert(result.count == 2);
    
    kdresult_clear(&result);
    assert(result.count == 0);
    assert(result.capacity >= 4);
    
    kdresult_free(&result);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running KD-tree tests...\n\n");
    
    RUN_TEST(kdtree_init);
    RUN_TEST(kdtree_insert);
    RUN_TEST(kdtree_bulk_load);
    RUN_TEST(kdtree_nearest);
    RUN_TEST(kdtree_range_query);
    RUN_TEST(kdtree_radius_query);
    RUN_TEST(kdtree_k_nearest);
    RUN_TEST(kdtree_partition);
    RUN_TEST(kdtree_depth);
    RUN_TEST(kdresult_operations);
    
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}

