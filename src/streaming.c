/**
 * @file streaming.c
 * @brief Real-time streaming implementation - core tracking and event system
 */

#include "../include/streaming.h"
#include "../include/spatial_ops.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Hash Functions
 * ============================================================================ */

static size_t hash_uint64(uint64_t key, size_t bucket_count) {
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;
    return key % bucket_count;
}

/* ============================================================================
 * Tracked Object Table
 * ============================================================================ */

static int tracked_table_init(TrackedObjectTable *table, size_t bucket_count) {
    table->buckets = calloc(bucket_count, sizeof(TrackedObjectEntry *));
    if (!table->buckets) return STREAM_ERR_ALLOC;
    
    table->bucket_count = bucket_count;
    table->object_count = 0;
    pthread_rwlock_init(&table->lock, NULL);
    
    return STREAM_OK;
}

static void tracked_table_free(TrackedObjectTable *table) {
    if (!table) return;
    
    for (size_t i = 0; i < table->bucket_count; i++) {
        TrackedObjectEntry *entry = table->buckets[i];
        while (entry) {
            TrackedObjectEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    free(table->buckets);
    pthread_rwlock_destroy(&table->lock);
}

TrackedObject* tracked_table_get(TrackedObjectTable *table, uint64_t object_id) {
    size_t bucket = hash_uint64(object_id, table->bucket_count);
    TrackedObjectEntry *entry = table->buckets[bucket];
    
    while (entry) {
        if (entry->object.object_id == object_id) {
            return &entry->object;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static TrackedObject* tracked_table_insert(TrackedObjectTable *table, uint64_t object_id) {
    size_t bucket = hash_uint64(object_id, table->bucket_count);
    
    // Check if exists
    TrackedObjectEntry *entry = table->buckets[bucket];
    while (entry) {
        if (entry->object.object_id == object_id) {
            return &entry->object;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = calloc(1, sizeof(TrackedObjectEntry));
    if (!entry) return NULL;
    
    entry->object.object_id = object_id;
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
    table->object_count++;
    
    return &entry->object;
}

static int tracked_table_remove(TrackedObjectTable *table, uint64_t object_id) {
    size_t bucket = hash_uint64(object_id, table->bucket_count);
    
    TrackedObjectEntry *prev = NULL;
    TrackedObjectEntry *entry = table->buckets[bucket];
    
    while (entry) {
        if (entry->object.object_id == object_id) {
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[bucket] = entry->next;
            }
            free(entry);
            table->object_count--;
            return STREAM_OK;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return STREAM_ERR_NOT_FOUND;
}

/* ============================================================================
 * Movement History Table
 * ============================================================================ */

static int history_init(MovementHistory *history, size_t capacity) {
    history->records = malloc(capacity * sizeof(PositionRecord));
    if (!history->records) return STREAM_ERR_ALLOC;
    
    history->capacity = capacity;
    history->count = 0;
    history->head = 0;
    history->tail = 0;
    
    return STREAM_OK;
}

static void history_free(MovementHistory *history) {
    free(history->records);
    history->records = NULL;
}

static void history_add(MovementHistory *history, const PositionRecord *record) {
    history->records[history->head] = *record;
    history->head = (history->head + 1) % history->capacity;
    
    if (history->count < history->capacity) {
        history->count++;
    } else {
        history->tail = (history->tail + 1) % history->capacity;
    }
}

static int history_table_init(MovementHistoryTable *table, size_t bucket_count,
                               size_t history_capacity) {
    table->buckets = calloc(bucket_count, sizeof(MovementHistoryEntry *));
    if (!table->buckets) return STREAM_ERR_ALLOC;
    
    table->bucket_count = bucket_count;
    table->history_capacity = history_capacity;
    pthread_rwlock_init(&table->lock, NULL);
    
    return STREAM_OK;
}

static void history_table_free(MovementHistoryTable *table) {
    if (!table) return;
    
    for (size_t i = 0; i < table->bucket_count; i++) {
        MovementHistoryEntry *entry = table->buckets[i];
        while (entry) {
            MovementHistoryEntry *next = entry->next;
            history_free(&entry->history);
            free(entry);
            entry = next;
        }
    }
    
    free(table->buckets);
    pthread_rwlock_destroy(&table->lock);
}

static MovementHistory* history_table_get_or_create(MovementHistoryTable *table,
                                                     uint64_t object_id) {
    size_t bucket = hash_uint64(object_id, table->bucket_count);
    
    MovementHistoryEntry *entry = table->buckets[bucket];
    while (entry) {
        if (entry->object_id == object_id) {
            return &entry->history;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = calloc(1, sizeof(MovementHistoryEntry));
    if (!entry) return NULL;
    
    entry->object_id = object_id;
    if (history_init(&entry->history, table->history_capacity) != STREAM_OK) {
        free(entry);
        return NULL;
    }
    
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
    
    return &entry->history;
}

/* ============================================================================
 * Event Queue
 * ============================================================================ */

static int event_queue_init(EventQueue *queue, size_t capacity) {
    queue->events = malloc(capacity * sizeof(StreamEvent));
    if (!queue->events) return STREAM_ERR_ALLOC;
    
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    
    return STREAM_OK;
}

static void event_queue_free(EventQueue *queue) {
    free(queue->events);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
}

static int event_queue_push(EventQueue *queue, const StreamEvent *event) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count >= queue->capacity) {
        // Drop oldest event
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
    }
    
    queue->events[queue->head] = *event;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return STREAM_OK;
}

static StreamEvent* event_queue_poll(EventQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    StreamEvent *event = malloc(sizeof(StreamEvent));
    if (event) {
        *event = queue->events[queue->tail];
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
    }
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return event;
}

static StreamEvent* event_queue_wait(EventQueue *queue, uint64_t timeout_ms) {
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->count == 0) {
        if (timeout_ms == 0) {
            pthread_cond_wait(&queue->not_empty, &queue->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            
            int rc = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
            if (rc != 0) {
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            }
        }
    }
    
    StreamEvent *event = malloc(sizeof(StreamEvent));
    if (event) {
        *event = queue->events[queue->tail];
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
    }
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return event;
}

/* ============================================================================
 * Geofence Engine (Forward declarations - implemented in geofence.c)
 * ============================================================================ */

extern int geofence_engine_init(GeofenceEngine *engine);
extern void geofence_engine_free(GeofenceEngine *engine);
extern void geofence_check_update(UrbisStream *stream, uint64_t object_id,
                                   const Point *old_pos, const Point *new_pos,
                                   uint64_t timestamp);

/* ============================================================================
 * Proximity Monitor (Forward declarations)
 * ============================================================================ */

extern int proximity_monitor_init(ProximityMonitor *monitor);
extern void proximity_monitor_free(ProximityMonitor *monitor);
extern void proximity_check_update(UrbisStream *stream, uint64_t object_id,
                                    const Point *position, uint64_t timestamp);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static double calculate_speed(const Point *p1, const Point *p2, 
                              uint64_t t1, uint64_t t2) {
    if (t2 <= t1) return 0.0;
    
    // Approximate degrees to meters (at equator, 1 degree ≈ 111km)
    double dx = (p2->x - p1->x) * 111000 * cos(p1->y * M_PI / 180);
    double dy = (p2->y - p1->y) * 111000;
    double distance = sqrt(dx * dx + dy * dy);
    double time_sec = (t2 - t1) / 1000.0;
    
    return distance / time_sec;
}

static double calculate_heading(const Point *p1, const Point *p2) {
    double dx = p2->x - p1->x;
    double dy = p2->y - p1->y;
    
    double heading = atan2(dx, dy) * 180.0 / M_PI;
    if (heading < 0) heading += 360.0;
    
    return heading;
}

/* ============================================================================
 * Stream Lifecycle
 * ============================================================================ */

UrbisStream* stream_create(SpatialIndex *idx) {
    UrbisStream *stream = calloc(1, sizeof(UrbisStream));
    if (!stream) return NULL;
    
    stream->spatial_index = idx;
    
    // Initialize tracked objects table
    if (tracked_table_init(&stream->objects, 10007) != STREAM_OK) {
        free(stream);
        return NULL;
    }
    
    // Initialize history table
    if (history_table_init(&stream->histories, 10007, 
                           STREAM_DEFAULT_HISTORY_SIZE) != STREAM_OK) {
        tracked_table_free(&stream->objects);
        free(stream);
        return NULL;
    }
    
    // Initialize geofence engine
    if (geofence_engine_init(&stream->geofence) != STREAM_OK) {
        history_table_free(&stream->histories);
        tracked_table_free(&stream->objects);
        free(stream);
        return NULL;
    }
    
    // Initialize proximity monitor
    if (proximity_monitor_init(&stream->proximity) != STREAM_OK) {
        geofence_engine_free(&stream->geofence);
        history_table_free(&stream->histories);
        tracked_table_free(&stream->objects);
        free(stream);
        return NULL;
    }
    
    // Initialize event queue
    if (event_queue_init(&stream->event_queue, STREAM_EVENT_QUEUE_SIZE) != STREAM_OK) {
        proximity_monitor_free(&stream->proximity);
        geofence_engine_free(&stream->geofence);
        history_table_free(&stream->histories);
        tracked_table_free(&stream->objects);
        free(stream);
        return NULL;
    }
    
    stream->running = false;
    stream->update_count = 0;
    stream->event_count = 0;
    
    return stream;
}

void stream_destroy(UrbisStream *stream) {
    if (!stream) return;
    
    if (stream->running) {
        stream_stop(stream);
    }
    
    event_queue_free(&stream->event_queue);
    proximity_monitor_free(&stream->proximity);
    geofence_engine_free(&stream->geofence);
    history_table_free(&stream->histories);
    tracked_table_free(&stream->objects);
    
    free(stream);
}

int stream_start(UrbisStream *stream) {
    if (!stream) return STREAM_ERR_NULL;
    stream->running = true;
    return STREAM_OK;
}

int stream_stop(UrbisStream *stream) {
    if (!stream) return STREAM_ERR_NULL;
    stream->running = false;
    return STREAM_OK;
}

/* ============================================================================
 * Location Updates
 * ============================================================================ */

int stream_update_location(UrbisStream *stream, uint64_t object_id,
                           double x, double y, uint64_t timestamp) {
    return stream_update_location_ex(stream, object_id, x, y, timestamp, -1, -1);
}

int stream_update_location_ex(UrbisStream *stream, uint64_t object_id,
                              double x, double y, uint64_t timestamp,
                              double speed, double heading) {
    if (!stream) return STREAM_ERR_NULL;
    
    Point new_pos = {x, y};
    Point old_pos = {0, 0};
    bool had_previous = false;
    bool was_moving = false;
    
    // Update tracked object
    pthread_rwlock_wrlock(&stream->objects.lock);
    
    TrackedObject *obj = tracked_table_get(&stream->objects, object_id);
    if (!obj) {
        obj = tracked_table_insert(&stream->objects, object_id);
        if (!obj) {
            pthread_rwlock_unlock(&stream->objects.lock);
            return STREAM_ERR_ALLOC;
        }
        obj->current_position = new_pos;
        obj->previous_position = new_pos;
    } else {
        had_previous = true;
        old_pos = obj->current_position;
        was_moving = obj->is_moving;
        obj->previous_position = obj->current_position;
        obj->current_position = new_pos;
    }
    
    obj->timestamp = timestamp;
    obj->last_update = get_current_time_ms();
    
    // Calculate speed and heading if not provided
    if (had_previous) {
        if (speed < 0) {
            obj->speed = calculate_speed(&old_pos, &new_pos, 
                                         obj->timestamp - 1000, timestamp);
        } else {
            obj->speed = speed;
        }
        
        if (heading < 0) {
            obj->heading = calculate_heading(&old_pos, &new_pos);
        } else {
            obj->heading = heading;
        }
    } else {
        obj->speed = speed >= 0 ? speed : 0;
        obj->heading = heading >= 0 ? heading : 0;
    }
    
    obj->is_moving = obj->speed > STREAM_STOP_SPEED_THRESHOLD;
    
    pthread_rwlock_unlock(&stream->objects.lock);
    
    // Add to history
    pthread_rwlock_wrlock(&stream->histories.lock);
    
    MovementHistory *history = history_table_get_or_create(&stream->histories, object_id);
    if (history) {
        PositionRecord record = {
            .position = new_pos,
            .timestamp = timestamp,
            .speed = obj->speed,
            .heading = obj->heading
        };
        history_add(history, &record);
    }
    
    pthread_rwlock_unlock(&stream->histories.lock);
    
    // Check geofences
    if (had_previous) {
        geofence_check_update(stream, object_id, &old_pos, &new_pos, timestamp);
    }
    
    // Check proximity
    proximity_check_update(stream, object_id, &new_pos, timestamp);
    
    // Generate movement events
    if (had_previous && !was_moving && obj->is_moving) {
        StreamEvent event = {
            .type = EVENT_MOVEMENT_STARTED,
            .timestamp = timestamp,
            .data.movement = {
                .object_id = object_id,
                .position = new_pos,
                .speed = obj->speed
            }
        };
        event_queue_push(&stream->event_queue, &event);
        stream->event_count++;
    }
    
    stream->update_count++;
    
    return STREAM_OK;
}

int stream_update_batch(UrbisStream *stream, const uint64_t *object_ids,
                        const double *x, const double *y,
                        const uint64_t *timestamps, size_t count) {
    if (!stream || !object_ids || !x || !y || !timestamps) {
        return STREAM_ERR_NULL;
    }
    
    for (size_t i = 0; i < count; i++) {
        int err = stream_update_location(stream, object_ids[i], x[i], y[i], timestamps[i]);
        if (err != STREAM_OK) return err;
    }
    
    return STREAM_OK;
}

TrackedObject* stream_get_object(UrbisStream *stream, uint64_t object_id) {
    if (!stream) return NULL;
    
    pthread_rwlock_rdlock(&stream->objects.lock);
    TrackedObject *obj = tracked_table_get(&stream->objects, object_id);
    pthread_rwlock_unlock(&stream->objects.lock);
    
    return obj;
}

int stream_remove_object(UrbisStream *stream, uint64_t object_id) {
    if (!stream) return STREAM_ERR_NULL;
    
    pthread_rwlock_wrlock(&stream->objects.lock);
    int err = tracked_table_remove(&stream->objects, object_id);
    pthread_rwlock_unlock(&stream->objects.lock);
    
    return err;
}

TrackedObject** stream_query_region(UrbisStream *stream, const MBR *region,
                                     size_t *count) {
    if (!stream || !region || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    // Collect objects in region
    size_t capacity = 64;
    TrackedObject **result = malloc(capacity * sizeof(TrackedObject *));
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    size_t found = 0;
    
    pthread_rwlock_rdlock(&stream->objects.lock);
    
    for (size_t i = 0; i < stream->objects.bucket_count; i++) {
        TrackedObjectEntry *entry = stream->objects.buckets[i];
        while (entry) {
            Point p = entry->object.current_position;
            if (p.x >= region->min_x && p.x <= region->max_x &&
                p.y >= region->min_y && p.y <= region->max_y) {
                
                if (found >= capacity) {
                    capacity *= 2;
                    TrackedObject **new_result = realloc(result, 
                                                          capacity * sizeof(TrackedObject *));
                    if (!new_result) break;
                    result = new_result;
                }
                
                result[found++] = &entry->object;
            }
            entry = entry->next;
        }
    }
    
    pthread_rwlock_unlock(&stream->objects.lock);
    
    *count = found;
    return result;
}

/* ============================================================================
 * Event Queue Operations
 * ============================================================================ */

StreamEvent* stream_poll_event(UrbisStream *stream) {
    if (!stream) return NULL;
    return event_queue_poll(&stream->event_queue);
}

StreamEvent* stream_wait_event(UrbisStream *stream, uint64_t timeout_ms) {
    if (!stream) return NULL;
    return event_queue_wait(&stream->event_queue, timeout_ms);
}

size_t stream_event_count(UrbisStream *stream) {
    if (!stream) return 0;
    
    pthread_mutex_lock(&stream->event_queue.mutex);
    size_t count = stream->event_queue.count;
    pthread_mutex_unlock(&stream->event_queue.mutex);
    
    return count;
}

void stream_event_free(StreamEvent *event) {
    free(event);
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void stream_get_stats(UrbisStream *stream, StreamStats *stats) {
    if (!stream || !stats) return;
    
    memset(stats, 0, sizeof(StreamStats));
    
    pthread_rwlock_rdlock(&stream->objects.lock);
    stats->tracked_objects = stream->objects.object_count;
    pthread_rwlock_unlock(&stream->objects.lock);
    
    pthread_rwlock_rdlock(&stream->geofence.lock);
    stats->geofence_zones = stream->geofence.zone_count;
    pthread_rwlock_unlock(&stream->geofence.lock);
    
    pthread_rwlock_rdlock(&stream->proximity.lock);
    stats->proximity_rules = stream->proximity.rule_count;
    pthread_rwlock_unlock(&stream->proximity.lock);
    
    pthread_mutex_lock(&stream->event_queue.mutex);
    stats->pending_events = stream->event_queue.count;
    pthread_mutex_unlock(&stream->event_queue.mutex);
    
    stats->total_updates = stream->update_count;
    stats->total_events = stream->event_count;
}

