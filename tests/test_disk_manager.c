/**
 * @file test_disk_manager.c
 * @brief Unit tests for disk manager
 */

#include "../include/disk_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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
 * Disk Manager Tests
 * ============================================================================ */

TEST(disk_manager_init) {
    DiskManager dm;
    DiskManagerConfig config = disk_manager_default_config();
    
    int err = disk_manager_init(&dm, &config);
    assert(err == DM_OK);
    assert(dm.pool.page_count == 0);
    assert(dm.pool.track_count == 0);
    
    disk_manager_free(&dm);
}

TEST(disk_manager_alloc_page) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    Point centroid = point_create(50, 50);
    Page *page = disk_manager_alloc_page(&dm, centroid);
    
    assert(page != NULL);
    assert(page->header.page_id > 0);
    assert(dm.pool.page_count == 1);
    
    disk_manager_free(&dm);
}

TEST(disk_manager_create_track) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    DiskTrack *track = disk_manager_create_track(&dm);
    assert(track != NULL);
    assert(track->track_id > 0);
    assert(dm.pool.track_count == 1);
    
    disk_manager_free(&dm);
}

TEST(disk_manager_best_track) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    /* Create some tracks with pages */
    for (int i = 0; i < 3; i++) {
        Point centroid = point_create(i * 30, i * 30);
        disk_manager_alloc_page(&dm, centroid);
    }
    
    /* Find best track for a new page near (0,0) */
    DiskTrack *best = disk_manager_find_best_track(&dm, point_create(5, 5));
    
    /* Should find a track (the one closest to (0,0)) */
    assert(best != NULL);
    
    disk_manager_free(&dm);
}

TEST(disk_manager_file_create) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    const char *test_file = "/tmp/urbis_test.dat";
    
    int err = disk_manager_create(&dm, test_file);
    assert(err == DM_OK);
    assert(dm.is_open);
    
    /* Add some data */
    Page *page = disk_manager_alloc_page(&dm, point_create(10, 10));
    assert(page != NULL);
    
    /* Sync to disk */
    err = disk_manager_sync(&dm);
    assert(err == DM_OK);
    
    disk_manager_close(&dm);
    disk_manager_free(&dm);
    
    /* Clean up */
    unlink(test_file);
}

TEST(disk_manager_file_open) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    const char *test_file = "/tmp/urbis_test2.dat";
    
    /* Create file with data */
    int err = disk_manager_create(&dm, test_file);
    assert(err == DM_OK);
    
    Page *page = disk_manager_alloc_page(&dm, point_create(10, 10));
    assert(page != NULL);
    
    disk_manager_sync(&dm);
    disk_manager_close(&dm);
    disk_manager_free(&dm);
    
    /* Reopen file */
    disk_manager_init(&dm, NULL);
    err = disk_manager_open(&dm, test_file);
    assert(err == DM_OK);
    assert(dm.is_open);
    
    disk_manager_close(&dm);
    disk_manager_free(&dm);
    
    /* Clean up */
    unlink(test_file);
}

TEST(disk_manager_query_region) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    /* Create pages with spatial distribution */
    for (int i = 0; i < 5; i++) {
        Point centroid = point_create(i * 20, i * 20);
        Page *page = disk_manager_alloc_page(&dm, centroid);
        
        /* Add an object to set the extent */
        SpatialObject obj;
        spatial_object_init_point(&obj, i + 1, centroid);
        page_add_object(page, &obj);
        page_update_derived(page);
        spatial_object_free(&obj);
    }
    
    /* Query a region */
    MBR region = mbr_create(0, 0, 50, 50);
    Page **pages = NULL;
    size_t count = 0;
    
    int err = disk_manager_query_region(&dm, &region, &pages, &count);
    assert(err == DM_OK);
    assert(count > 0);
    
    free(pages);
    disk_manager_free(&dm);
}

TEST(disk_manager_stats) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    /* Create some pages */
    for (int i = 0; i < 3; i++) {
        disk_manager_alloc_page(&dm, point_create(i * 10, i * 10));
    }
    
    IOStats stats;
    disk_manager_get_stats(&dm, &stats);
    
    /* Stats should be zero since we haven't done file I/O */
    /* (only in-memory operations) */
    
    disk_manager_reset_stats(&dm);
    disk_manager_get_stats(&dm, &stats);
    assert(stats.pages_read == 0);
    assert(stats.pages_written == 0);
    
    disk_manager_free(&dm);
}

TEST(disk_manager_estimate_seeks) {
    DiskManager dm;
    disk_manager_init(&dm, NULL);
    
    /* Create pages in different tracks */
    Page *p1 = disk_manager_alloc_page(&dm, point_create(0, 0));
    Page *p2 = disk_manager_alloc_page(&dm, point_create(100, 100));
    Page *p3 = disk_manager_alloc_page(&dm, point_create(200, 200));
    
    uint32_t page_ids[] = { p1->header.page_id, p2->header.page_id, p3->header.page_id };
    
    uint64_t seeks = disk_manager_estimate_seeks(&dm, page_ids, 3);
    
    /* May or may not have seeks depending on track allocation */
    /* Just ensure it doesn't crash */
    (void)seeks;
    
    disk_manager_free(&dm);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running disk manager tests...\n\n");
    
    RUN_TEST(disk_manager_init);
    RUN_TEST(disk_manager_alloc_page);
    RUN_TEST(disk_manager_create_track);
    RUN_TEST(disk_manager_best_track);
    RUN_TEST(disk_manager_file_create);
    RUN_TEST(disk_manager_file_open);
    RUN_TEST(disk_manager_query_region);
    RUN_TEST(disk_manager_stats);
    RUN_TEST(disk_manager_estimate_seeks);
    
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}

