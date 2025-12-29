/**
 * @file streaming.h
 * @brief Real-time streaming layer for Urbis
 * 
 * Provides real-time location tracking, geofencing, proximity alerts,
 * and trajectory analysis for live GPS/IoT data feeds.
 */

#ifndef URBIS_STREAMING_H
#define URBIS_STREAMING_H

#include "geometry.h"
#include "spatial_index.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define STREAM_DEFAULT_HISTORY_SIZE 1000    /**< Default position history per object */
#define STREAM_MAX_TRACKED_OBJECTS 100000   /**< Maximum tracked objects */
#define STREAM_MAX_GEOFENCE_ZONES 10000     /**< Maximum geofence zones */
#define STREAM_MAX_PROXIMITY_RULES 10000    /**< Maximum proximity rules */
#define STREAM_EVENT_QUEUE_SIZE 10000       /**< Event queue capacity */
#define STREAM_STOP_SPEED_THRESHOLD 0.5     /**< m/s - below this is considered stopped */
#define STREAM_STOP_TIME_THRESHOLD 60000    /**< ms - minimum time to be considered a stop */

/* ============================================================================
 * Tracked Object
 * ============================================================================ */

/**
 * @brief Object state for real-time tracking
 */
typedef struct {
    uint64_t object_id;          /**< Unique object identifier */
    Point current_position;      /**< Current position */
    Point previous_position;     /**< Previous position */
    double speed;                /**< Current speed in m/s */
    double heading;              /**< Heading in degrees (0-360, north=0) */
    uint64_t timestamp;          /**< Current position timestamp (ms) */
    uint64_t last_update;        /**< System time of last update */
    bool is_moving;              /**< True if object is moving */
    void *user_data;             /**< User-attached data */
} TrackedObject;

/**
 * @brief Hash table entry for tracked objects
 */
typedef struct TrackedObjectEntry {
    TrackedObject object;
    struct TrackedObjectEntry *next;
} TrackedObjectEntry;

/**
 * @brief Hash table for fast object lookup
 */
typedef struct {
    TrackedObjectEntry **buckets;
    size_t bucket_count;
    size_t object_count;
    pthread_rwlock_t lock;
} TrackedObjectTable;

/* ============================================================================
 * Movement History
 * ============================================================================ */

/**
 * @brief Position record with timestamp
 */
typedef struct {
    Point position;
    uint64_t timestamp;
    double speed;
    double heading;
} PositionRecord;

/**
 * @brief Circular buffer for movement history
 */
typedef struct {
    PositionRecord *records;
    size_t capacity;
    size_t count;
    size_t head;             /**< Next write position */
    size_t tail;             /**< Oldest record */
} MovementHistory;

/**
 * @brief Hash table entry for movement histories
 */
typedef struct MovementHistoryEntry {
    uint64_t object_id;
    MovementHistory history;
    struct MovementHistoryEntry *next;
} MovementHistoryEntry;

/**
 * @brief Hash table for movement histories
 */
typedef struct {
    MovementHistoryEntry **buckets;
    size_t bucket_count;
    size_t history_capacity;     /**< Capacity per object */
    pthread_rwlock_t lock;
} MovementHistoryTable;

/* ============================================================================
 * Geofencing
 * ============================================================================ */

/**
 * @brief Geofence zone definition
 */
typedef struct {
    uint64_t zone_id;            /**< Unique zone identifier */
    char name[256];              /**< Zone name */
    Polygon boundary;            /**< Zone boundary polygon */
    MBR mbr;                     /**< Precomputed bounding box */
    bool active;                 /**< Whether zone is active */
    uint64_t dwell_threshold;    /**< ms - time before DWELL event */
    void *user_data;             /**< User-attached data */
} GeofenceZone;

/**
 * @brief Geofence event types
 */
typedef enum {
    GEOFENCE_ENTER = 1,          /**< Object entered zone */
    GEOFENCE_EXIT = 2,           /**< Object exited zone */
    GEOFENCE_DWELL = 3           /**< Object dwelled in zone */
} GeofenceEventType;

/**
 * @brief Geofence event
 */
typedef struct {
    uint64_t event_id;           /**< Unique event ID */
    uint64_t object_id;          /**< Object that triggered event */
    uint64_t zone_id;            /**< Zone involved */
    GeofenceEventType type;      /**< Event type */
    uint64_t timestamp;          /**< When event occurred */
    Point position;              /**< Position when event occurred */
    uint64_t dwell_time;         /**< Dwell time for DWELL events */
} GeofenceEvent;

/**
 * @brief Object's state within a zone
 */
typedef struct {
    uint64_t object_id;
    uint64_t zone_id;
    bool inside;
    uint64_t enter_time;         /**< When object entered */
    bool dwell_fired;            /**< Whether dwell event was fired */
} ZoneState;

/**
 * @brief Geofence callback function
 */
typedef void (*GeofenceCallback)(const GeofenceEvent *event, void *user_data);

/**
 * @brief Geofence engine
 */
typedef struct {
    GeofenceZone *zones;
    size_t zone_count;
    size_t zone_capacity;
    ZoneState *states;           /**< Object-zone state pairs */
    size_t state_count;
    size_t state_capacity;
    GeofenceCallback callback;
    void *callback_data;
    pthread_rwlock_t lock;
    uint64_t next_event_id;
} GeofenceEngine;

/* ============================================================================
 * Proximity Alerts
 * ============================================================================ */

/**
 * @brief Proximity rule definition
 */
typedef struct {
    uint64_t rule_id;            /**< Unique rule identifier */
    uint64_t object_a;           /**< First object (0 = any) */
    uint64_t object_b;           /**< Second object (0 = any) */
    double threshold;            /**< Distance threshold in meters */
    bool one_shot;               /**< Fire only once per proximity event */
    bool active;                 /**< Whether rule is active */
    void *user_data;             /**< User-attached data */
} ProximityRule;

/**
 * @brief Proximity event
 */
typedef struct {
    uint64_t event_id;           /**< Unique event ID */
    uint64_t rule_id;            /**< Rule that triggered event */
    uint64_t object_a;           /**< First object */
    uint64_t object_b;           /**< Second object */
    double distance;             /**< Distance between objects */
    uint64_t timestamp;          /**< When event occurred */
    Point position_a;            /**< Position of object A */
    Point position_b;            /**< Position of object B */
} ProximityEvent;

/**
 * @brief Proximity state for tracking fired events
 */
typedef struct {
    uint64_t rule_id;
    uint64_t object_a;
    uint64_t object_b;
    bool in_proximity;           /**< Currently within threshold */
    uint64_t last_fired;         /**< Last time event fired */
} ProximityState;

/**
 * @brief Proximity callback function
 */
typedef void (*ProximityCallback)(const ProximityEvent *event, void *user_data);

/**
 * @brief Proximity monitor
 */
typedef struct {
    ProximityRule *rules;
    size_t rule_count;
    size_t rule_capacity;
    ProximityState *states;
    size_t state_count;
    size_t state_capacity;
    ProximityCallback callback;
    void *callback_data;
    pthread_rwlock_t lock;
    uint64_t next_event_id;
    uint64_t next_rule_id;
} ProximityMonitor;

/* ============================================================================
 * Trajectory Analysis
 * ============================================================================ */

/**
 * @brief Trajectory statistics
 */
typedef struct {
    uint64_t object_id;          /**< Object this trajectory belongs to */
    double total_distance;       /**< Total distance traveled in meters */
    double avg_speed;            /**< Average speed in m/s */
    double max_speed;            /**< Maximum speed in m/s */
    uint64_t total_time;         /**< Total time in milliseconds */
    uint64_t moving_time;        /**< Time spent moving */
    uint64_t stopped_time;       /**< Time spent stopped */
    Point start_point;           /**< Starting position */
    Point end_point;             /**< Ending position */
    uint64_t start_time;         /**< Start timestamp */
    uint64_t end_time;           /**< End timestamp */
    size_t point_count;          /**< Number of position records */
    size_t stop_count;           /**< Number of stops detected */
} TrajectoryStats;

/**
 * @brief Trajectory segment
 */
typedef struct {
    Point start;                 /**< Segment start point */
    Point end;                   /**< Segment end point */
    uint64_t start_time;         /**< Segment start time */
    uint64_t end_time;           /**< Segment end time */
    double distance;             /**< Segment distance */
    double speed;                /**< Average speed on segment */
    double heading;              /**< Heading on segment */
    bool is_stop;                /**< True if this is a stop segment */
} TrajectorySegment;

/**
 * @brief Full trajectory result
 */
typedef struct {
    TrajectoryStats stats;       /**< Overall statistics */
    TrajectorySegment *segments; /**< Array of segments */
    size_t segment_count;        /**< Number of segments */
    Point *simplified_path;      /**< Simplified path points */
    size_t path_count;           /**< Number of path points */
} Trajectory;

/* ============================================================================
 * Event Queue
 * ============================================================================ */

/**
 * @brief Event types
 */
typedef enum {
    EVENT_GEOFENCE = 1,
    EVENT_PROXIMITY = 2,
    EVENT_TRAJECTORY = 3,
    EVENT_SPEED_ALERT = 4,
    EVENT_STOP_DETECTED = 5,
    EVENT_MOVEMENT_STARTED = 6
} StreamEventType;

/**
 * @brief Generic stream event
 */
typedef struct {
    StreamEventType type;
    uint64_t timestamp;
    union {
        GeofenceEvent geofence;
        ProximityEvent proximity;
        struct {
            uint64_t object_id;
            Point position;
            double speed;
        } movement;
    } data;
} StreamEvent;

/**
 * @brief Thread-safe event queue
 */
typedef struct {
    StreamEvent *events;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} EventQueue;

/* ============================================================================
 * Stream Context
 * ============================================================================ */

/**
 * @brief Main streaming context
 */
typedef struct UrbisStream {
    SpatialIndex *spatial_index;     /**< Associated spatial index */
    TrackedObjectTable objects;      /**< Tracked objects */
    MovementHistoryTable histories;  /**< Movement histories */
    GeofenceEngine geofence;         /**< Geofencing engine */
    ProximityMonitor proximity;      /**< Proximity monitor */
    EventQueue event_queue;          /**< Output event queue */
    bool running;                    /**< Whether stream is active */
    pthread_t processor_thread;      /**< Background processor */
    uint64_t update_count;           /**< Total updates processed */
    uint64_t event_count;            /**< Total events generated */
} UrbisStream;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    STREAM_OK = 0,
    STREAM_ERR_NULL = -1,
    STREAM_ERR_ALLOC = -2,
    STREAM_ERR_NOT_FOUND = -3,
    STREAM_ERR_EXISTS = -4,
    STREAM_ERR_FULL = -5,
    STREAM_ERR_INVALID = -6,
    STREAM_ERR_THREAD = -7
} StreamError;

/* ============================================================================
 * Stream Lifecycle
 * ============================================================================ */

/**
 * @brief Create a new stream context
 * @param idx Associated spatial index (can be NULL)
 * @return New stream context, or NULL on error
 */
UrbisStream* stream_create(SpatialIndex *idx);

/**
 * @brief Destroy a stream context
 */
void stream_destroy(UrbisStream *stream);

/**
 * @brief Start the stream processor
 */
int stream_start(UrbisStream *stream);

/**
 * @brief Stop the stream processor
 */
int stream_stop(UrbisStream *stream);

/* ============================================================================
 * Location Updates
 * ============================================================================ */

/**
 * @brief Update object location
 * @param stream Stream context
 * @param object_id Object identifier
 * @param x Longitude/X coordinate
 * @param y Latitude/Y coordinate
 * @param timestamp Position timestamp in milliseconds
 * @return STREAM_OK on success
 */
int stream_update_location(UrbisStream *stream, uint64_t object_id,
                           double x, double y, uint64_t timestamp);

/**
 * @brief Update object location with metadata
 */
int stream_update_location_ex(UrbisStream *stream, uint64_t object_id,
                              double x, double y, uint64_t timestamp,
                              double speed, double heading);

/**
 * @brief Batch update multiple locations
 */
int stream_update_batch(UrbisStream *stream, const uint64_t *object_ids,
                        const double *x, const double *y,
                        const uint64_t *timestamps, size_t count);

/**
 * @brief Get current state of tracked object
 */
TrackedObject* stream_get_object(UrbisStream *stream, uint64_t object_id);

/**
 * @brief Remove tracked object
 */
int stream_remove_object(UrbisStream *stream, uint64_t object_id);

/**
 * @brief Get all tracked objects in a region
 */
TrackedObject** stream_query_region(UrbisStream *stream, const MBR *region,
                                     size_t *count);

/* ============================================================================
 * Geofence Management
 * ============================================================================ */

/**
 * @brief Add a geofence zone
 */
int stream_geofence_add(UrbisStream *stream, const GeofenceZone *zone);

/**
 * @brief Remove a geofence zone
 */
int stream_geofence_remove(UrbisStream *stream, uint64_t zone_id);

/**
 * @brief Update a geofence zone
 */
int stream_geofence_update(UrbisStream *stream, const GeofenceZone *zone);

/**
 * @brief Get a geofence zone by ID
 */
GeofenceZone* stream_geofence_get(UrbisStream *stream, uint64_t zone_id);

/**
 * @brief List all geofence zones
 */
GeofenceZone** stream_geofence_list(UrbisStream *stream, size_t *count);

/**
 * @brief Set geofence callback
 */
int stream_geofence_set_callback(UrbisStream *stream, GeofenceCallback callback,
                                  void *user_data);

/**
 * @brief Check which zones contain a point
 */
uint64_t* stream_geofence_check_point(UrbisStream *stream, const Point *p,
                                       size_t *count);

/**
 * @brief Get objects currently in a zone
 */
uint64_t* stream_geofence_objects_in_zone(UrbisStream *stream, uint64_t zone_id,
                                           size_t *count);

/* ============================================================================
 * Proximity Management
 * ============================================================================ */

/**
 * @brief Add a proximity rule
 */
int stream_proximity_add_rule(UrbisStream *stream, const ProximityRule *rule);

/**
 * @brief Remove a proximity rule
 */
int stream_proximity_remove_rule(UrbisStream *stream, uint64_t rule_id);

/**
 * @brief Set proximity callback
 */
int stream_proximity_set_callback(UrbisStream *stream, ProximityCallback callback,
                                   void *user_data);

/**
 * @brief Find all objects within distance of a point
 */
uint64_t* stream_proximity_query(UrbisStream *stream, const Point *p,
                                  double distance, size_t *count);

/**
 * @brief Find all objects within distance of another object
 */
uint64_t* stream_proximity_query_object(UrbisStream *stream, uint64_t object_id,
                                         double distance, size_t *count);

/* ============================================================================
 * Trajectory Analysis
 * ============================================================================ */

/**
 * @brief Get trajectory statistics for an object
 */
TrajectoryStats* stream_trajectory_stats(UrbisStream *stream, uint64_t object_id,
                                          uint64_t start_time, uint64_t end_time);

/**
 * @brief Get full trajectory with segments
 */
Trajectory* stream_trajectory_get(UrbisStream *stream, uint64_t object_id,
                                   uint64_t start_time, uint64_t end_time);

/**
 * @brief Get trajectory path as linestring
 */
LineString* stream_trajectory_path(UrbisStream *stream, uint64_t object_id,
                                    uint64_t start_time, uint64_t end_time);

/**
 * @brief Get simplified trajectory path
 * @param tolerance Simplification tolerance (Douglas-Peucker)
 */
LineString* stream_trajectory_simplified(UrbisStream *stream, uint64_t object_id,
                                          uint64_t start_time, uint64_t end_time,
                                          double tolerance);

/**
 * @brief Free trajectory result
 */
void stream_trajectory_free(Trajectory *traj);

/**
 * @brief Free trajectory stats
 */
void stream_trajectory_stats_free(TrajectoryStats *stats);

/* ============================================================================
 * Event Queue Operations
 * ============================================================================ */

/**
 * @brief Poll for next event (non-blocking)
 * @return Event or NULL if queue is empty
 */
StreamEvent* stream_poll_event(UrbisStream *stream);

/**
 * @brief Wait for next event (blocking)
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 */
StreamEvent* stream_wait_event(UrbisStream *stream, uint64_t timeout_ms);

/**
 * @brief Get pending event count
 */
size_t stream_event_count(UrbisStream *stream);

/**
 * @brief Free an event
 */
void stream_event_free(StreamEvent *event);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Stream statistics
 */
typedef struct {
    size_t tracked_objects;
    size_t geofence_zones;
    size_t proximity_rules;
    size_t pending_events;
    uint64_t total_updates;
    uint64_t total_events;
    uint64_t geofence_events;
    uint64_t proximity_events;
} StreamStats;

/**
 * @brief Get stream statistics
 */
void stream_get_stats(UrbisStream *stream, StreamStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_STREAMING_H */

