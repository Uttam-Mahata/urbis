/**
 * @file disk_manager.h
 * @brief Disk I/O and track-aware page allocation manager
 * 
 * Manages disk I/O operations with spatial awareness, using KD-tree
 * for intelligent page allocation that minimizes disk seeks during
 * spatial queries.
 */

#ifndef URBIS_DISK_MANAGER_H
#define URBIS_DISK_MANAGER_H

#include "geometry.h"
#include "page.h"
#include "kdtree.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define DM_DEFAULT_CACHE_SIZE 128     /**< Default page cache size */
#define DM_MAGIC 0x55524249           /**< "URBI" magic number */
#define DM_VERSION 1                  /**< File format version */

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief File header for persisted index
 */
typedef struct {
    uint32_t magic;                   /**< Magic number for validation */
    uint32_t version;                 /**< File format version */
    uint32_t page_count;              /**< Total number of pages */
    uint32_t track_count;             /**< Total number of tracks */
    uint64_t object_count;            /**< Total spatial objects */
    MBR bounds;                       /**< Overall spatial bounds */
    uint64_t created_time;            /**< Creation timestamp */
    uint64_t modified_time;           /**< Last modification timestamp */
    uint32_t page_size;               /**< Page size used */
    uint32_t pages_per_track;         /**< Pages per track */
    uint64_t index_offset;            /**< Offset to index data */
    uint64_t data_offset;             /**< Offset to page data */
    uint8_t reserved[64];             /**< Reserved for future use */
} DiskFileHeader;

/**
 * @brief Allocation strategy for new pages
 */
typedef enum {
    ALLOC_NEAREST_TRACK,              /**< Allocate to nearest existing track */
    ALLOC_NEW_TRACK,                  /**< Always create new track */
    ALLOC_BEST_FIT,                   /**< Find best fitting track */
    ALLOC_SEQUENTIAL                  /**< Sequential allocation */
} AllocationStrategy;

/**
 * @brief Disk manager configuration
 */
typedef struct {
    size_t cache_size;                /**< Number of pages to cache */
    size_t page_size;                 /**< Page size in bytes */
    size_t pages_per_track;           /**< Pages per disk track */
    AllocationStrategy strategy;      /**< Page allocation strategy */
    bool use_mmap;                    /**< Use memory-mapped I/O */
    bool sync_on_write;               /**< Sync to disk on every write */
} DiskManagerConfig;

/**
 * @brief I/O statistics
 */
typedef struct {
    uint64_t pages_read;              /**< Total pages read */
    uint64_t pages_written;           /**< Total pages written */
    uint64_t cache_hits;              /**< Cache hits */
    uint64_t cache_misses;            /**< Cache misses */
    uint64_t seeks;                   /**< Estimated disk seeks */
    uint64_t bytes_read;              /**< Total bytes read */
    uint64_t bytes_written;           /**< Total bytes written */
} IOStats;

/**
 * @brief Disk manager structure
 */
typedef struct {
    DiskManagerConfig config;
    DiskFileHeader header;
    PagePool pool;                    /**< Page pool */
    PageCache cache;                  /**< Page cache */
    KDTree allocation_tree;           /**< KD-tree for page allocation */
    FILE *data_file;                  /**< Data file handle */
    char *file_path;                  /**< Path to data file */
    void *mmap_base;                  /**< Memory-mapped region base */
    size_t mmap_size;                 /**< Memory-mapped region size */
    IOStats stats;                    /**< I/O statistics */
    bool is_open;                     /**< True if file is open */
    bool is_dirty;                    /**< True if uncommitted changes */
} DiskManager;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    DM_OK = 0,
    DM_ERR_NULL_PTR = -1,
    DM_ERR_ALLOC = -2,
    DM_ERR_IO = -3,
    DM_ERR_NOT_OPEN = -4,
    DM_ERR_CORRUPT = -5,
    DM_ERR_VERSION = -6,
    DM_ERR_FULL = -7,
    DM_ERR_NOT_FOUND = -8
} DiskManagerError;

/* ============================================================================
 * Disk Manager Operations
 * ============================================================================ */

/**
 * @brief Get default configuration
 */
DiskManagerConfig disk_manager_default_config(void);

/**
 * @brief Initialize disk manager
 */
int disk_manager_init(DiskManager *dm, const DiskManagerConfig *config);

/**
 * @brief Free disk manager resources
 */
void disk_manager_free(DiskManager *dm);

/**
 * @brief Create a new data file
 */
int disk_manager_create(DiskManager *dm, const char *path);

/**
 * @brief Open an existing data file
 */
int disk_manager_open(DiskManager *dm, const char *path);

/**
 * @brief Close the data file
 */
int disk_manager_close(DiskManager *dm);

/**
 * @brief Sync all changes to disk
 */
int disk_manager_sync(DiskManager *dm);

/**
 * @brief Allocate a new page using spatial-aware allocation
 * @param dm Disk manager
 * @param centroid Centroid of data to be stored in page
 * @return Pointer to allocated page, or NULL on error
 */
Page* disk_manager_alloc_page(DiskManager *dm, Point centroid);

/**
 * @brief Free a page
 */
int disk_manager_free_page(DiskManager *dm, uint32_t page_id);

/**
 * @brief Get a page by ID (loads from disk if needed)
 */
Page* disk_manager_get_page(DiskManager *dm, uint32_t page_id);

/**
 * @brief Write a page to disk
 */
int disk_manager_write_page(DiskManager *dm, Page *page);

/**
 * @brief Find the best track for a page based on centroid
 */
DiskTrack* disk_manager_find_best_track(DiskManager *dm, Point centroid);

/**
 * @brief Get pages in a spatial region
 */
int disk_manager_query_region(DiskManager *dm, const MBR *region,
                               Page ***pages, size_t *count);

/**
 * @brief Get I/O statistics
 */
void disk_manager_get_stats(const DiskManager *dm, IOStats *stats);

/**
 * @brief Reset I/O statistics
 */
void disk_manager_reset_stats(DiskManager *dm);

/**
 * @brief Estimate seeks for a sequence of page accesses
 */
uint64_t disk_manager_estimate_seeks(const DiskManager *dm,
                                      const uint32_t *page_ids, size_t count);

/**
 * @brief Optimize page layout for spatial locality
 */
int disk_manager_optimize(DiskManager *dm);

/**
 * @brief Rebuild allocation tree after bulk operations
 */
int disk_manager_rebuild_allocation_tree(DiskManager *dm);

/* ============================================================================
 * Track Management
 * ============================================================================ */

/**
 * @brief Create a new track
 */
DiskTrack* disk_manager_create_track(DiskManager *dm);

/**
 * @brief Get track by ID
 */
DiskTrack* disk_manager_get_track(DiskManager *dm, uint32_t track_id);

/**
 * @brief Get all tracks intersecting a region
 */
int disk_manager_query_tracks(DiskManager *dm, const MBR *region,
                               DiskTrack ***tracks, size_t *count);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get data file size
 */
size_t disk_manager_file_size(const DiskManager *dm);

/**
 * @brief Check if data file exists
 */
bool disk_manager_file_exists(const char *path);

/**
 * @brief Validate data file integrity
 */
int disk_manager_validate(DiskManager *dm);

/**
 * @brief Print disk manager statistics
 */
void disk_manager_print_stats(const DiskManager *dm, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_DISK_MANAGER_H */

