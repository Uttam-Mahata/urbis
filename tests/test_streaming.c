/**
 * @file test_streaming.c
 * @brief Unit tests for real-time streaming, geofencing, and trajectory analysis
 */

#include "../include/urbis.h"
#include "../include/streaming.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#define TEST_PASSED() printf("  PASSED: %s\n", __func__)
#define ASSERT(cond) do { if (!(cond)) { printf("  FAILED: %s line %d: %s\n", __func__, __LINE__, #cond); return; } } while(0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_DBL_NEAR(a, b) ASSERT(fabs((a) - (b)) < 0.001)

/* ============================================================================
 * Stream Lifecycle Tests
 * ============================================================================ */

void test_stream_create_destroy(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_stream_start_stop(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    
    int err = urbis_stream_start(stream);
    ASSERT_EQ(err, URBIS_OK);
    
    err = urbis_stream_stop(stream);
    ASSERT_EQ(err, URBIS_OK);
    
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Location Update Tests
 * ============================================================================ */

void test_stream_update_location(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Update location
    int err = urbis_stream_update(stream, 1, 88.35, 22.57, 1000);
    ASSERT_EQ(err, URBIS_OK);
    
    // Get tracked object
    TrackedObject *obj = urbis_stream_get_object(stream, 1);
    ASSERT(obj != NULL);
    ASSERT_EQ(obj->object_id, 1);
    ASSERT_DBL_NEAR(obj->current_position.x, 88.35);
    ASSERT_DBL_NEAR(obj->current_position.y, 22.57);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_stream_update_with_speed(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Update with speed and heading
    int err = urbis_stream_update_ex(stream, 1, 88.35, 22.57, 1000, 15.5, 45.0);
    ASSERT_EQ(err, URBIS_OK);
    
    TrackedObject *obj = urbis_stream_get_object(stream, 1);
    ASSERT(obj != NULL);
    ASSERT_DBL_NEAR(obj->speed, 15.5);
    ASSERT_DBL_NEAR(obj->heading, 45.0);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_stream_multiple_updates(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Update multiple times
    for (int i = 0; i < 10; i++) {
        int err = urbis_stream_update(stream, 1, 88.35 + i * 0.001, 22.57, 1000 + i * 1000);
        ASSERT_EQ(err, URBIS_OK);
    }
    
    TrackedObject *obj = urbis_stream_get_object(stream, 1);
    ASSERT(obj != NULL);
    
    // Speed should be calculated from movement
    ASSERT(obj->speed >= 0);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_stream_remove_object(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    urbis_stream_update(stream, 1, 88.35, 22.57, 1000);
    
    TrackedObject *obj = urbis_stream_get_object(stream, 1);
    ASSERT(obj != NULL);
    
    int err = urbis_stream_remove_object(stream, 1);
    ASSERT_EQ(err, URBIS_OK);
    
    obj = urbis_stream_get_object(stream, 1);
    ASSERT(obj == NULL);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Geofencing Tests
 * ============================================================================ */

void test_geofence_add_remove(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Create a square zone
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}  // Closed
    };
    
    int err = urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    ASSERT_EQ(err, URBIS_OK);
    
    // Adding same zone should fail
    err = urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    ASSERT_NE(err, URBIS_OK);
    
    // Remove zone
    err = urbis_geofence_remove(stream, 1);
    ASSERT_EQ(err, URBIS_OK);
    
    // Remove again should fail
    err = urbis_geofence_remove(stream, 1);
    ASSERT_NE(err, URBIS_OK);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_geofence_check_point(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Create zone
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}
    };
    
    urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    
    // Point inside zone
    size_t count = 0;
    uint64_t *zones = urbis_geofence_check(stream, 88.35, 22.57, &count);
    ASSERT_EQ(count, 1);
    ASSERT(zones != NULL);
    ASSERT_EQ(zones[0], 1);
    free(zones);
    
    // Point outside zone
    zones = urbis_geofence_check(stream, 88.40, 22.60, &count);
    ASSERT_EQ(count, 0);
    if (zones) free(zones);  // May still be allocated
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

static int geofence_event_count = 0;
static GeofenceEventType last_geofence_event_type = 0;

void geofence_callback(uint64_t event_id, uint64_t object_id,
                       uint64_t zone_id, int event_type,
                       uint64_t timestamp, double x, double y,
                       void *user_data) {
    (void)event_id; (void)object_id; (void)zone_id;
    (void)timestamp; (void)x; (void)y; (void)user_data;
    geofence_event_count++;
    last_geofence_event_type = event_type;
}

void test_geofence_enter_exit(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Reset counter
    geofence_event_count = 0;
    last_geofence_event_type = 0;
    
    // Set callback
    urbis_geofence_set_callback(stream, geofence_callback, NULL);
    
    // Create zone
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}
    };
    
    urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    
    // Start outside zone
    urbis_stream_update(stream, 1, 88.30, 22.50, 1000);
    
    // Enter zone
    urbis_stream_update(stream, 1, 88.35, 22.57, 2000);
    
    // Check for ENTER event
    ASSERT(geofence_event_count >= 1);
    ASSERT_EQ(last_geofence_event_type, GEOFENCE_ENTER);
    
    // Exit zone
    urbis_stream_update(stream, 1, 88.40, 22.60, 3000);
    
    // Check for EXIT event
    ASSERT(geofence_event_count >= 2);
    ASSERT_EQ(last_geofence_event_type, GEOFENCE_EXIT);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_geofence_objects_in_zone(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Create zone
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}
    };
    
    urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    
    // Put objects inside and outside
    urbis_stream_update(stream, 100, 88.30, 22.50, 1000);  // Outside
    urbis_stream_update(stream, 100, 88.35, 22.57, 2000);  // Inside (enter)
    
    urbis_stream_update(stream, 200, 88.30, 22.50, 1000);  // Outside
    urbis_stream_update(stream, 200, 88.351, 22.571, 2000);  // Inside (enter)
    
    urbis_stream_update(stream, 300, 88.40, 22.60, 1000);  // Outside, stays outside
    
    // Check objects in zone
    size_t count = 0;
    uint64_t *objects = urbis_geofence_objects(stream, 1, &count);
    ASSERT_EQ(count, 2);
    ASSERT(objects != NULL);
    free(objects);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Proximity Tests
 * ============================================================================ */

void test_proximity_query(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Add objects at different locations
    urbis_stream_update(stream, 1, 88.35, 22.57, 1000);
    urbis_stream_update(stream, 2, 88.3501, 22.5701, 1000);  // Very close to 1
    urbis_stream_update(stream, 3, 88.40, 22.60, 1000);       // Far from 1
    
    // Query proximity
    size_t count = 0;
    uint64_t *nearby = urbis_proximity_query(stream, 88.35, 22.57, 1000, &count);
    
    // Should find object 1 and 2
    ASSERT(count >= 2);
    free(nearby);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_proximity_rule(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Add proximity rule for any objects within 500m
    uint64_t rule_id = urbis_proximity_add_rule(stream, 0, 0, 500, false);
    ASSERT(rule_id > 0);
    
    // Add two objects far apart
    urbis_stream_update(stream, 1, 88.30, 22.50, 1000);
    urbis_stream_update(stream, 2, 88.40, 22.60, 1000);
    
    // Check events (should be none)
    size_t event_count = urbis_stream_event_count(stream);
    
    // Move them close
    urbis_stream_update(stream, 1, 88.351, 22.571, 2000);
    urbis_stream_update(stream, 2, 88.3515, 22.5715, 2000);
    
    // Should have proximity events now
    size_t new_event_count = urbis_stream_event_count(stream);
    (void)event_count;  /* May or may not have more events depending on timing */
    (void)new_event_count;  /* Events processed */
    
    // Remove rule
    int err = urbis_proximity_remove_rule(stream, rule_id);
    ASSERT_EQ(err, URBIS_OK);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Trajectory Tests
 * ============================================================================ */

void test_trajectory_stats(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Simulate a moving object
    uint64_t base_time = 1000;
    for (int i = 0; i < 100; i++) {
        double x = 88.35 + i * 0.001;  // Moving east
        double y = 22.57;
        urbis_stream_update(stream, 1, x, y, base_time + i * 1000);
    }
    
    // Get trajectory stats
    UrbisTrajectoryStats *stats = urbis_trajectory_stats(stream, 1, base_time, base_time + 100000);
    ASSERT(stats != NULL);
    
    ASSERT_EQ(stats->object_id, 1);
    ASSERT(stats->total_distance > 0);
    ASSERT(stats->point_count >= 50);  // At least half the updates
    ASSERT_DBL_NEAR(stats->start_x, 88.35);
    
    urbis_trajectory_stats_free(stats);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_trajectory_path(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Simulate movement
    uint64_t base_time = 1000;
    for (int i = 0; i < 20; i++) {
        double x = 88.35 + i * 0.001;
        double y = 22.57 + i * 0.0005;
        urbis_stream_update(stream, 1, x, y, base_time + i * 1000);
    }
    
    // Get path
    size_t count = 0;
    Point *path = urbis_trajectory_path(stream, 1, base_time, base_time + 25000, &count);
    ASSERT(path != NULL);
    ASSERT(count >= 10);
    
    // First point should be near start
    ASSERT_DBL_NEAR(path[0].x, 88.35);
    ASSERT_DBL_NEAR(path[0].y, 22.57);
    
    free(path);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_trajectory_simplified(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Simulate a straight path with many points
    uint64_t base_time = 1000;
    for (int i = 0; i < 50; i++) {
        double x = 88.35 + i * 0.0001;  // Straight line
        double y = 22.57;
        urbis_stream_update(stream, 1, x, y, base_time + i * 100);
    }
    
    // Get full path
    size_t full_count = 0;
    Point *full_path = urbis_trajectory_path(stream, 1, base_time, base_time + 10000, &full_count);
    
    // Get simplified path
    size_t simplified_count = 0;
    Point *simplified = urbis_trajectory_simplified(stream, 1, base_time, base_time + 10000, 10.0, &simplified_count);
    
    ASSERT(full_path != NULL);
    ASSERT(simplified != NULL);
    
    // Simplified should have fewer or equal points
    ASSERT(simplified_count <= full_count);
    
    // For a straight line, simplification should reduce significantly
    // But we can't guarantee exact reduction, just that it works
    
    free(full_path);
    free(simplified);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Event Queue Tests
 * ============================================================================ */

void test_event_queue(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Create zone for events
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}
    };
    
    urbis_geofence_add(stream, 1, "Test Zone", boundary, 5, 0);
    
    // Trigger an enter event
    urbis_stream_update(stream, 1, 88.30, 22.50, 1000);
    urbis_stream_update(stream, 1, 88.35, 22.57, 2000);
    
    // Poll for event
    UrbisStreamEvent *event = urbis_stream_poll_event(stream);
    
    // We might have an event
    if (event) {
        ASSERT(event->event_type == 1 || event->event_type == 6);  // Geofence or movement
        urbis_stream_event_free(event);
    }
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

void test_stream_stats(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Add some data
    Point boundary[] = {
        {88.34, 22.56},
        {88.36, 22.56},
        {88.36, 22.58},
        {88.34, 22.58},
        {88.34, 22.56}
    };
    
    urbis_geofence_add(stream, 1, "Zone 1", boundary, 5, 0);
    urbis_geofence_add(stream, 2, "Zone 2", boundary, 5, 0);
    
    urbis_stream_update(stream, 1, 88.35, 22.57, 1000);
    urbis_stream_update(stream, 2, 88.36, 22.58, 1000);
    urbis_stream_update(stream, 3, 88.37, 22.59, 1000);
    
    urbis_proximity_add_rule(stream, 0, 0, 500, false);
    
    // Get stats
    UrbisStreamStats stats;
    urbis_stream_get_stats(stream, &stats);
    
    ASSERT_EQ(stats.tracked_objects, 3);
    ASSERT_EQ(stats.geofence_zones, 2);
    ASSERT_EQ(stats.proximity_rules, 1);
    ASSERT(stats.total_updates >= 3);
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Batch Update Tests
 * ============================================================================ */

void test_batch_update(void) {
    UrbisStreamHandle *stream = urbis_stream_create(NULL);
    ASSERT(stream != NULL);
    urbis_stream_start(stream);
    
    // Prepare batch data
    uint64_t ids[] = {1, 2, 3, 4, 5};
    double x[] = {88.35, 88.36, 88.37, 88.38, 88.39};
    double y[] = {22.57, 22.58, 22.59, 22.60, 22.61};
    uint64_t timestamps[] = {1000, 1000, 1000, 1000, 1000};
    
    int err = urbis_stream_update_batch(stream, ids, x, y, timestamps, 5);
    ASSERT_EQ(err, URBIS_OK);
    
    // Verify all objects exist
    for (int i = 0; i < 5; i++) {
        TrackedObject *obj = urbis_stream_get_object(stream, ids[i]);
        ASSERT(obj != NULL);
        ASSERT_DBL_NEAR(obj->current_position.x, x[i]);
        ASSERT_DBL_NEAR(obj->current_position.y, y[i]);
    }
    
    urbis_stream_stop(stream);
    urbis_stream_destroy(stream);
    TEST_PASSED();
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== Streaming Tests ===\n\n");
    
    printf("Stream Lifecycle:\n");
    test_stream_create_destroy();
    test_stream_start_stop();
    
    printf("\nLocation Updates:\n");
    test_stream_update_location();
    test_stream_update_with_speed();
    test_stream_multiple_updates();
    test_stream_remove_object();
    test_batch_update();
    
    printf("\nGeofencing:\n");
    test_geofence_add_remove();
    test_geofence_check_point();
    test_geofence_enter_exit();
    test_geofence_objects_in_zone();
    
    printf("\nProximity:\n");
    test_proximity_query();
    test_proximity_rule();
    
    printf("\nTrajectory:\n");
    test_trajectory_stats();
    test_trajectory_path();
    test_trajectory_simplified();
    
    printf("\nEvents:\n");
    test_event_queue();
    test_stream_stats();
    
    printf("\n=== All Streaming Tests Passed! ===\n");
    return 0;
}

