/**
 * @file trajectory.c
 * @brief Trajectory analysis implementation
 */

#include "../include/streaming.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Calculate distance between two points in meters (approximate)
 */
static double point_distance_meters(const Point *p1, const Point *p2) {
    // Approximate degrees to meters (at equator, 1 degree ≈ 111km)
    double lat_avg = (p1->y + p2->y) / 2.0;
    double dx = (p2->x - p1->x) * 111000 * cos(lat_avg * M_PI / 180);
    double dy = (p2->y - p1->y) * 111000;
    return sqrt(dx * dx + dy * dy);
}

/**
 * @brief Calculate heading from p1 to p2
 */
static double calc_heading(const Point *p1, const Point *p2) {
    double dx = p2->x - p1->x;
    double dy = p2->y - p1->y;
    double heading = atan2(dx, dy) * 180.0 / M_PI;
    if (heading < 0) heading += 360.0;
    return heading;
}

/**
 * @brief Check if speed indicates a stop
 */
static bool is_stopped(double speed) {
    return speed < STREAM_STOP_SPEED_THRESHOLD;
}

/**
 * @brief Hash function (same as streaming.c)
 */
static size_t hash_uint64(uint64_t key, size_t bucket_count) {
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;
    return key % bucket_count;
}

/**
 * @brief Get movement history entry for object
 */
static MovementHistoryEntry* get_history_entry(MovementHistoryTable *table,
                                                uint64_t object_id) {
    size_t bucket = hash_uint64(object_id, table->bucket_count);
    MovementHistoryEntry *entry = table->buckets[bucket];
    while (entry) {
        if (entry->object_id == object_id) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/* ============================================================================
 * Trajectory Statistics
 * ============================================================================ */

TrajectoryStats* stream_trajectory_stats(UrbisStream *stream, uint64_t object_id,
                                          uint64_t start_time, uint64_t end_time) {
    if (!stream) return NULL;
    
    pthread_rwlock_rdlock(&stream->histories.lock);
    
    MovementHistoryEntry *entry = get_history_entry(&stream->histories, object_id);
    if (!entry || entry->history.count == 0) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    MovementHistory *history = &entry->history;
    
    // Collect records in time range
    size_t record_cap = 256;
    PositionRecord *records = malloc(record_cap * sizeof(PositionRecord));
    size_t record_count = 0;
    
    if (!records) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    // Iterate through circular buffer
    size_t idx = history->tail;
    for (size_t i = 0; i < history->count; i++) {
        PositionRecord *rec = &history->records[idx];
        
        if (rec->timestamp >= start_time && rec->timestamp <= end_time) {
            if (record_count >= record_cap) {
                record_cap *= 2;
                PositionRecord *new_rec = realloc(records, record_cap * sizeof(PositionRecord));
                if (!new_rec) break;
                records = new_rec;
            }
            records[record_count++] = *rec;
        }
        
        idx = (idx + 1) % history->capacity;
    }
    
    pthread_rwlock_unlock(&stream->histories.lock);
    
    if (record_count < 2) {
        free(records);
        return NULL;
    }
    
    // Calculate statistics
    TrajectoryStats *stats = calloc(1, sizeof(TrajectoryStats));
    if (!stats) {
        free(records);
        return NULL;
    }
    
    stats->object_id = object_id;
    stats->point_count = record_count;
    stats->start_point = records[0].position;
    stats->end_point = records[record_count - 1].position;
    stats->start_time = records[0].timestamp;
    stats->end_time = records[record_count - 1].timestamp;
    stats->total_time = stats->end_time - stats->start_time;
    
    double total_distance = 0;
    double max_speed = 0;
    uint64_t moving_time = 0;
    uint64_t stopped_time = 0;
    size_t stop_count = 0;
    bool was_stopped = is_stopped(records[0].speed);
    uint64_t stop_start = was_stopped ? records[0].timestamp : 0;
    
    for (size_t i = 1; i < record_count; i++) {
        // Calculate distance
        double dist = point_distance_meters(&records[i-1].position, &records[i].position);
        total_distance += dist;
        
        // Track max speed
        if (records[i].speed > max_speed) {
            max_speed = records[i].speed;
        }
        
        // Track moving/stopped time
        uint64_t segment_time = records[i].timestamp - records[i-1].timestamp;
        bool now_stopped = is_stopped(records[i].speed);
        
        if (now_stopped) {
            stopped_time += segment_time;
            if (!was_stopped) {
                stop_start = records[i].timestamp;
            }
        } else {
            moving_time += segment_time;
            if (was_stopped && stop_start > 0) {
                uint64_t stop_duration = records[i].timestamp - stop_start;
                if (stop_duration >= STREAM_STOP_TIME_THRESHOLD) {
                    stop_count++;
                }
            }
        }
        
        was_stopped = now_stopped;
    }
    
    // Check for final stop
    if (was_stopped && stop_start > 0) {
        uint64_t stop_duration = records[record_count-1].timestamp - stop_start;
        if (stop_duration >= STREAM_STOP_TIME_THRESHOLD) {
            stop_count++;
        }
    }
    
    stats->total_distance = total_distance;
    stats->max_speed = max_speed;
    stats->moving_time = moving_time;
    stats->stopped_time = stopped_time;
    stats->stop_count = stop_count;
    
    // Calculate average speed (only while moving)
    if (moving_time > 0) {
        stats->avg_speed = total_distance / (moving_time / 1000.0);
    }
    
    free(records);
    
    return stats;
}

void stream_trajectory_stats_free(TrajectoryStats *stats) {
    free(stats);
}

/* ============================================================================
 * Full Trajectory
 * ============================================================================ */

Trajectory* stream_trajectory_get(UrbisStream *stream, uint64_t object_id,
                                   uint64_t start_time, uint64_t end_time) {
    if (!stream) return NULL;
    
    pthread_rwlock_rdlock(&stream->histories.lock);
    
    MovementHistoryEntry *entry = get_history_entry(&stream->histories, object_id);
    if (!entry || entry->history.count == 0) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    MovementHistory *history = &entry->history;
    
    // Collect records in time range
    size_t record_cap = 256;
    PositionRecord *records = malloc(record_cap * sizeof(PositionRecord));
    size_t record_count = 0;
    
    if (!records) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    size_t idx = history->tail;
    for (size_t i = 0; i < history->count; i++) {
        PositionRecord *rec = &history->records[idx];
        
        if (rec->timestamp >= start_time && rec->timestamp <= end_time) {
            if (record_count >= record_cap) {
                record_cap *= 2;
                PositionRecord *new_rec = realloc(records, record_cap * sizeof(PositionRecord));
                if (!new_rec) break;
                records = new_rec;
            }
            records[record_count++] = *rec;
        }
        
        idx = (idx + 1) % history->capacity;
    }
    
    pthread_rwlock_unlock(&stream->histories.lock);
    
    if (record_count < 2) {
        free(records);
        return NULL;
    }
    
    // Create trajectory
    Trajectory *traj = calloc(1, sizeof(Trajectory));
    if (!traj) {
        free(records);
        return NULL;
    }
    
    // Calculate stats
    traj->stats.object_id = object_id;
    traj->stats.point_count = record_count;
    traj->stats.start_point = records[0].position;
    traj->stats.end_point = records[record_count - 1].position;
    traj->stats.start_time = records[0].timestamp;
    traj->stats.end_time = records[record_count - 1].timestamp;
    traj->stats.total_time = traj->stats.end_time - traj->stats.start_time;
    
    // Allocate segments
    traj->segments = malloc((record_count - 1) * sizeof(TrajectorySegment));
    if (!traj->segments) {
        free(traj);
        free(records);
        return NULL;
    }
    
    double total_distance = 0;
    double max_speed = 0;
    uint64_t moving_time = 0;
    uint64_t stopped_time = 0;
    size_t stop_count = 0;
    bool in_stop = false;
    
    for (size_t i = 0; i < record_count - 1; i++) {
        TrajectorySegment *seg = &traj->segments[i];
        
        seg->start = records[i].position;
        seg->end = records[i + 1].position;
        seg->start_time = records[i].timestamp;
        seg->end_time = records[i + 1].timestamp;
        seg->distance = point_distance_meters(&seg->start, &seg->end);
        seg->heading = calc_heading(&seg->start, &seg->end);
        
        uint64_t seg_time = seg->end_time - seg->start_time;
        if (seg_time > 0) {
            seg->speed = seg->distance / (seg_time / 1000.0);
        } else {
            seg->speed = 0;
        }
        
        seg->is_stop = is_stopped(seg->speed);
        
        total_distance += seg->distance;
        if (seg->speed > max_speed) max_speed = seg->speed;
        
        if (seg->is_stop) {
            stopped_time += seg_time;
            if (!in_stop) {
                in_stop = true;
            }
        } else {
            moving_time += seg_time;
            if (in_stop) {
                stop_count++;
                in_stop = false;
            }
        }
    }
    
    traj->segment_count = record_count - 1;
    
    // Final stop count
    if (in_stop) stop_count++;
    
    traj->stats.total_distance = total_distance;
    traj->stats.max_speed = max_speed;
    traj->stats.moving_time = moving_time;
    traj->stats.stopped_time = stopped_time;
    traj->stats.stop_count = stop_count;
    
    if (moving_time > 0) {
        traj->stats.avg_speed = total_distance / (moving_time / 1000.0);
    }
    
    // Create simplified path
    traj->path_count = record_count;
    traj->simplified_path = malloc(record_count * sizeof(Point));
    if (traj->simplified_path) {
        for (size_t i = 0; i < record_count; i++) {
            traj->simplified_path[i] = records[i].position;
        }
    }
    
    free(records);
    
    return traj;
}

void stream_trajectory_free(Trajectory *traj) {
    if (!traj) return;
    free(traj->segments);
    free(traj->simplified_path);
    free(traj);
}

/* ============================================================================
 * Trajectory Path
 * ============================================================================ */

LineString* stream_trajectory_path(UrbisStream *stream, uint64_t object_id,
                                    uint64_t start_time, uint64_t end_time) {
    if (!stream) return NULL;
    
    pthread_rwlock_rdlock(&stream->histories.lock);
    
    MovementHistoryEntry *entry = get_history_entry(&stream->histories, object_id);
    if (!entry || entry->history.count == 0) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    MovementHistory *history = &entry->history;
    
    // Count points in range
    size_t count = 0;
    size_t idx = history->tail;
    for (size_t i = 0; i < history->count; i++) {
        if (history->records[idx].timestamp >= start_time &&
            history->records[idx].timestamp <= end_time) {
            count++;
        }
        idx = (idx + 1) % history->capacity;
    }
    
    if (count < 2) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    // Create linestring
    LineString *ls = malloc(sizeof(LineString));
    if (!ls) {
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    if (linestring_init(ls, count) != GEOM_OK) {
        free(ls);
        pthread_rwlock_unlock(&stream->histories.lock);
        return NULL;
    }
    
    // Add points
    idx = history->tail;
    for (size_t i = 0; i < history->count; i++) {
        if (history->records[idx].timestamp >= start_time &&
            history->records[idx].timestamp <= end_time) {
            linestring_add_point(ls, history->records[idx].position);
        }
        idx = (idx + 1) % history->capacity;
    }
    
    pthread_rwlock_unlock(&stream->histories.lock);
    
    return ls;
}

/* ============================================================================
 * Simplified Path (Douglas-Peucker)
 * ============================================================================ */

/**
 * @brief Perpendicular distance from point to line segment
 */
static double perpendicular_distance(const Point *p, const Point *line_start,
                                      const Point *line_end) {
    double dx = line_end->x - line_start->x;
    double dy = line_end->y - line_start->y;
    
    double line_len_sq = dx * dx + dy * dy;
    if (line_len_sq < 1e-10) {
        // Line is a point
        return point_distance_meters(p, line_start);
    }
    
    // Project point onto line
    double t = ((p->x - line_start->x) * dx + (p->y - line_start->y) * dy) / line_len_sq;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    
    Point proj = {
        line_start->x + t * dx,
        line_start->y + t * dy
    };
    
    return point_distance_meters(p, &proj);
}

/**
 * @brief Douglas-Peucker recursive simplification
 */
static void douglas_peucker(const Point *points, size_t start, size_t end,
                            double tolerance, bool *keep) {
    if (end <= start + 1) return;
    
    double max_dist = 0;
    size_t max_idx = start;
    
    for (size_t i = start + 1; i < end; i++) {
        double dist = perpendicular_distance(&points[i], &points[start], &points[end]);
        if (dist > max_dist) {
            max_dist = dist;
            max_idx = i;
        }
    }
    
    if (max_dist > tolerance) {
        keep[max_idx] = true;
        douglas_peucker(points, start, max_idx, tolerance, keep);
        douglas_peucker(points, max_idx, end, tolerance, keep);
    }
}

LineString* stream_trajectory_simplified(UrbisStream *stream, uint64_t object_id,
                                          uint64_t start_time, uint64_t end_time,
                                          double tolerance) {
    if (!stream || tolerance < 0) return NULL;
    
    // Get full path first
    LineString *full_path = stream_trajectory_path(stream, object_id, start_time, end_time);
    if (!full_path || full_path->count < 3) {
        return full_path;  // No simplification needed
    }
    
    // Mark points to keep
    bool *keep = calloc(full_path->count, sizeof(bool));
    if (!keep) {
        linestring_free(full_path);
        free(full_path);
        return NULL;
    }
    
    // Always keep first and last
    keep[0] = true;
    keep[full_path->count - 1] = true;
    
    // Run Douglas-Peucker
    douglas_peucker(full_path->points, 0, full_path->count - 1, tolerance, keep);
    
    // Count kept points
    size_t kept_count = 0;
    for (size_t i = 0; i < full_path->count; i++) {
        if (keep[i]) kept_count++;
    }
    
    // Create simplified linestring
    LineString *simplified = malloc(sizeof(LineString));
    if (!simplified) {
        free(keep);
        linestring_free(full_path);
        free(full_path);
        return NULL;
    }
    
    if (linestring_init(simplified, kept_count) != GEOM_OK) {
        free(simplified);
        free(keep);
        linestring_free(full_path);
        free(full_path);
        return NULL;
    }
    
    for (size_t i = 0; i < full_path->count; i++) {
        if (keep[i]) {
            linestring_add_point(simplified, full_path->points[i]);
        }
    }
    
    free(keep);
    linestring_free(full_path);
    free(full_path);
    
    return simplified;
}

