/**
 * @file disk_manager.c
 * @brief Disk I/O and track-aware page allocation implementation
 */

#include "disk_manager.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * @brief Get current time in seconds
 */
static uint64_t get_current_time(void) {
    return (uint64_t)time(NULL);
}

/**
 * @brief Calculate file offset for a page
 */
static size_t page_file_offset(const DiskManager *dm, uint32_t page_id) {
    return dm->header.data_offset + (page_id - 1) * dm->config.page_size;
}

/**
 * @brief Read page from disk
 */
static int read_page_from_disk(DiskManager *dm, Page *page) {
    if (!dm || !page || !dm->data_file) return DM_ERR_NULL_PTR;
    
    size_t offset = page_file_offset(dm, page->header.page_id);
    
    if (fseek(dm->data_file, offset, SEEK_SET) != 0) {
        return DM_ERR_IO;
    }
    
    uint8_t buffer[PAGE_SIZE];
    if (fread(buffer, 1, dm->config.page_size, dm->data_file) != dm->config.page_size) {
        if (!feof(dm->data_file)) {
            return DM_ERR_IO;
        }
        /* New page - initialize empty */
        memset(buffer, 0, sizeof(buffer));
    }
    
    int err = page_deserialize(page, buffer, dm->config.page_size);
    if (err != PAGE_OK) return DM_ERR_CORRUPT;
    
    dm->stats.pages_read++;
    dm->stats.bytes_read += dm->config.page_size;
    
    return DM_OK;
}

/**
 * @brief Write page to disk
 */
static int write_page_to_disk(DiskManager *dm, const Page *page) {
    if (!dm || !page || !dm->data_file) return DM_ERR_NULL_PTR;
    
    size_t offset = page_file_offset(dm, page->header.page_id);
    
    if (fseek(dm->data_file, offset, SEEK_SET) != 0) {
        return DM_ERR_IO;
    }
    
    uint8_t buffer[PAGE_SIZE];
    int err = page_serialize(page, buffer, dm->config.page_size);
    if (err != PAGE_OK) return DM_ERR_IO;
    
    if (fwrite(buffer, 1, dm->config.page_size, dm->data_file) != dm->config.page_size) {
        return DM_ERR_IO;
    }
    
    if (dm->config.sync_on_write) {
        fflush(dm->data_file);
    }
    
    dm->stats.pages_written++;
    dm->stats.bytes_written += dm->config.page_size;
    
    return DM_OK;
}

/**
 * @brief Write file header to disk
 */
static int write_header(DiskManager *dm) {
    if (!dm || !dm->data_file) return DM_ERR_NULL_PTR;
    
    if (fseek(dm->data_file, 0, SEEK_SET) != 0) {
        return DM_ERR_IO;
    }
    
    dm->header.modified_time = get_current_time();
    
    if (fwrite(&dm->header, sizeof(DiskFileHeader), 1, dm->data_file) != 1) {
        return DM_ERR_IO;
    }
    
    fflush(dm->data_file);
    
    return DM_OK;
}

/**
 * @brief Read file header from disk
 */
static int read_header(DiskManager *dm) {
    if (!dm || !dm->data_file) return DM_ERR_NULL_PTR;
    
    if (fseek(dm->data_file, 0, SEEK_SET) != 0) {
        return DM_ERR_IO;
    }
    
    if (fread(&dm->header, sizeof(DiskFileHeader), 1, dm->data_file) != 1) {
        return DM_ERR_IO;
    }
    
    /* Validate header */
    if (dm->header.magic != DM_MAGIC) {
        return DM_ERR_CORRUPT;
    }
    
    if (dm->header.version > DM_VERSION) {
        return DM_ERR_VERSION;
    }
    
    return DM_OK;
}

/**
 * @brief Update allocation tree with page centroid
 */
static int update_allocation_tree(DiskManager *dm, Page *page) {
    if (!dm || !page) return DM_ERR_NULL_PTR;
    
    /* Insert page centroid into allocation tree */
    return kdtree_insert(&dm->allocation_tree, page->header.centroid,
                         page->header.page_id, page);
}

/**
 * @brief Calculate distance between two tracks
 * Note: Reserved for future optimization passes
 */
__attribute__((unused))
static double track_distance(const DiskTrack *a, const DiskTrack *b) {
    if (!a || !b) return 0.0;
    return point_distance(&a->centroid, &b->centroid);
}

/* ============================================================================
 * Disk Manager Operations
 * ============================================================================ */

DiskManagerConfig disk_manager_default_config(void) {
    DiskManagerConfig config = {
        .cache_size = DM_DEFAULT_CACHE_SIZE,
        .page_size = PAGE_SIZE,
        .pages_per_track = PAGES_PER_TRACK,
        .strategy = ALLOC_BEST_FIT,
        .use_mmap = false,
        .sync_on_write = false
    };
    return config;
}

int disk_manager_init(DiskManager *dm, const DiskManagerConfig *config) {
    if (!dm) return DM_ERR_NULL_PTR;
    
    memset(dm, 0, sizeof(DiskManager));
    
    if (config) {
        dm->config = *config;
    } else {
        dm->config = disk_manager_default_config();
    }
    
    /* Initialize page pool */
    int err = page_pool_init(&dm->pool);
    if (err != PAGE_OK) return DM_ERR_ALLOC;
    
    /* Initialize page cache */
    err = page_cache_init(&dm->cache, &dm->pool, dm->config.cache_size);
    if (err != PAGE_OK) {
        page_pool_free(&dm->pool);
        return DM_ERR_ALLOC;
    }
    
    /* Initialize allocation tree */
    err = kdtree_init(&dm->allocation_tree);
    if (err != KD_OK) {
        page_cache_free(&dm->cache);
        page_pool_free(&dm->pool);
        return DM_ERR_ALLOC;
    }
    
    return DM_OK;
}

void disk_manager_free(DiskManager *dm) {
    if (!dm) return;
    
    disk_manager_close(dm);
    
    kdtree_free(&dm->allocation_tree);
    page_cache_free(&dm->cache);
    page_pool_free(&dm->pool);
    free(dm->file_path);
    
    memset(dm, 0, sizeof(DiskManager));
}

int disk_manager_create(DiskManager *dm, const char *path) {
    if (!dm || !path) return DM_ERR_NULL_PTR;
    
    /* Close any existing file */
    disk_manager_close(dm);
    
    /* Open file for writing */
    dm->data_file = fopen(path, "w+b");
    if (!dm->data_file) {
        return DM_ERR_IO;
    }
    
    /* Store path */
    dm->file_path = strdup(path);
    if (!dm->file_path) {
        fclose(dm->data_file);
        dm->data_file = NULL;
        return DM_ERR_ALLOC;
    }
    
    /* Initialize header */
    memset(&dm->header, 0, sizeof(DiskFileHeader));
    dm->header.magic = DM_MAGIC;
    dm->header.version = DM_VERSION;
    dm->header.page_count = 0;
    dm->header.track_count = 0;
    dm->header.object_count = 0;
    dm->header.bounds = mbr_empty();
    dm->header.created_time = get_current_time();
    dm->header.modified_time = dm->header.created_time;
    dm->header.page_size = dm->config.page_size;
    dm->header.pages_per_track = dm->config.pages_per_track;
    dm->header.index_offset = sizeof(DiskFileHeader);
    dm->header.data_offset = sizeof(DiskFileHeader) + PAGE_SIZE;  /* Reserve for index */
    
    /* Write header */
    int err = write_header(dm);
    if (err != DM_OK) {
        fclose(dm->data_file);
        dm->data_file = NULL;
        free(dm->file_path);
        dm->file_path = NULL;
        return err;
    }
    
    dm->is_open = true;
    dm->is_dirty = false;
    
    return DM_OK;
}

int disk_manager_open(DiskManager *dm, const char *path) {
    if (!dm || !path) return DM_ERR_NULL_PTR;
    
    /* Close any existing file */
    disk_manager_close(dm);
    
    /* Open file for reading and writing */
    dm->data_file = fopen(path, "r+b");
    if (!dm->data_file) {
        return DM_ERR_IO;
    }
    
    /* Store path */
    dm->file_path = strdup(path);
    if (!dm->file_path) {
        fclose(dm->data_file);
        dm->data_file = NULL;
        return DM_ERR_ALLOC;
    }
    
    /* Read header */
    int err = read_header(dm);
    if (err != DM_OK) {
        fclose(dm->data_file);
        dm->data_file = NULL;
        free(dm->file_path);
        dm->file_path = NULL;
        return err;
    }
    
    /* Load pages into pool */
    for (uint32_t i = 1; i <= dm->header.page_count; i++) {
        Page *page = page_pool_alloc(&dm->pool, 0);
        if (!page) continue;
        
        page->header.page_id = i;
        read_page_from_disk(dm, page);
        
        /* Update allocation tree */
        if (page->header.object_count > 0) {
            update_allocation_tree(dm, page);
        }
    }
    
    dm->is_open = true;
    dm->is_dirty = false;
    
    return DM_OK;
}

int disk_manager_close(DiskManager *dm) {
    if (!dm) return DM_ERR_NULL_PTR;
    
    if (!dm->is_open) return DM_OK;
    
    /* Flush dirty pages */
    disk_manager_sync(dm);
    
    /* Unmap if using mmap */
#ifdef __linux__
    if (dm->mmap_base && dm->mmap_size > 0) {
        munmap(dm->mmap_base, dm->mmap_size);
        dm->mmap_base = NULL;
        dm->mmap_size = 0;
    }
#endif
    
    /* Close file */
    if (dm->data_file) {
        fclose(dm->data_file);
        dm->data_file = NULL;
    }
    
    dm->is_open = false;
    
    return DM_OK;
}

int disk_manager_sync(DiskManager *dm) {
    if (!dm || !dm->is_open) return DM_ERR_NOT_OPEN;
    
    /* Write all dirty pages */
    for (size_t i = 0; i < dm->pool.page_count; i++) {
        Page *page = dm->pool.pages[i];
        if (page->header.flags & PAGE_STATUS_DIRTY) {
            write_page_to_disk(dm, page);
            page->header.flags &= ~PAGE_STATUS_DIRTY;
        }
    }
    
    /* Update header */
    dm->header.page_count = dm->pool.page_count;
    dm->header.track_count = dm->pool.track_count;
    
    size_t objects = 0;
    page_pool_stats(&dm->pool, NULL, NULL, &objects);
    dm->header.object_count = objects;
    
    write_header(dm);
    
    dm->is_dirty = false;
    
    return DM_OK;
}

Page* disk_manager_alloc_page(DiskManager *dm, Point centroid) {
    if (!dm) return NULL;
    
    /* Find best track for this page */
    DiskTrack *track = disk_manager_find_best_track(dm, centroid);
    
    /* Create new track if needed */
    if (!track || !track_has_space(track)) {
        track = disk_manager_create_track(dm);
        if (!track) return NULL;
    }
    
    /* Allocate page */
    Page *page = page_pool_alloc(&dm->pool, track->track_id);
    if (!page) return NULL;
    
    page->header.centroid = centroid;
    
    /* Add to track */
    track_add_page(track, page);
    
    /* Update allocation tree */
    update_allocation_tree(dm, page);
    
    /* Update bounds */
    mbr_expand_point(&dm->header.bounds, &centroid);
    
    dm->is_dirty = true;
    dm->header.page_count++;
    
    return page;
}

int disk_manager_free_page(DiskManager *dm, uint32_t page_id) {
    if (!dm) return DM_ERR_NULL_PTR;
    
    int err = page_pool_free_page(&dm->pool, page_id);
    if (err != PAGE_OK) return DM_ERR_NOT_FOUND;
    
    dm->is_dirty = true;
    dm->header.page_count--;
    
    /* Rebuild allocation tree */
    disk_manager_rebuild_allocation_tree(dm);
    
    return DM_OK;
}

Page* disk_manager_get_page(DiskManager *dm, uint32_t page_id) {
    if (!dm) return NULL;
    
    /* Try cache first */
    Page *page = page_cache_get(&dm->cache, page_id);
    if (page) {
        dm->stats.cache_hits++;
        return page;
    }
    
    dm->stats.cache_misses++;
    
    /* Get from pool */
    page = page_pool_get(&dm->pool, page_id);
    if (!page) return NULL;
    
    /* Load from disk if needed */
    if (!page->in_memory && dm->is_open) {
        read_page_from_disk(dm, page);
    }
    
    return page;
}

int disk_manager_write_page(DiskManager *dm, Page *page) {
    if (!dm || !page) return DM_ERR_NULL_PTR;
    
    page->header.flags |= PAGE_STATUS_DIRTY;
    dm->is_dirty = true;
    
    if (dm->config.sync_on_write && dm->is_open) {
        return write_page_to_disk(dm, page);
    }
    
    return DM_OK;
}

DiskTrack* disk_manager_find_best_track(DiskManager *dm, Point centroid) {
    if (!dm) return NULL;
    
    switch (dm->config.strategy) {
        case ALLOC_NEAREST_TRACK: {
            /* Find track with nearest centroid */
            DiskTrack *best = NULL;
            double best_dist = __DBL_MAX__;
            
            for (size_t i = 0; i < dm->pool.track_count; i++) {
                DiskTrack *track = dm->pool.tracks[i];
                if (!track_has_space(track)) continue;
                
                double dist = point_distance(&centroid, &track->centroid);
                if (dist < best_dist) {
                    best_dist = dist;
                    best = track;
                }
            }
            return best;
        }
        
        case ALLOC_BEST_FIT: {
            /* Find track that best contains the centroid's region */
            DiskTrack *best = NULL;
            double best_expansion = __DBL_MAX__;
            
            for (size_t i = 0; i < dm->pool.track_count; i++) {
                DiskTrack *track = dm->pool.tracks[i];
                if (!track_has_space(track)) continue;
                
                /* Calculate how much the track's extent would expand */
                MBR expanded = track->extent;
                mbr_expand_point(&expanded, &centroid);
                double expansion = mbr_area(&expanded) - mbr_area(&track->extent);
                
                if (expansion < best_expansion) {
                    best_expansion = expansion;
                    best = track;
                }
            }
            return best;
        }
        
        case ALLOC_SEQUENTIAL: {
            /* Use last track if it has space */
            if (dm->pool.track_count > 0) {
                DiskTrack *last = dm->pool.tracks[dm->pool.track_count - 1];
                if (track_has_space(last)) return last;
            }
            return NULL;
        }
        
        case ALLOC_NEW_TRACK:
        default:
            return NULL;
    }
}

int disk_manager_query_region(DiskManager *dm, const MBR *region,
                               Page ***pages, size_t *count) {
    if (!dm || !region || !pages || !count) return DM_ERR_NULL_PTR;
    
    return page_pool_query_region(&dm->pool, region, pages, count);
}

void disk_manager_get_stats(const DiskManager *dm, IOStats *stats) {
    if (!dm || !stats) return;
    *stats = dm->stats;
}

void disk_manager_reset_stats(DiskManager *dm) {
    if (!dm) return;
    memset(&dm->stats, 0, sizeof(IOStats));
}

uint64_t disk_manager_estimate_seeks(const DiskManager *dm,
                                      const uint32_t *page_ids, size_t count) {
    if (!dm || !page_ids || count == 0) return 0;
    
    uint64_t seeks = 0;
    uint32_t last_track = 0;
    
    for (size_t i = 0; i < count; i++) {
        Page *page = page_pool_get((PagePool *)&dm->pool, page_ids[i]);
        if (!page) continue;
        
        if (page->header.track_id != last_track && last_track != 0) {
            seeks++;
        }
        last_track = page->header.track_id;
    }
    
    return seeks;
}

int disk_manager_optimize(DiskManager *dm) {
    if (!dm) return DM_ERR_NULL_PTR;
    
    /* Reorganize pages by spatial locality */
    /* This is a simplified implementation - a production version
     * would use more sophisticated clustering algorithms */
    
    /* Rebuild allocation tree with current page centroids */
    disk_manager_rebuild_allocation_tree(dm);
    
    return DM_OK;
}

int disk_manager_rebuild_allocation_tree(DiskManager *dm) {
    if (!dm) return DM_ERR_NULL_PTR;
    
    /* Clear existing tree */
    kdtree_free(&dm->allocation_tree);
    kdtree_init(&dm->allocation_tree);
    
    /* Re-add all pages */
    for (size_t i = 0; i < dm->pool.page_count; i++) {
        Page *page = dm->pool.pages[i];
        if (page->header.object_count > 0) {
            kdtree_insert(&dm->allocation_tree, page->header.centroid,
                         page->header.page_id, page);
        }
    }
    
    return DM_OK;
}

/* ============================================================================
 * Track Management
 * ============================================================================ */

DiskTrack* disk_manager_create_track(DiskManager *dm) {
    if (!dm) return NULL;
    
    DiskTrack *track = page_pool_create_track(&dm->pool);
    if (!track) return NULL;
    
    dm->is_dirty = true;
    dm->header.track_count++;
    
    return track;
}

DiskTrack* disk_manager_get_track(DiskManager *dm, uint32_t track_id) {
    if (!dm) return NULL;
    return page_pool_get_track(&dm->pool, track_id);
}

int disk_manager_query_tracks(DiskManager *dm, const MBR *region,
                               DiskTrack ***tracks, size_t *count) {
    if (!dm || !region || !tracks || !count) return DM_ERR_NULL_PTR;
    
    *tracks = NULL;
    *count = 0;
    
    /* Count matching tracks */
    size_t matching = 0;
    for (size_t i = 0; i < dm->pool.track_count; i++) {
        if (mbr_intersects(&dm->pool.tracks[i]->extent, region)) {
            matching++;
        }
    }
    
    if (matching == 0) return DM_OK;
    
    /* Allocate result array */
    *tracks = (DiskTrack **)malloc(matching * sizeof(DiskTrack *));
    if (!*tracks) return DM_ERR_ALLOC;
    
    /* Collect matching tracks */
    size_t idx = 0;
    for (size_t i = 0; i < dm->pool.track_count; i++) {
        if (mbr_intersects(&dm->pool.tracks[i]->extent, region)) {
            (*tracks)[idx++] = dm->pool.tracks[i];
        }
    }
    
    *count = idx;
    return DM_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

size_t disk_manager_file_size(const DiskManager *dm) {
    if (!dm || !dm->data_file) return 0;
    
    long current = ftell(dm->data_file);
    fseek(dm->data_file, 0, SEEK_END);
    long size = ftell(dm->data_file);
    fseek(dm->data_file, current, SEEK_SET);
    
    return (size_t)size;
}

bool disk_manager_file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

int disk_manager_validate(DiskManager *dm) {
    if (!dm) return DM_ERR_NULL_PTR;
    if (!dm->is_open) return DM_ERR_NOT_OPEN;
    
    /* Validate header */
    if (dm->header.magic != DM_MAGIC) return DM_ERR_CORRUPT;
    if (dm->header.version > DM_VERSION) return DM_ERR_VERSION;
    
    /* Validate pages */
    for (size_t i = 0; i < dm->pool.page_count; i++) {
        if (!page_verify(dm->pool.pages[i])) {
            return DM_ERR_CORRUPT;
        }
    }
    
    return DM_OK;
}

void disk_manager_print_stats(const DiskManager *dm, FILE *out) {
    if (!dm || !out) return;
    
    fprintf(out, "=== Disk Manager Statistics ===\n");
    fprintf(out, "File: %s\n", dm->file_path ? dm->file_path : "(none)");
    fprintf(out, "Open: %s\n", dm->is_open ? "yes" : "no");
    fprintf(out, "Dirty: %s\n", dm->is_dirty ? "yes" : "no");
    fprintf(out, "\n");
    fprintf(out, "Pages: %u\n", dm->header.page_count);
    fprintf(out, "Tracks: %u\n", dm->header.track_count);
    fprintf(out, "Objects: %lu\n", (unsigned long)dm->header.object_count);
    fprintf(out, "\n");
    fprintf(out, "I/O Statistics:\n");
    fprintf(out, "  Pages read: %lu\n", (unsigned long)dm->stats.pages_read);
    fprintf(out, "  Pages written: %lu\n", (unsigned long)dm->stats.pages_written);
    fprintf(out, "  Cache hits: %lu\n", (unsigned long)dm->stats.cache_hits);
    fprintf(out, "  Cache misses: %lu\n", (unsigned long)dm->stats.cache_misses);
    fprintf(out, "  Bytes read: %lu\n", (unsigned long)dm->stats.bytes_read);
    fprintf(out, "  Bytes written: %lu\n", (unsigned long)dm->stats.bytes_written);
    
    if (dm->stats.cache_hits + dm->stats.cache_misses > 0) {
        double hit_rate = 100.0 * dm->stats.cache_hits / 
                          (dm->stats.cache_hits + dm->stats.cache_misses);
        fprintf(out, "  Cache hit rate: %.1f%%\n", hit_rate);
    }
}

