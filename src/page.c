/**
 * @file page.c
 * @brief Page and track management implementation
 */

#include "page.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define GROWTH_FACTOR 2
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

/**
 * @brief FNV-1a hash for checksums
 */
static uint64_t fnv1a_hash(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = FNV_OFFSET;
    
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

/**
 * @brief Get current timestamp in microseconds
 */
static uint64_t get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief Hash function for page cache
 */
static size_t page_hash(uint32_t page_id, size_t hash_size) {
    return page_id % hash_size;
}

/* ============================================================================
 * Page Operations
 * ============================================================================ */

Page* page_create(uint32_t page_id, uint32_t track_id) {
    Page *page = (Page *)calloc(1, sizeof(Page));
    if (!page) return NULL;
    
    page->header.page_id = page_id;
    page->header.track_id = track_id;
    page->header.object_count = 0;
    page->header.flags = PAGE_STATUS_ALLOCATED;
    page->header.extent = mbr_empty();
    page->header.centroid = point_create(0, 0);
    page->header.checksum = 0;
    
    page->object_capacity = MAX_OBJECTS_PER_PAGE;
    page->objects = (SpatialObject *)calloc(page->object_capacity, sizeof(SpatialObject));
    if (!page->objects) {
        free(page);
        return NULL;
    }
    
    page->raw_data = NULL;
    page->in_memory = true;
    
    return page;
}

void page_free(Page *page) {
    if (!page) return;
    
    /* Free spatial objects */
    if (page->objects) {
        for (size_t i = 0; i < page->header.object_count; i++) {
            spatial_object_free(&page->objects[i]);
        }
        free(page->objects);
    }
    
    free(page->raw_data);
    free(page);
}

int page_add_object(Page *page, const SpatialObject *obj) {
    if (!page || !obj) return PAGE_ERR_NULL_PTR;
    
    if (page->header.object_count >= page->object_capacity) {
        return PAGE_ERR_FULL;
    }
    
    /* Deep copy the object */
    int err = spatial_object_copy(&page->objects[page->header.object_count], obj);
    if (err != GEOM_OK) return PAGE_ERR_ALLOC;
    
    page->header.object_count++;
    page->header.flags |= PAGE_STATUS_DIRTY;
    
    /* Update extent */
    mbr_expand_mbr(&page->header.extent, &obj->mbr);
    
    /* Check if full */
    if (page->header.object_count >= page->object_capacity) {
        page->header.flags |= PAGE_STATUS_FULL;
    }
    
    return PAGE_OK;
}

int page_remove_object(Page *page, uint64_t object_id) {
    if (!page) return PAGE_ERR_NULL_PTR;
    
    for (size_t i = 0; i < page->header.object_count; i++) {
        if (page->objects[i].id == object_id) {
            /* Free object resources */
            spatial_object_free(&page->objects[i]);
            
            /* Shift remaining objects */
            memmove(&page->objects[i], &page->objects[i + 1],
                    (page->header.object_count - i - 1) * sizeof(SpatialObject));
            
            page->header.object_count--;
            page->header.flags |= PAGE_STATUS_DIRTY;
            page->header.flags &= ~PAGE_STATUS_FULL;
            
            /* Recalculate extent */
            page_update_derived(page);
            
            return PAGE_OK;
        }
    }
    
    return PAGE_ERR_NOT_FOUND;
}

SpatialObject* page_find_object(Page *page, uint64_t object_id) {
    if (!page) return NULL;
    
    for (size_t i = 0; i < page->header.object_count; i++) {
        if (page->objects[i].id == object_id) {
            return &page->objects[i];
        }
    }
    
    return NULL;
}

void page_update_derived(Page *page) {
    if (!page) return;
    
    page->header.extent = mbr_empty();
    double cx = 0, cy = 0;
    
    for (size_t i = 0; i < page->header.object_count; i++) {
        mbr_expand_mbr(&page->header.extent, &page->objects[i].mbr);
        cx += page->objects[i].centroid.x;
        cy += page->objects[i].centroid.y;
    }
    
    if (page->header.object_count > 0) {
        page->header.centroid.x = cx / page->header.object_count;
        page->header.centroid.y = cy / page->header.object_count;
    }
    
    page->header.checksum = page_checksum(page);
}

bool page_is_full(const Page *page) {
    if (!page) return true;
    return page->header.object_count >= page->object_capacity;
}

double page_utilization(const Page *page) {
    if (!page || page->object_capacity == 0) return 0.0;
    return (double)page->header.object_count / page->object_capacity;
}

int page_serialize(const Page *page, uint8_t *buffer, size_t buffer_size) {
    if (!page || !buffer) return PAGE_ERR_NULL_PTR;
    if (buffer_size < PAGE_SIZE) return PAGE_ERR_ALLOC;
    
    memset(buffer, 0, buffer_size);
    
    /* Write header */
    memcpy(buffer, &page->header, sizeof(PageHeader));
    
    /* Write object count */
    size_t offset = sizeof(PageHeader);
    
    /* For each object, serialize basic info */
    for (size_t i = 0; i < page->header.object_count; i++) {
        const SpatialObject *obj = &page->objects[i];
        
        /* Object ID */
        memcpy(buffer + offset, &obj->id, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        
        /* Object type */
        memcpy(buffer + offset, &obj->type, sizeof(GeomType));
        offset += sizeof(GeomType);
        
        /* Centroid */
        memcpy(buffer + offset, &obj->centroid, sizeof(Point));
        offset += sizeof(Point);
        
        /* MBR */
        memcpy(buffer + offset, &obj->mbr, sizeof(MBR));
        offset += sizeof(MBR);
        
        /* Note: Full geometry serialization would be more complex */
        /* This is a simplified version for the prototype */
    }
    
    return PAGE_OK;
}

int page_deserialize(Page *page, const uint8_t *buffer, size_t buffer_size) {
    if (!page || !buffer) return PAGE_ERR_NULL_PTR;
    if (buffer_size < PAGE_SIZE) return PAGE_ERR_CORRUPT;
    
    /* Read header */
    memcpy(&page->header, buffer, sizeof(PageHeader));
    
    /* Verify basic sanity */
    if (page->header.object_count > MAX_OBJECTS_PER_PAGE) {
        return PAGE_ERR_CORRUPT;
    }
    
    size_t offset = sizeof(PageHeader);
    
    /* Read objects */
    for (size_t i = 0; i < page->header.object_count; i++) {
        SpatialObject *obj = &page->objects[i];
        
        /* Object ID */
        memcpy(&obj->id, buffer + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        
        /* Object type */
        memcpy(&obj->type, buffer + offset, sizeof(GeomType));
        offset += sizeof(GeomType);
        
        /* Centroid */
        memcpy(&obj->centroid, buffer + offset, sizeof(Point));
        offset += sizeof(Point);
        
        /* MBR */
        memcpy(&obj->mbr, buffer + offset, sizeof(MBR));
        offset += sizeof(MBR);
    }
    
    page->in_memory = true;
    
    return PAGE_OK;
}

uint64_t page_checksum(const Page *page) {
    if (!page) return 0;
    
    uint64_t hash = FNV_OFFSET;
    
    /* Hash header (excluding checksum field) */
    hash = fnv1a_hash(&page->header.page_id, sizeof(uint32_t));
    hash ^= fnv1a_hash(&page->header.track_id, sizeof(uint32_t));
    hash ^= fnv1a_hash(&page->header.object_count, sizeof(uint32_t));
    
    /* Hash objects */
    for (size_t i = 0; i < page->header.object_count; i++) {
        hash ^= fnv1a_hash(&page->objects[i].id, sizeof(uint64_t));
        hash ^= fnv1a_hash(&page->objects[i].centroid, sizeof(Point));
    }
    
    return hash;
}

bool page_verify(const Page *page) {
    if (!page) return false;
    return page->header.checksum == page_checksum(page);
}

/* ============================================================================
 * Track Operations
 * ============================================================================ */

DiskTrack* track_create(uint32_t track_id) {
    DiskTrack *track = (DiskTrack *)calloc(1, sizeof(DiskTrack));
    if (!track) return NULL;
    
    track->track_id = track_id;
    track->page_capacity = PAGES_PER_TRACK;
    track->pages = (Page **)calloc(track->page_capacity, sizeof(Page *));
    
    if (!track->pages) {
        free(track);
        return NULL;
    }
    
    track->page_count = 0;
    track->extent = mbr_empty();
    track->centroid = point_create(0, 0);
    track->is_full = false;
    
    return track;
}

void track_free(DiskTrack *track) {
    if (!track) return;
    
    /* Note: Does not free pages themselves */
    free(track->pages);
    free(track);
}

int track_add_page(DiskTrack *track, Page *page) {
    if (!track || !page) return PAGE_ERR_NULL_PTR;
    
    if (track->page_count >= track->page_capacity) {
        return PAGE_ERR_FULL;
    }
    
    track->pages[track->page_count++] = page;
    page->header.track_id = track->track_id;
    
    if (track->page_count >= track->page_capacity) {
        track->is_full = true;
    }
    
    track_update_derived(track);
    
    return PAGE_OK;
}

int track_remove_page(DiskTrack *track, uint32_t page_id) {
    if (!track) return PAGE_ERR_NULL_PTR;
    
    for (size_t i = 0; i < track->page_count; i++) {
        if (track->pages[i]->header.page_id == page_id) {
            /* Shift remaining pages */
            memmove(&track->pages[i], &track->pages[i + 1],
                    (track->page_count - i - 1) * sizeof(Page *));
            
            track->page_count--;
            track->is_full = false;
            track_update_derived(track);
            
            return PAGE_OK;
        }
    }
    
    return PAGE_ERR_NOT_FOUND;
}

Page* track_find_page(DiskTrack *track, uint32_t page_id) {
    if (!track) return NULL;
    
    for (size_t i = 0; i < track->page_count; i++) {
        if (track->pages[i]->header.page_id == page_id) {
            return track->pages[i];
        }
    }
    
    return NULL;
}

void track_update_derived(DiskTrack *track) {
    if (!track) return;
    
    track->extent = mbr_empty();
    double cx = 0, cy = 0;
    size_t valid_pages = 0;
    
    for (size_t i = 0; i < track->page_count; i++) {
        if (track->pages[i]) {
            mbr_expand_mbr(&track->extent, &track->pages[i]->header.extent);
            if (!mbr_is_empty(&track->pages[i]->header.extent)) {
                cx += track->pages[i]->header.centroid.x;
                cy += track->pages[i]->header.centroid.y;
                valid_pages++;
            }
        }
    }
    
    if (valid_pages > 0) {
        track->centroid.x = cx / valid_pages;
        track->centroid.y = cy / valid_pages;
    }
}

bool track_has_space(const DiskTrack *track) {
    if (!track) return false;
    return track->page_count < track->page_capacity;
}

size_t track_object_count(const DiskTrack *track) {
    if (!track) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < track->page_count; i++) {
        if (track->pages[i]) {
            count += track->pages[i]->header.object_count;
        }
    }
    
    return count;
}

/* ============================================================================
 * Page Pool Operations
 * ============================================================================ */

int page_pool_init(PagePool *pool) {
    if (!pool) return PAGE_ERR_NULL_PTR;
    
    memset(pool, 0, sizeof(PagePool));
    
    pool->page_capacity = 256;
    pool->pages = (Page **)calloc(pool->page_capacity, sizeof(Page *));
    if (!pool->pages) return PAGE_ERR_ALLOC;
    
    pool->track_capacity = 64;
    pool->tracks = (DiskTrack **)calloc(pool->track_capacity, sizeof(DiskTrack *));
    if (!pool->tracks) {
        free(pool->pages);
        return PAGE_ERR_ALLOC;
    }
    
    pool->next_page_id = 1;
    pool->next_track_id = 1;
    
    return PAGE_OK;
}

void page_pool_free(PagePool *pool) {
    if (!pool) return;
    
    /* Free all pages */
    for (size_t i = 0; i < pool->page_count; i++) {
        page_free(pool->pages[i]);
    }
    free(pool->pages);
    
    /* Free all tracks */
    for (size_t i = 0; i < pool->track_count; i++) {
        track_free(pool->tracks[i]);
    }
    free(pool->tracks);
    
    memset(pool, 0, sizeof(PagePool));
}

Page* page_pool_alloc(PagePool *pool, uint32_t track_id) {
    if (!pool) return NULL;
    
    /* Grow pages array if needed */
    if (pool->page_count >= pool->page_capacity) {
        size_t new_cap = pool->page_capacity * GROWTH_FACTOR;
        Page **new_pages = (Page **)realloc(pool->pages, new_cap * sizeof(Page *));
        if (!new_pages) return NULL;
        pool->pages = new_pages;
        pool->page_capacity = new_cap;
    }
    
    /* Create new page */
    Page *page = page_create(pool->next_page_id++, track_id);
    if (!page) return NULL;
    
    pool->pages[pool->page_count++] = page;
    
    /* Add to track if specified */
    if (track_id > 0) {
        DiskTrack *track = page_pool_get_track(pool, track_id);
        if (track) {
            track_add_page(track, page);
        }
    }
    
    return page;
}

int page_pool_free_page(PagePool *pool, uint32_t page_id) {
    if (!pool) return PAGE_ERR_NULL_PTR;
    
    for (size_t i = 0; i < pool->page_count; i++) {
        if (pool->pages[i]->header.page_id == page_id) {
            /* Remove from track */
            uint32_t track_id = pool->pages[i]->header.track_id;
            if (track_id > 0) {
                DiskTrack *track = page_pool_get_track(pool, track_id);
                if (track) {
                    track_remove_page(track, page_id);
                }
            }
            
            /* Free the page */
            page_free(pool->pages[i]);
            
            /* Shift remaining pages */
            memmove(&pool->pages[i], &pool->pages[i + 1],
                    (pool->page_count - i - 1) * sizeof(Page *));
            pool->page_count--;
            
            return PAGE_OK;
        }
    }
    
    return PAGE_ERR_NOT_FOUND;
}

Page* page_pool_get(PagePool *pool, uint32_t page_id) {
    if (!pool) return NULL;
    
    for (size_t i = 0; i < pool->page_count; i++) {
        if (pool->pages[i]->header.page_id == page_id) {
            return pool->pages[i];
        }
    }
    
    return NULL;
}

DiskTrack* page_pool_create_track(PagePool *pool) {
    if (!pool) return NULL;
    
    /* Grow tracks array if needed */
    if (pool->track_count >= pool->track_capacity) {
        size_t new_cap = pool->track_capacity * GROWTH_FACTOR;
        DiskTrack **new_tracks = (DiskTrack **)realloc(pool->tracks, 
                                                        new_cap * sizeof(DiskTrack *));
        if (!new_tracks) return NULL;
        pool->tracks = new_tracks;
        pool->track_capacity = new_cap;
    }
    
    DiskTrack *track = track_create(pool->next_track_id++);
    if (!track) return NULL;
    
    pool->tracks[pool->track_count++] = track;
    
    return track;
}

DiskTrack* page_pool_get_track(PagePool *pool, uint32_t track_id) {
    if (!pool) return NULL;
    
    for (size_t i = 0; i < pool->track_count; i++) {
        if (pool->tracks[i]->track_id == track_id) {
            return pool->tracks[i];
        }
    }
    
    return NULL;
}

int page_pool_query_region(PagePool *pool, const MBR *region,
                           Page ***pages, size_t *count) {
    if (!pool || !region || !pages || !count) return PAGE_ERR_NULL_PTR;
    
    *pages = NULL;
    *count = 0;
    
    /* First pass: count matching pages */
    size_t matching = 0;
    for (size_t i = 0; i < pool->page_count; i++) {
        if (mbr_intersects(&pool->pages[i]->header.extent, region)) {
            matching++;
        }
    }
    
    if (matching == 0) return PAGE_OK;
    
    /* Allocate result array */
    *pages = (Page **)malloc(matching * sizeof(Page *));
    if (!*pages) return PAGE_ERR_ALLOC;
    
    /* Second pass: collect matching pages */
    size_t idx = 0;
    for (size_t i = 0; i < pool->page_count; i++) {
        if (mbr_intersects(&pool->pages[i]->header.extent, region)) {
            (*pages)[idx++] = pool->pages[i];
        }
    }
    
    *count = idx;
    return PAGE_OK;
}

void page_pool_stats(const PagePool *pool, size_t *total_pages,
                     size_t *total_tracks, size_t *total_objects) {
    if (!pool) return;
    
    if (total_pages) *total_pages = pool->page_count;
    if (total_tracks) *total_tracks = pool->track_count;
    
    if (total_objects) {
        *total_objects = 0;
        for (size_t i = 0; i < pool->page_count; i++) {
            *total_objects += pool->pages[i]->header.object_count;
        }
    }
}

/* ============================================================================
 * Page Cache Operations
 * ============================================================================ */

int page_cache_init(PageCache *cache, PagePool *pool, size_t capacity) {
    if (!cache || !pool) return PAGE_ERR_NULL_PTR;
    
    memset(cache, 0, sizeof(PageCache));
    
    cache->pool = pool;
    cache->capacity = capacity > 0 ? capacity : 64;
    cache->hash_size = cache->capacity * 2;
    
    cache->hash_table = (PageRef **)calloc(cache->hash_size, sizeof(PageRef *));
    if (!cache->hash_table) return PAGE_ERR_ALLOC;
    
    return PAGE_OK;
}

void page_cache_free(PageCache *cache) {
    if (!cache) return;
    
    /* Free all page refs */
    PageRef *ref = cache->head;
    while (ref) {
        PageRef *next = ref->next;
        free(ref);
        ref = next;
    }
    
    free(cache->hash_table);
    memset(cache, 0, sizeof(PageCache));
}

Page* page_cache_get(PageCache *cache, uint32_t page_id) {
    if (!cache) return NULL;
    
    /* Look up in hash table */
    size_t idx = page_hash(page_id, cache->hash_size);
    PageRef *ref = cache->hash_table[idx];
    
    while (ref) {
        if (ref->page_id == page_id) {
            /* Move to front (MRU) */
            if (ref != cache->head) {
                /* Remove from current position */
                if (ref->prev) ref->prev->next = ref->next;
                if (ref->next) ref->next->prev = ref->prev;
                if (ref == cache->tail) cache->tail = ref->prev;
                
                /* Insert at head */
                ref->prev = NULL;
                ref->next = cache->head;
                if (cache->head) cache->head->prev = ref;
                cache->head = ref;
            }
            
            ref->access_count++;
            ref->last_access = get_timestamp();
            
            return page_pool_get(cache->pool, page_id);
        }
        ref = ref->next;
    }
    
    /* Not in cache - get from pool and add to cache */
    Page *page = page_pool_get(cache->pool, page_id);
    if (!page) return NULL;
    
    /* Evict if at capacity */
    if (cache->count >= cache->capacity) {
        page_cache_evict(cache, 1);
    }
    
    /* Create new ref */
    ref = (PageRef *)calloc(1, sizeof(PageRef));
    if (!ref) return page;  /* Return page anyway */
    
    ref->page_id = page_id;
    ref->access_count = 1;
    ref->last_access = get_timestamp();
    
    /* Add to hash table */
    ref->next = cache->hash_table[idx];
    cache->hash_table[idx] = ref;
    
    /* Add to LRU list head */
    ref->next = cache->head;
    ref->prev = NULL;
    if (cache->head) cache->head->prev = ref;
    cache->head = ref;
    if (!cache->tail) cache->tail = ref;
    
    cache->count++;
    
    return page;
}

int page_cache_pin(PageCache *cache, uint32_t page_id) {
    if (!cache) return PAGE_ERR_NULL_PTR;
    
    Page *page = page_pool_get(cache->pool, page_id);
    if (!page) return PAGE_ERR_NOT_FOUND;
    
    page->header.flags |= PAGE_STATUS_PINNED;
    return PAGE_OK;
}

int page_cache_unpin(PageCache *cache, uint32_t page_id) {
    if (!cache) return PAGE_ERR_NULL_PTR;
    
    Page *page = page_pool_get(cache->pool, page_id);
    if (!page) return PAGE_ERR_NOT_FOUND;
    
    page->header.flags &= ~PAGE_STATUS_PINNED;
    return PAGE_OK;
}

int page_cache_mark_dirty(PageCache *cache, uint32_t page_id) {
    if (!cache) return PAGE_ERR_NULL_PTR;
    
    Page *page = page_pool_get(cache->pool, page_id);
    if (!page) return PAGE_ERR_NOT_FOUND;
    
    page->header.flags |= PAGE_STATUS_DIRTY;
    return PAGE_OK;
}

int page_cache_flush(PageCache *cache) {
    if (!cache || !cache->pool) return PAGE_ERR_NULL_PTR;
    
    for (size_t i = 0; i < cache->pool->page_count; i++) {
        Page *page = cache->pool->pages[i];
        if (page->header.flags & PAGE_STATUS_DIRTY) {
            /* In a real implementation, write to disk here */
            page->header.flags &= ~PAGE_STATUS_DIRTY;
        }
    }
    
    return PAGE_OK;
}

int page_cache_evict(PageCache *cache, size_t count) {
    if (!cache) return PAGE_ERR_NULL_PTR;
    
    size_t evicted = 0;
    
    while (evicted < count && cache->tail) {
        PageRef *victim = cache->tail;
        
        /* Skip pinned pages */
        Page *page = page_pool_get(cache->pool, victim->page_id);
        if (page && (page->header.flags & PAGE_STATUS_PINNED)) {
            victim = victim->prev;
            continue;
        }
        
        /* Remove from LRU list */
        if (victim->prev) victim->prev->next = victim->next;
        if (victim->next) victim->next->prev = victim->prev;
        if (victim == cache->head) cache->head = victim->next;
        if (victim == cache->tail) cache->tail = victim->prev;
        
        /* Remove from hash table */
        size_t idx = page_hash(victim->page_id, cache->hash_size);
        PageRef **pp = &cache->hash_table[idx];
        while (*pp) {
            if (*pp == victim) {
                *pp = victim->next;
                break;
            }
            pp = &(*pp)->next;
        }
        
        free(victim);
        cache->count--;
        evicted++;
    }
    
    return PAGE_OK;
}

double page_cache_hit_rate(const PageCache *cache) {
    if (!cache || cache->count == 0) return 0.0;
    
    uint64_t total_accesses = 0;
    PageRef *ref = cache->head;
    while (ref) {
        total_accesses += ref->access_count;
        ref = ref->next;
    }
    
    /* Approximate hit rate based on reuse */
    if (total_accesses == 0) return 0.0;
    return (double)(total_accesses - cache->count) / total_accesses;
}

