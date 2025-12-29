/**
 * @file lsm.h
 * @brief LSM-tree for incremental spatial index updates
 * 
 * Provides an LSM-tree (Log-Structured Merge-tree) structure for
 * efficient incremental updates without full index rebuilds.
 * 
 * Architecture:
 * - MemTable: In-memory sorted structure for recent writes
 * - WAL (Write-Ahead Log): Durability for uncommitted changes
 * - Levels: Sorted runs on disk, merged during compaction
 */

#ifndef URBIS_LSM_H
#define URBIS_LSM_H

#include "geometry.h"
#include "page.h"
#include "kdtree.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define LSM_DEFAULT_MEMTABLE_SIZE (64 * 1024)  /**< 64K objects before flush */
#define LSM_DEFAULT_LEVEL_RATIO 10             /**< Size ratio between levels */
#define LSM_MAX_LEVELS 7                       /**< Maximum number of levels */
#define LSM_WAL_MAGIC 0x4C534D57               /**< "LSMW" magic number */
#define LSM_WAL_VERSION 1

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief MemTable entry
 */
typedef struct LSMEntry {
    SpatialObject object;       /**< Spatial object */
    uint64_t sequence;          /**< Sequence number for ordering */
    bool is_delete;             /**< True if this is a tombstone */
    struct LSMEntry *next;      /**< For hash table chaining */
} LSMEntry;

/**
 * @brief MemTable structure (in-memory sorted tree)
 */
typedef struct {
    LSMEntry *entries;          /**< Array of entries */
    size_t count;               /**< Current entry count */
    size_t capacity;            /**< Array capacity */
    size_t max_size;            /**< Flush threshold */
    MBR bounds;                 /**< Bounding box of all entries */
    KDTree index;               /**< KD-tree index for queries */
    pthread_rwlock_t lock;      /**< Read-write lock */
    uint64_t next_sequence;     /**< Next sequence number */
} LSMMemTable;

/**
 * @brief Level run (sorted pages on disk)
 */
typedef struct {
    uint32_t run_id;            /**< Unique run identifier */
    Page **pages;               /**< Array of pages in this run */
    size_t page_count;          /**< Number of pages */
    size_t object_count;        /**< Total objects in run */
    MBR bounds;                 /**< Bounding box of run */
    uint64_t min_sequence;      /**< Minimum sequence number */
    uint64_t max_sequence;      /**< Maximum sequence number */
} LSMRun;

/**
 * @brief Level in the LSM-tree
 */
typedef struct {
    LSMRun **runs;              /**< Array of runs at this level */
    size_t run_count;           /**< Number of runs */
    size_t run_capacity;        /**< Capacity of runs array */
    size_t max_runs;            /**< Max runs before compaction */
    size_t total_objects;       /**< Total objects at this level */
    MBR bounds;                 /**< Combined bounds */
} LSMLevel;

/**
 * @brief WAL (Write-Ahead Log) entry header
 */
typedef struct {
    uint32_t magic;             /**< Magic number */
    uint32_t entry_size;        /**< Size of entry data */
    uint64_t sequence;          /**< Sequence number */
    uint32_t checksum;          /**< CRC32 of entry data */
    uint8_t op_type;            /**< Operation type (insert/delete) */
    uint8_t reserved[3];        /**< Reserved */
} WALEntryHeader;

/**
 * @brief WAL configuration
 */
typedef struct {
    char *path;                 /**< WAL file path */
    size_t max_size;            /**< Max WAL size before rotation */
    bool sync_on_write;         /**< Sync after each write */
} WALConfig;

/**
 * @brief WAL structure
 */
typedef struct {
    WALConfig config;
    FILE *file;                 /**< WAL file handle */
    char *current_path;         /**< Current WAL file path */
    size_t current_size;        /**< Current file size */
    uint64_t next_sequence;     /**< Next sequence number */
    pthread_mutex_t lock;       /**< Mutex for write serialization */
} LSMWriteAheadLog;

/**
 * @brief LSM-tree configuration
 */
typedef struct {
    size_t memtable_size;       /**< Max objects in memtable */
    size_t level_ratio;         /**< Size ratio between levels */
    size_t level0_compaction;   /**< Runs at L0 before compaction */
    WALConfig wal_config;       /**< WAL configuration */
    bool enable_wal;            /**< Enable write-ahead logging */
    char *data_dir;             /**< Directory for level files */
} LSMConfig;

/**
 * @brief LSM-tree statistics
 */
typedef struct {
    uint64_t inserts;           /**< Total inserts */
    uint64_t deletes;           /**< Total deletes */
    uint64_t flushes;           /**< MemTable flushes */
    uint64_t compactions;       /**< Compaction operations */
    uint64_t memtable_queries;  /**< Queries served from memtable */
    uint64_t level_queries;     /**< Queries to disk levels */
    size_t total_objects;       /**< Current total objects */
    size_t total_pages;         /**< Current total pages */
    double write_amplification; /**< Write amplification factor */
} LSMStats;

/**
 * @brief LSM-tree structure
 */
typedef struct {
    LSMConfig config;
    LSMMemTable memtable;       /**< Active memtable */
    LSMMemTable *immutable;     /**< Immutable memtable being flushed */
    LSMLevel levels[LSM_MAX_LEVELS]; /**< Disk levels */
    size_t level_count;         /**< Number of active levels */
    LSMWriteAheadLog *wal;      /**< Write-ahead log */
    LSMStats stats;             /**< Statistics */
    uint64_t next_run_id;       /**< Next run ID */
    pthread_rwlock_t lock;      /**< Global read-write lock */
    bool is_compacting;         /**< Compaction in progress */
} LSMTree;

/**
 * @brief Query result from LSM-tree
 */
typedef struct {
    SpatialObject *objects;     /**< Array of result objects */
    size_t count;               /**< Number of results */
    size_t capacity;            /**< Array capacity */
} LSMQueryResult;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    LSM_OK = 0,
    LSM_ERR_NULL_PTR = -1,
    LSM_ERR_ALLOC = -2,
    LSM_ERR_IO = -3,
    LSM_ERR_CORRUPT = -4,
    LSM_ERR_NOT_FOUND = -5,
    LSM_ERR_FULL = -6,
    LSM_ERR_COMPACTING = -7
} LSMError;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Get default LSM configuration
 */
LSMConfig lsm_default_config(void);

/* ============================================================================
 * LSM-Tree Operations
 * ============================================================================ */

/**
 * @brief Create a new LSM-tree
 */
LSMTree* lsm_create(const LSMConfig *config);

/**
 * @brief Destroy LSM-tree and free resources
 */
void lsm_destroy(LSMTree *lsm);

/**
 * @brief Insert a spatial object
 * @param lsm LSM-tree
 * @param obj Spatial object to insert
 * @return LSM_OK on success
 */
int lsm_insert(LSMTree *lsm, const SpatialObject *obj);

/**
 * @brief Delete a spatial object by ID
 * @param lsm LSM-tree
 * @param object_id ID of object to delete
 * @return LSM_OK on success
 */
int lsm_delete(LSMTree *lsm, uint64_t object_id);

/**
 * @brief Query objects in a spatial region
 * @param lsm LSM-tree
 * @param region Query bounding box
 * @param result Output results (must be initialized)
 * @return LSM_OK on success
 */
int lsm_query_range(LSMTree *lsm, const MBR *region, LSMQueryResult *result);

/**
 * @brief Find nearest object to a point
 * @param lsm LSM-tree
 * @param point Query point
 * @param nearest Output nearest object
 * @return LSM_OK on success
 */
int lsm_query_nearest(LSMTree *lsm, Point point, SpatialObject *nearest);

/**
 * @brief Find object by ID
 * @param lsm LSM-tree
 * @param object_id Object ID to find
 * @param obj Output object (if found)
 * @return LSM_OK if found, LSM_ERR_NOT_FOUND otherwise
 */
int lsm_get(LSMTree *lsm, uint64_t object_id, SpatialObject *obj);

/**
 * @brief Flush memtable to disk
 * @param lsm LSM-tree
 * @return LSM_OK on success
 */
int lsm_flush(LSMTree *lsm);

/**
 * @brief Trigger compaction at a specific level
 * @param lsm LSM-tree
 * @param level Level to compact (0 for L0)
 * @return LSM_OK on success
 */
int lsm_compact(LSMTree *lsm, size_t level);

/**
 * @brief Compact all levels
 */
int lsm_compact_all(LSMTree *lsm);

/**
 * @brief Get LSM statistics
 */
void lsm_get_stats(const LSMTree *lsm, LSMStats *stats);

/**
 * @brief Print LSM statistics
 */
void lsm_print_stats(const LSMTree *lsm, FILE *out);

/* ============================================================================
 * MemTable Operations
 * ============================================================================ */

/**
 * @brief Initialize a memtable
 */
int memtable_init(LSMMemTable *mt, size_t max_size);

/**
 * @brief Free memtable resources
 */
void memtable_free(LSMMemTable *mt);

/**
 * @brief Insert into memtable
 */
int memtable_insert(LSMMemTable *mt, const SpatialObject *obj, uint64_t sequence);

/**
 * @brief Delete from memtable (add tombstone)
 */
int memtable_delete(LSMMemTable *mt, uint64_t object_id, uint64_t sequence);

/**
 * @brief Query memtable range
 */
int memtable_query_range(LSMMemTable *mt, const MBR *region, LSMQueryResult *result);

/**
 * @brief Get from memtable by ID
 */
int memtable_get(LSMMemTable *mt, uint64_t object_id, SpatialObject *obj, bool *is_delete);

/**
 * @brief Check if memtable is full
 */
bool memtable_is_full(const LSMMemTable *mt);

/**
 * @brief Get memtable size
 */
size_t memtable_size(const LSMMemTable *mt);

/* ============================================================================
 * WAL Operations
 * ============================================================================ */

/**
 * @brief Create WAL
 */
LSMWriteAheadLog* wal_create(const WALConfig *config);

/**
 * @brief Destroy WAL
 */
void wal_destroy(LSMWriteAheadLog *wal);

/**
 * @brief Write insert to WAL
 */
int wal_write_insert(LSMWriteAheadLog *wal, const SpatialObject *obj, uint64_t sequence);

/**
 * @brief Write delete to WAL
 */
int wal_write_delete(LSMWriteAheadLog *wal, uint64_t object_id, uint64_t sequence);

/**
 * @brief Replay WAL entries
 */
int wal_replay(LSMWriteAheadLog *wal, LSMMemTable *mt);

/**
 * @brief Truncate WAL (after successful flush)
 */
int wal_truncate(LSMWriteAheadLog *wal);

/**
 * @brief Sync WAL to disk
 */
int wal_sync(LSMWriteAheadLog *wal);

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

/**
 * @brief Initialize query result
 */
int lsm_result_init(LSMQueryResult *result, size_t capacity);

/**
 * @brief Free query result
 */
void lsm_result_free(LSMQueryResult *result);

/**
 * @brief Clear query result
 */
void lsm_result_clear(LSMQueryResult *result);

/**
 * @brief Add to query result
 */
int lsm_result_add(LSMQueryResult *result, const SpatialObject *obj);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_LSM_H */

