/**
 * @file geofence.c
 * @brief Geofencing engine implementation
 */

#include "../include/streaming.h"
#include "../include/spatial_ops.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* Forward declaration for tracked_table_get from streaming.c */
extern TrackedObject* tracked_table_get(TrackedObjectTable *table, uint64_t object_id);

/* ============================================================================
 * Geofence Engine Initialization
 * ============================================================================ */

int geofence_engine_init(GeofenceEngine *engine) {
    if (!engine) return STREAM_ERR_NULL;
    
    engine->zones = NULL;
    engine->zone_count = 0;
    engine->zone_capacity = 0;
    
    engine->states = NULL;
    engine->state_count = 0;
    engine->state_capacity = 0;
    
    engine->callback = NULL;
    engine->callback_data = NULL;
    engine->next_event_id = 1;
    
    pthread_rwlock_init(&engine->lock, NULL);
    
    return STREAM_OK;
}

void geofence_engine_free(GeofenceEngine *engine) {
    if (!engine) return;
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        polygon_free(&engine->zones[i].boundary);
    }
    
    free(engine->zones);
    free(engine->states);
    pthread_rwlock_destroy(&engine->lock);
}

/* ============================================================================
 * Zone State Management
 * ============================================================================ */

static ZoneState* find_zone_state(GeofenceEngine *engine, uint64_t object_id,
                                   uint64_t zone_id) {
    for (size_t i = 0; i < engine->state_count; i++) {
        if (engine->states[i].object_id == object_id &&
            engine->states[i].zone_id == zone_id) {
            return &engine->states[i];
        }
    }
    return NULL;
}

static ZoneState* get_or_create_zone_state(GeofenceEngine *engine, uint64_t object_id,
                                            uint64_t zone_id) {
    ZoneState *state = find_zone_state(engine, object_id, zone_id);
    if (state) return state;
    
    // Grow array if needed
    if (engine->state_count >= engine->state_capacity) {
        size_t new_cap = engine->state_capacity == 0 ? 64 : engine->state_capacity * 2;
        ZoneState *new_states = realloc(engine->states, new_cap * sizeof(ZoneState));
        if (!new_states) return NULL;
        engine->states = new_states;
        engine->state_capacity = new_cap;
    }
    
    state = &engine->states[engine->state_count++];
    state->object_id = object_id;
    state->zone_id = zone_id;
    state->inside = false;
    state->enter_time = 0;
    state->dwell_fired = false;
    
    return state;
}

/* ============================================================================
 * Geofence Zone Management
 * ============================================================================ */

int stream_geofence_add(UrbisStream *stream, const GeofenceZone *zone) {
    if (!stream || !zone) return STREAM_ERR_NULL;
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_wrlock(&engine->lock);
    
    // Check if zone already exists
    for (size_t i = 0; i < engine->zone_count; i++) {
        if (engine->zones[i].zone_id == zone->zone_id) {
            pthread_rwlock_unlock(&engine->lock);
            return STREAM_ERR_EXISTS;
        }
    }
    
    // Grow array if needed
    if (engine->zone_count >= engine->zone_capacity) {
        size_t new_cap = engine->zone_capacity == 0 ? 16 : engine->zone_capacity * 2;
        GeofenceZone *new_zones = realloc(engine->zones, new_cap * sizeof(GeofenceZone));
        if (!new_zones) {
            pthread_rwlock_unlock(&engine->lock);
            return STREAM_ERR_ALLOC;
        }
        engine->zones = new_zones;
        engine->zone_capacity = new_cap;
    }
    
    // Copy zone
    GeofenceZone *new_zone = &engine->zones[engine->zone_count];
    new_zone->zone_id = zone->zone_id;
    strncpy(new_zone->name, zone->name, sizeof(new_zone->name) - 1);
    new_zone->name[sizeof(new_zone->name) - 1] = '\0';
    new_zone->active = zone->active;
    new_zone->dwell_threshold = zone->dwell_threshold;
    new_zone->user_data = zone->user_data;
    
    // Copy boundary polygon
    if (polygon_init(&new_zone->boundary, zone->boundary.ext_count) != GEOM_OK) {
        pthread_rwlock_unlock(&engine->lock);
        return STREAM_ERR_ALLOC;
    }
    
    for (size_t i = 0; i < zone->boundary.ext_count; i++) {
        polygon_add_exterior_point(&new_zone->boundary, zone->boundary.exterior[i]);
    }
    
    // Compute MBR
    polygon_mbr(&new_zone->boundary, &new_zone->mbr);
    
    engine->zone_count++;
    
    pthread_rwlock_unlock(&engine->lock);
    
    return STREAM_OK;
}

int stream_geofence_remove(UrbisStream *stream, uint64_t zone_id) {
    if (!stream) return STREAM_ERR_NULL;
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_wrlock(&engine->lock);
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        if (engine->zones[i].zone_id == zone_id) {
            polygon_free(&engine->zones[i].boundary);
            
            // Shift remaining zones
            for (size_t j = i; j < engine->zone_count - 1; j++) {
                engine->zones[j] = engine->zones[j + 1];
            }
            engine->zone_count--;
            
            // Remove related states
            size_t new_state_count = 0;
            for (size_t j = 0; j < engine->state_count; j++) {
                if (engine->states[j].zone_id != zone_id) {
                    if (new_state_count != j) {
                        engine->states[new_state_count] = engine->states[j];
                    }
                    new_state_count++;
                }
            }
            engine->state_count = new_state_count;
            
            pthread_rwlock_unlock(&engine->lock);
            return STREAM_OK;
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
    return STREAM_ERR_NOT_FOUND;
}

int stream_geofence_update(UrbisStream *stream, const GeofenceZone *zone) {
    if (!stream || !zone) return STREAM_ERR_NULL;
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_wrlock(&engine->lock);
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        if (engine->zones[i].zone_id == zone->zone_id) {
            GeofenceZone *existing = &engine->zones[i];
            
            snprintf(existing->name, sizeof(existing->name), "%s", zone->name);
            existing->active = zone->active;
            existing->dwell_threshold = zone->dwell_threshold;
            existing->user_data = zone->user_data;
            
            // Update boundary if changed
            polygon_free(&existing->boundary);
            if (polygon_init(&existing->boundary, zone->boundary.ext_count) == GEOM_OK) {
                for (size_t j = 0; j < zone->boundary.ext_count; j++) {
                    polygon_add_exterior_point(&existing->boundary, zone->boundary.exterior[j]);
                }
            }
            polygon_mbr(&existing->boundary, &existing->mbr);
            
            pthread_rwlock_unlock(&engine->lock);
            return STREAM_OK;
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
    return STREAM_ERR_NOT_FOUND;
}

GeofenceZone* stream_geofence_get(UrbisStream *stream, uint64_t zone_id) {
    if (!stream) return NULL;
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_rdlock(&engine->lock);
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        if (engine->zones[i].zone_id == zone_id) {
            GeofenceZone *zone = &engine->zones[i];
            pthread_rwlock_unlock(&engine->lock);
            return zone;
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
    return NULL;
}

GeofenceZone** stream_geofence_list(UrbisStream *stream, size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_rdlock(&engine->lock);
    
    if (engine->zone_count == 0) {
        pthread_rwlock_unlock(&engine->lock);
        *count = 0;
        return NULL;
    }
    
    GeofenceZone **result = malloc(engine->zone_count * sizeof(GeofenceZone *));
    if (!result) {
        pthread_rwlock_unlock(&engine->lock);
        *count = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        result[i] = &engine->zones[i];
    }
    
    *count = engine->zone_count;
    
    pthread_rwlock_unlock(&engine->lock);
    
    return result;
}

int stream_geofence_set_callback(UrbisStream *stream, GeofenceCallback callback,
                                  void *user_data) {
    if (!stream) return STREAM_ERR_NULL;
    
    pthread_rwlock_wrlock(&stream->geofence.lock);
    stream->geofence.callback = callback;
    stream->geofence.callback_data = user_data;
    pthread_rwlock_unlock(&stream->geofence.lock);
    
    return STREAM_OK;
}

uint64_t* stream_geofence_check_point(UrbisStream *stream, const Point *p,
                                       size_t *count) {
    if (!stream || !p || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_rdlock(&engine->lock);
    
    size_t capacity = 16;
    uint64_t *result = malloc(capacity * sizeof(uint64_t));
    size_t found = 0;
    
    if (!result) {
        pthread_rwlock_unlock(&engine->lock);
        *count = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        GeofenceZone *zone = &engine->zones[i];
        
        if (!zone->active) continue;
        
        // Quick MBR check
        if (p->x < zone->mbr.min_x || p->x > zone->mbr.max_x ||
            p->y < zone->mbr.min_y || p->y > zone->mbr.max_y) {
            continue;
        }
        
        // Point-in-polygon check
        if (point_in_polygon(p, &zone->boundary)) {
            if (found >= capacity) {
                capacity *= 2;
                uint64_t *new_result = realloc(result, capacity * sizeof(uint64_t));
                if (!new_result) break;
                result = new_result;
            }
            result[found++] = zone->zone_id;
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
    
    *count = found;
    return result;
}

uint64_t* stream_geofence_objects_in_zone(UrbisStream *stream, uint64_t zone_id,
                                           size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_rdlock(&engine->lock);
    
    size_t capacity = 64;
    uint64_t *result = malloc(capacity * sizeof(uint64_t));
    size_t found = 0;
    
    if (!result) {
        pthread_rwlock_unlock(&engine->lock);
        *count = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < engine->state_count; i++) {
        ZoneState *state = &engine->states[i];
        if (state->zone_id == zone_id && state->inside) {
            if (found >= capacity) {
                capacity *= 2;
                uint64_t *new_result = realloc(result, capacity * sizeof(uint64_t));
                if (!new_result) break;
                result = new_result;
            }
            result[found++] = state->object_id;
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
    
    *count = found;
    return result;
}

/* ============================================================================
 * Geofence Check (called on location update)
 * ============================================================================ */

void geofence_check_update(UrbisStream *stream, uint64_t object_id,
                            const Point *old_pos, const Point *new_pos,
                            uint64_t timestamp) {
    (void)old_pos;  /* May be used for more sophisticated enter/exit detection */
    if (!stream || !new_pos) return;
    
    GeofenceEngine *engine = &stream->geofence;
    
    pthread_rwlock_wrlock(&engine->lock);
    
    for (size_t i = 0; i < engine->zone_count; i++) {
        GeofenceZone *zone = &engine->zones[i];
        
        if (!zone->active) continue;
        
        // Quick MBR check for new position
        bool maybe_in_new = (new_pos->x >= zone->mbr.min_x && 
                             new_pos->x <= zone->mbr.max_x &&
                             new_pos->y >= zone->mbr.min_y && 
                             new_pos->y <= zone->mbr.max_y);
        
        bool in_zone_now = maybe_in_new && point_in_polygon(new_pos, &zone->boundary);
        
        // Get or create state
        ZoneState *state = get_or_create_zone_state(engine, object_id, zone->zone_id);
        if (!state) continue;
        
        bool was_inside = state->inside;
        
        // Check for enter/exit events
        if (!was_inside && in_zone_now) {
            // ENTER event
            state->inside = true;
            state->enter_time = timestamp;
            state->dwell_fired = false;
            
            GeofenceEvent event = {
                .event_id = engine->next_event_id++,
                .object_id = object_id,
                .zone_id = zone->zone_id,
                .type = GEOFENCE_ENTER,
                .timestamp = timestamp,
                .position = *new_pos,
                .dwell_time = 0
            };
            
            // Call callback
            if (engine->callback) {
                engine->callback(&event, engine->callback_data);
            }
            
            // Add to event queue
            StreamEvent stream_event = {
                .type = EVENT_GEOFENCE,
                .timestamp = timestamp,
                .data.geofence = event
            };
            
            pthread_rwlock_unlock(&engine->lock);
            pthread_mutex_lock(&stream->event_queue.mutex);
            
            if (stream->event_queue.count < stream->event_queue.capacity) {
                stream->event_queue.events[stream->event_queue.head] = stream_event;
                stream->event_queue.head = (stream->event_queue.head + 1) % 
                                           stream->event_queue.capacity;
                stream->event_queue.count++;
                pthread_cond_signal(&stream->event_queue.not_empty);
            }
            
            pthread_mutex_unlock(&stream->event_queue.mutex);
            stream->event_count++;
            pthread_rwlock_wrlock(&engine->lock);
            
        } else if (was_inside && !in_zone_now) {
            // EXIT event
            state->inside = false;
            
            GeofenceEvent event = {
                .event_id = engine->next_event_id++,
                .object_id = object_id,
                .zone_id = zone->zone_id,
                .type = GEOFENCE_EXIT,
                .timestamp = timestamp,
                .position = *new_pos,
                .dwell_time = timestamp - state->enter_time
            };
            
            if (engine->callback) {
                engine->callback(&event, engine->callback_data);
            }
            
            StreamEvent stream_event = {
                .type = EVENT_GEOFENCE,
                .timestamp = timestamp,
                .data.geofence = event
            };
            
            pthread_rwlock_unlock(&engine->lock);
            pthread_mutex_lock(&stream->event_queue.mutex);
            
            if (stream->event_queue.count < stream->event_queue.capacity) {
                stream->event_queue.events[stream->event_queue.head] = stream_event;
                stream->event_queue.head = (stream->event_queue.head + 1) % 
                                           stream->event_queue.capacity;
                stream->event_queue.count++;
                pthread_cond_signal(&stream->event_queue.not_empty);
            }
            
            pthread_mutex_unlock(&stream->event_queue.mutex);
            stream->event_count++;
            pthread_rwlock_wrlock(&engine->lock);
            
        } else if (in_zone_now && !state->dwell_fired && zone->dwell_threshold > 0) {
            // Check for DWELL event
            uint64_t dwell_time = timestamp - state->enter_time;
            if (dwell_time >= zone->dwell_threshold) {
                state->dwell_fired = true;
                
                GeofenceEvent event = {
                    .event_id = engine->next_event_id++,
                    .object_id = object_id,
                    .zone_id = zone->zone_id,
                    .type = GEOFENCE_DWELL,
                    .timestamp = timestamp,
                    .position = *new_pos,
                    .dwell_time = dwell_time
                };
                
                if (engine->callback) {
                    engine->callback(&event, engine->callback_data);
                }
                
                StreamEvent stream_event = {
                    .type = EVENT_GEOFENCE,
                    .timestamp = timestamp,
                    .data.geofence = event
                };
                
                pthread_rwlock_unlock(&engine->lock);
                pthread_mutex_lock(&stream->event_queue.mutex);
                
                if (stream->event_queue.count < stream->event_queue.capacity) {
                    stream->event_queue.events[stream->event_queue.head] = stream_event;
                    stream->event_queue.head = (stream->event_queue.head + 1) % 
                                               stream->event_queue.capacity;
                    stream->event_queue.count++;
                    pthread_cond_signal(&stream->event_queue.not_empty);
                }
                
                pthread_mutex_unlock(&stream->event_queue.mutex);
                stream->event_count++;
                pthread_rwlock_wrlock(&engine->lock);
            }
        }
    }
    
    pthread_rwlock_unlock(&engine->lock);
}

/* ============================================================================
 * Proximity Monitor Implementation
 * ============================================================================ */

int proximity_monitor_init(ProximityMonitor *monitor) {
    if (!monitor) return STREAM_ERR_NULL;
    
    monitor->rules = NULL;
    monitor->rule_count = 0;
    monitor->rule_capacity = 0;
    
    monitor->states = NULL;
    monitor->state_count = 0;
    monitor->state_capacity = 0;
    
    monitor->callback = NULL;
    monitor->callback_data = NULL;
    monitor->next_event_id = 1;
    monitor->next_rule_id = 1;
    
    pthread_rwlock_init(&monitor->lock, NULL);
    
    return STREAM_OK;
}

void proximity_monitor_free(ProximityMonitor *monitor) {
    if (!monitor) return;
    
    free(monitor->rules);
    free(monitor->states);
    pthread_rwlock_destroy(&monitor->lock);
}

int stream_proximity_add_rule(UrbisStream *stream, const ProximityRule *rule) {
    if (!stream || !rule) return STREAM_ERR_NULL;
    
    ProximityMonitor *monitor = &stream->proximity;
    
    pthread_rwlock_wrlock(&monitor->lock);
    
    // Grow array if needed
    if (monitor->rule_count >= monitor->rule_capacity) {
        size_t new_cap = monitor->rule_capacity == 0 ? 16 : monitor->rule_capacity * 2;
        ProximityRule *new_rules = realloc(monitor->rules, new_cap * sizeof(ProximityRule));
        if (!new_rules) {
            pthread_rwlock_unlock(&monitor->lock);
            return STREAM_ERR_ALLOC;
        }
        monitor->rules = new_rules;
        monitor->rule_capacity = new_cap;
    }
    
    ProximityRule *new_rule = &monitor->rules[monitor->rule_count];
    *new_rule = *rule;
    new_rule->rule_id = monitor->next_rule_id++;
    new_rule->active = true;
    
    monitor->rule_count++;
    
    pthread_rwlock_unlock(&monitor->lock);
    
    return STREAM_OK;
}

int stream_proximity_remove_rule(UrbisStream *stream, uint64_t rule_id) {
    if (!stream) return STREAM_ERR_NULL;
    
    ProximityMonitor *monitor = &stream->proximity;
    
    pthread_rwlock_wrlock(&monitor->lock);
    
    for (size_t i = 0; i < monitor->rule_count; i++) {
        if (monitor->rules[i].rule_id == rule_id) {
            for (size_t j = i; j < monitor->rule_count - 1; j++) {
                monitor->rules[j] = monitor->rules[j + 1];
            }
            monitor->rule_count--;
            
            pthread_rwlock_unlock(&monitor->lock);
            return STREAM_OK;
        }
    }
    
    pthread_rwlock_unlock(&monitor->lock);
    return STREAM_ERR_NOT_FOUND;
}

int stream_proximity_set_callback(UrbisStream *stream, ProximityCallback callback,
                                   void *user_data) {
    if (!stream) return STREAM_ERR_NULL;
    
    pthread_rwlock_wrlock(&stream->proximity.lock);
    stream->proximity.callback = callback;
    stream->proximity.callback_data = user_data;
    pthread_rwlock_unlock(&stream->proximity.lock);
    
    return STREAM_OK;
}

void proximity_check_update(UrbisStream *stream, uint64_t object_id,
                             const Point *position, uint64_t timestamp) {
    if (!stream || !position) return;
    
    ProximityMonitor *monitor = &stream->proximity;
    
    pthread_rwlock_rdlock(&monitor->lock);
    
    // Check each rule
    for (size_t i = 0; i < monitor->rule_count; i++) {
        ProximityRule *rule = &monitor->rules[i];
        
        if (!rule->active) continue;
        
        // Check if this object matches the rule
        bool matches_a = (rule->object_a == 0 || rule->object_a == object_id);
        bool matches_b = (rule->object_b == 0 || rule->object_b == object_id);
        
        if (!matches_a && !matches_b) continue;
        
        // Find other tracked objects to check against
        pthread_rwlock_rdlock(&stream->objects.lock);
        
        for (size_t j = 0; j < stream->objects.bucket_count; j++) {
            TrackedObjectEntry *entry = stream->objects.buckets[j];
            while (entry) {
                TrackedObject *other = &entry->object;
                
                if (other->object_id == object_id) {
                    entry = entry->next;
                    continue;
                }
                
                // Check if this pair matches the rule
                bool pair_matches = false;
                if (rule->object_a == 0 && rule->object_b == 0) {
                    pair_matches = true;  // Any pair
                } else if (rule->object_a == object_id && 
                           (rule->object_b == 0 || rule->object_b == other->object_id)) {
                    pair_matches = true;
                } else if (rule->object_b == object_id && 
                           (rule->object_a == 0 || rule->object_a == other->object_id)) {
                    pair_matches = true;
                }
                
                if (!pair_matches) {
                    entry = entry->next;
                    continue;
                }
                
                // Calculate distance
                double dx = (position->x - other->current_position.x) * 111000;
                double dy = (position->y - other->current_position.y) * 111000;
                double distance = sqrt(dx * dx + dy * dy);
                
                if (distance <= rule->threshold) {
                    // Proximity event!
                    ProximityEvent event = {
                        .event_id = monitor->next_event_id++,
                        .rule_id = rule->rule_id,
                        .object_a = object_id,
                        .object_b = other->object_id,
                        .distance = distance,
                        .timestamp = timestamp,
                        .position_a = *position,
                        .position_b = other->current_position
                    };
                    
                    if (monitor->callback) {
                        pthread_rwlock_unlock(&stream->objects.lock);
                        pthread_rwlock_unlock(&monitor->lock);
                        monitor->callback(&event, monitor->callback_data);
                        pthread_rwlock_rdlock(&monitor->lock);
                        pthread_rwlock_rdlock(&stream->objects.lock);
                    }
                    
                    // Add to event queue
                    StreamEvent stream_event = {
                        .type = EVENT_PROXIMITY,
                        .timestamp = timestamp,
                        .data.proximity = event
                    };
                    
                    pthread_mutex_lock(&stream->event_queue.mutex);
                    if (stream->event_queue.count < stream->event_queue.capacity) {
                        stream->event_queue.events[stream->event_queue.head] = stream_event;
                        stream->event_queue.head = (stream->event_queue.head + 1) % 
                                                   stream->event_queue.capacity;
                        stream->event_queue.count++;
                        pthread_cond_signal(&stream->event_queue.not_empty);
                    }
                    pthread_mutex_unlock(&stream->event_queue.mutex);
                    stream->event_count++;
                    
                    if (rule->one_shot) {
                        rule->active = false;
                    }
                }
                
                entry = entry->next;
            }
        }
        
        pthread_rwlock_unlock(&stream->objects.lock);
    }
    
    pthread_rwlock_unlock(&monitor->lock);
}

uint64_t* stream_proximity_query(UrbisStream *stream, const Point *p,
                                  double distance, size_t *count) {
    if (!stream || !p || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    size_t capacity = 64;
    uint64_t *result = malloc(capacity * sizeof(uint64_t));
    size_t found = 0;
    
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    pthread_rwlock_rdlock(&stream->objects.lock);
    
    for (size_t i = 0; i < stream->objects.bucket_count; i++) {
        TrackedObjectEntry *entry = stream->objects.buckets[i];
        while (entry) {
            double dx = (p->x - entry->object.current_position.x) * 111000;
            double dy = (p->y - entry->object.current_position.y) * 111000;
            double dist = sqrt(dx * dx + dy * dy);
            
            if (dist <= distance) {
                if (found >= capacity) {
                    capacity *= 2;
                    uint64_t *new_result = realloc(result, capacity * sizeof(uint64_t));
                    if (!new_result) break;
                    result = new_result;
                }
                result[found++] = entry->object.object_id;
            }
            
            entry = entry->next;
        }
    }
    
    pthread_rwlock_unlock(&stream->objects.lock);
    
    *count = found;
    return result;
}

uint64_t* stream_proximity_query_object(UrbisStream *stream, uint64_t object_id,
                                         double distance, size_t *count) {
    if (!stream || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    pthread_rwlock_rdlock(&stream->objects.lock);
    TrackedObject *obj = tracked_table_get(&stream->objects, object_id);
    if (!obj) {
        pthread_rwlock_unlock(&stream->objects.lock);
        *count = 0;
        return NULL;
    }
    
    Point p = obj->current_position;
    pthread_rwlock_unlock(&stream->objects.lock);
    
    return stream_proximity_query(stream, &p, distance, count);
}

