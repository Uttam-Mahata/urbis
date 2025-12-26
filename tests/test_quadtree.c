/**
 * @file test_quadtree.c
 * @brief Unit tests for quadtree implementation
 */

#include "../include/quadtree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

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
 * Quadtree Tests
 * ============================================================================ */

TEST(quadtree_create) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    assert(qt != NULL);
    assert(qt->total_items == 0);
    assert(qt->root != NULL);
    
    quadtree_destroy(qt);
}

TEST(quadtree_insert) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    MBR item1 = mbr_create(10, 10, 20, 20);
    int err = quadtree_insert(qt, 1, item1, NULL);
    assert(err == QT_OK);
    assert(qt->total_items == 1);
    
    MBR item2 = mbr_create(50, 50, 60, 60);
    err = quadtree_insert(qt, 2, item2, NULL);
    assert(err == QT_OK);
    assert(qt->total_items == 2);
    
    quadtree_destroy(qt);
}

TEST(quadtree_query_range) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    /* Insert items in different quadrants */
    quadtree_insert(qt, 1, mbr_create(10, 10, 20, 20), NULL);
    quadtree_insert(qt, 2, mbr_create(60, 10, 70, 20), NULL);
    quadtree_insert(qt, 3, mbr_create(10, 60, 20, 70), NULL);
    quadtree_insert(qt, 4, mbr_create(60, 60, 70, 70), NULL);
    
    QTQueryResult result;
    qtresult_init(&result, 16);
    
    /* Query lower-left quadrant */
    MBR query = mbr_create(0, 0, 50, 50);
    int err = quadtree_query_range(qt, &query, &result);
    assert(err == QT_OK);
    assert(result.count == 1);  /* Only item 1 */
    
    qtresult_clear(&result);
    
    /* Query upper half */
    query = mbr_create(0, 50, 100, 100);
    err = quadtree_query_range(qt, &query, &result);
    assert(err == QT_OK);
    assert(result.count == 2);  /* Items 3 and 4 */
    
    qtresult_free(&result);
    quadtree_destroy(qt);
}

TEST(quadtree_query_point) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    quadtree_insert(qt, 1, mbr_create(10, 10, 30, 30), NULL);
    quadtree_insert(qt, 2, mbr_create(20, 20, 40, 40), NULL);  /* Overlaps with 1 */
    quadtree_insert(qt, 3, mbr_create(60, 60, 80, 80), NULL);
    
    QTQueryResult result;
    qtresult_init(&result, 16);
    
    /* Query a point that's in both items 1 and 2 */
    int err = quadtree_query_point(qt, point_create(25, 25), &result);
    assert(err == QT_OK);
    assert(result.count == 2);
    
    qtresult_clear(&result);
    
    /* Query a point in item 3 only */
    err = quadtree_query_point(qt, point_create(70, 70), &result);
    assert(err == QT_OK);
    assert(result.count == 1);
    
    qtresult_free(&result);
    quadtree_destroy(qt);
}

TEST(quadtree_find_adjacent) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    /* Insert adjacent items */
    quadtree_insert(qt, 1, mbr_create(10, 10, 30, 30), NULL);
    quadtree_insert(qt, 2, mbr_create(30, 10, 50, 30), NULL);  /* Adjacent to 1 */
    quadtree_insert(qt, 3, mbr_create(60, 60, 80, 80), NULL);  /* Not adjacent */
    
    QTQueryResult result;
    qtresult_init(&result, 16);
    
    int err = quadtree_find_adjacent(qt, 1, &result);
    assert(err == QT_OK);
    /* Should find items 1 (self) and 2 (adjacent) */
    assert(result.count >= 1);
    
    qtresult_free(&result);
    quadtree_destroy(qt);
}

TEST(quadtree_remove) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    quadtree_insert(qt, 1, mbr_create(10, 10, 20, 20), NULL);
    quadtree_insert(qt, 2, mbr_create(50, 50, 60, 60), NULL);
    assert(qt->total_items == 2);
    
    int err = quadtree_remove(qt, 1);
    assert(err == QT_OK);
    assert(qt->total_items == 1);
    
    /* Try to remove non-existent item */
    err = quadtree_remove(qt, 99);
    assert(err == QT_ERR_NOT_FOUND);
    
    quadtree_destroy(qt);
}

TEST(quadtree_get) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    MBR item_bounds = mbr_create(10, 10, 20, 20);
    quadtree_insert(qt, 42, item_bounds, (void *)0xDEADBEEF);
    
    QTItem item;
    int err = quadtree_get(qt, 42, &item);
    assert(err == QT_OK);
    assert(item.id == 42);
    assert(item.data == (void *)0xDEADBEEF);
    
    err = quadtree_get(qt, 99, &item);
    assert(err == QT_ERR_NOT_FOUND);
    
    quadtree_destroy(qt);
}

TEST(quadtree_stats) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 2, 10);  /* Small capacity to force splits */
    
    /* Insert enough items to cause splits */
    for (int i = 0; i < 10; i++) {
        MBR item = mbr_create(i * 10, i * 10, i * 10 + 5, i * 10 + 5);
        quadtree_insert(qt, i + 1, item, NULL);
    }
    
    size_t total_items, total_nodes, max_depth, leaf_count;
    quadtree_stats(qt, &total_items, &total_nodes, &max_depth, &leaf_count);
    
    assert(total_items == 10);
    assert(total_nodes >= 1);
    assert(leaf_count >= 1);
    
    quadtree_destroy(qt);
}

TEST(quadtree_clear) {
    MBR bounds = mbr_create(0, 0, 100, 100);
    QuadTree *qt = quadtree_create(bounds, 4, 10);
    
    quadtree_insert(qt, 1, mbr_create(10, 10, 20, 20), NULL);
    quadtree_insert(qt, 2, mbr_create(50, 50, 60, 60), NULL);
    assert(qt->total_items == 2);
    
    quadtree_clear(qt);
    assert(qt->total_items == 0);
    
    quadtree_destroy(qt);
}

TEST(qtresult_operations) {
    QTQueryResult result;
    int err = qtresult_init(&result, 4);
    assert(err == QT_OK);
    
    QTItem item1 = { .id = 1, .bounds = mbr_create(0, 0, 10, 10) };
    QTItem item2 = { .id = 2, .bounds = mbr_create(20, 20, 30, 30) };
    
    qtresult_add(&result, &item1);
    qtresult_add(&result, &item2);
    assert(result.count == 2);
    
    qtresult_clear(&result);
    assert(result.count == 0);
    
    qtresult_free(&result);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running quadtree tests...\n\n");
    
    RUN_TEST(quadtree_create);
    RUN_TEST(quadtree_insert);
    RUN_TEST(quadtree_query_range);
    RUN_TEST(quadtree_query_point);
    RUN_TEST(quadtree_find_adjacent);
    RUN_TEST(quadtree_remove);
    RUN_TEST(quadtree_get);
    RUN_TEST(quadtree_stats);
    RUN_TEST(quadtree_clear);
    RUN_TEST(qtresult_operations);
    
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}

