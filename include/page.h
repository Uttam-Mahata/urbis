/**
 * @file page.h
 * @brief Page and track structures for disk-aware spatial indexing
 * 
 * Provides page management structures that map to disk tracks,
 * enabling efficient spatial queries by minimizing disk seeks.
 */

#ifndef URBIS_PAGE_H
#define URBIS_PAGE_H

#include "geometry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define PAGE_SIZE 4096            /**< Default page size in bytes */
#define PAGES_PER_TRACK 16        /**< Number of pages per disk track */
#define MAX_OBJECTS_PER_PAGE 64   /**< Maximum spatial objects per page */
#define PAGE_HEADER_SIZE 64       /**< Header size in each page */

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Page status flags
 */
typedef enum {
    PAGE_STATUS_FREE = 0,         /**< Page is unallocated */
    PAGE_STATUS_ALLOCATED = 1,    /**< Page is allocated but not full */
    PAGE_STATUS_FULL = 2,         /**< Page is full */
    PAGE_STATUS_DIRTY = 4,        /**< Page has unsaved modifications */
    PAGE_STATUS_PINNED = 8        /**< Page is pinned in cache */
} PageStatus;

/**
 * @brief Page header stored at start of each page
 */
typedef struct {
    uint32_t page_id;             /**< Unique page identifier */
    uint32_t track_id;            /**< Track this page belongs to */
    uint32_t object_count;        /**< Number of objects in page */
    uint32_t flags;               /**< Status flags */
    MBR extent;                   /**< Spatial extent of page contents */
    Point centroid;               /**< Centroid of page (for allocation) */
    uint64_t checksum;            /**< Data integrity checksum */
} PageHeader;

/**
 * @brief Page structure representing a disk page
 */
typedef struct {
    PageHeader header;
    SpatialObject *objects;       /**< Array of spatial objects */
    size_t object_capacity;       /**< Capacity of objects array */
    uint8_t *raw_data;            /**< Raw page data for disk I/O */
    bool in_memory;               /**< True if loaded in memory */
} Page;

/**
 * @brief Track structure representing a disk track (group of pages)
 */
typedef struct {
    uint32_t track_id;            /**< Unique track identifier */
    Page **pages;                 /**< Array of pages in this track */
    size_t page_count;            /**< Number of allocated pages */
    size_t page_capacity;         /**< Capacity of pages array */
    MBR extent;                   /**< Spatial extent of track */
    Point centroid;               /**< Centroid of track */
    bool is_full;                 /**< True if track is at capacity */
} DiskTrack;

/**
 * @brief Page pool for managing all pages
 */
typedef struct {
    Page **pages;                 /**< Array of all pages */
    size_t page_count;            /**< Total number of pages */
    size_t page_capacity;         /**< Capacity of pages array */
    DiskTrack **tracks;           /**< Array of all tracks */
    size_t track_count;           /**< Number of tracks */
    size_t track_capacity;        /**< Capacity of tracks array */
    uint32_t next_page_id;        /**< Next available page ID */
    uint32_t next_track_id;       /**< Next available track ID */
} PagePool;

/**
 * @brief Page reference for tracking page usage
 */
typedef struct PageRef {
    uint32_t page_id;
    uint32_t access_count;
    uint64_t last_access;
    struct PageRef *next;
    struct PageRef *prev;
} PageRef;

/**
 * @brief LRU cache for pages
 */
typedef struct {
    PageRef *head;                /**< Most recently used */
    PageRef *tail;                /**< Least recently used */
    PageRef **hash_table;         /**< Hash table for O(1) lookup */
    size_t hash_size;
    size_t count;
    size_t capacity;
    PagePool *pool;               /**< Reference to page pool */
} PageCache;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    PAGE_OK = 0,
    PAGE_ERR_NULL_PTR = -1,
    PAGE_ERR_ALLOC = -2,
    PAGE_ERR_FULL = -3,
    PAGE_ERR_NOT_FOUND = -4,
    PAGE_ERR_IO = -5,
    PAGE_ERR_CORRUPT = -6,
    PAGE_ERR_INVALID_ID = -7
} PageError;

/* ============================================================================
 * Page Operations
 * ============================================================================ */

/**
 * @brief Create a new page
 */
Page* page_create(uint32_t page_id, uint32_t track_id);

/**
 * @brief Free page resources
 */
void page_free(Page *page);

/**
 * @brief Add a spatial object to a page
 */
int page_add_object(Page *page, const SpatialObject *obj);

/**
 * @brief Remove an object from a page by ID
 */
int page_remove_object(Page *page, uint64_t object_id);

/**
 * @brief Find an object in a page by ID
 */
SpatialObject* page_find_object(Page *page, uint64_t object_id);

/**
 * @brief Update page extent and centroid after modifications
 */
void page_update_derived(Page *page);

/**
 * @brief Check if page is full
 */
bool page_is_full(const Page *page);

/**
 * @brief Get page utilization (0.0 to 1.0)
 */
double page_utilization(const Page *page);

/**
 * @brief Serialize page to raw bytes for disk I/O
 */
int page_serialize(const Page *page, uint8_t *buffer, size_t buffer_size);

/**
 * @brief Deserialize page from raw bytes
 */
int page_deserialize(Page *page, const uint8_t *buffer, size_t buffer_size);

/**
 * @brief Calculate page checksum
 */
uint64_t page_checksum(const Page *page);

/**
 * @brief Verify page integrity
 */
bool page_verify(const Page *page);

/* ============================================================================
 * Track Operations
 * ============================================================================ */

/**
 * @brief Create a new track
 */
DiskTrack* track_create(uint32_t track_id);

/**
 * @brief Free track resources (does not free pages)
 */
void track_free(DiskTrack *track);

/**
 * @brief Add a page to a track
 */
int track_add_page(DiskTrack *track, Page *page);

/**
 * @brief Remove a page from a track
 */
int track_remove_page(DiskTrack *track, uint32_t page_id);

/**
 * @brief Find a page in a track
 */
Page* track_find_page(DiskTrack *track, uint32_t page_id);

/**
 * @brief Update track extent and centroid
 */
void track_update_derived(DiskTrack *track);

/**
 * @brief Check if track can accept more pages
 */
bool track_has_space(const DiskTrack *track);

/**
 * @brief Get number of objects across all pages in track
 */
size_t track_object_count(const DiskTrack *track);

/* ============================================================================
 * Page Pool Operations
 * ============================================================================ */

/**
 * @brief Initialize a page pool
 */
int page_pool_init(PagePool *pool);

/**
 * @brief Free page pool resources
 */
void page_pool_free(PagePool *pool);

/**
 * @brief Allocate a new page in the pool
 * @param pool The page pool
 * @param track_id Track to assign page to (or 0 for unassigned)
 * @return Pointer to new page, or NULL on error
 */
Page* page_pool_alloc(PagePool *pool, uint32_t track_id);

/**
 * @brief Free a page back to the pool
 */
int page_pool_free_page(PagePool *pool, uint32_t page_id);

/**
 * @brief Get a page by ID
 */
Page* page_pool_get(PagePool *pool, uint32_t page_id);

/**
 * @brief Create a new track in the pool
 */
DiskTrack* page_pool_create_track(PagePool *pool);

/**
 * @brief Get a track by ID
 */
DiskTrack* page_pool_get_track(PagePool *pool, uint32_t track_id);

/**
 * @brief Get all pages with objects intersecting a region
 */
int page_pool_query_region(PagePool *pool, const MBR *region, 
                           Page ***pages, size_t *count);

/**
 * @brief Get pool statistics
 */
void page_pool_stats(const PagePool *pool, size_t *total_pages, 
                     size_t *total_tracks, size_t *total_objects);

/* ============================================================================
 * Page Cache Operations
 * ============================================================================ */

/**
 * @brief Initialize a page cache
 */
int page_cache_init(PageCache *cache, PagePool *pool, size_t capacity);

/**
 * @brief Free cache resources
 */
void page_cache_free(PageCache *cache);

/**
 * @brief Get a page from cache (loads if not present)
 */
Page* page_cache_get(PageCache *cache, uint32_t page_id);

/**
 * @brief Pin a page in cache (prevents eviction)
 */
int page_cache_pin(PageCache *cache, uint32_t page_id);

/**
 * @brief Unpin a page
 */
int page_cache_unpin(PageCache *cache, uint32_t page_id);

/**
 * @brief Mark a page as dirty
 */
int page_cache_mark_dirty(PageCache *cache, uint32_t page_id);

/**
 * @brief Flush dirty pages to disk
 */
int page_cache_flush(PageCache *cache);

/**
 * @brief Evict least recently used pages to make room
 */
int page_cache_evict(PageCache *cache, size_t count);

/**
 * @brief Get cache hit rate
 */
double page_cache_hit_rate(const PageCache *cache);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_PAGE_H */

