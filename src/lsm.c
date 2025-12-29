/**
 * @file lsm.c
 * @brief LSM-tree implementation for incremental spatial index updates
 */

#include "lsm.h"
#include "compression.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

LSMConfig lsm_default_config(void) {
    LSMConfig config = {
        .memtable_size = LSM_DEFAULT_MEMTABLE_SIZE,
        .level_ratio = LSM_DEFAULT_LEVEL_RATIO,
        .level0_compaction = 4,
        .wal_config = {
            .path = NULL,
            .max_size = 64 * 1024 * 1024,  /* 64MB */
            .sync_on_write = true
        },
        .enable_wal = false,
        .data_dir = NULL
    };
    return config;
}

/* ============================================================================
 * Query Result Operations
 * ============================================================================ */

int lsm_result_init(LSMQueryResult *result, size_t capacity) {
    if (!result) return LSM_ERR_NULL_PTR;
    
    result->count = 0;
    result->capacity = capacity > 0 ? capacity : 64;
    result->objects = (SpatialObject *)calloc(result->capacity, sizeof(SpatialObject));
    
    if (!result->objects) {
        result->capacity = 0;
        return LSM_ERR_ALLOC;
    }
    
    return LSM_OK;
}

void lsm_result_free(LSMQueryResult *result) {
    if (!result) return;
    free(result->objects);
    result->objects = NULL;
    result->count = 0;
    result->capacity = 0;
}

void lsm_result_clear(LSMQueryResult *result) {
    if (!result) return;
    result->count = 0;
}

int lsm_result_add(LSMQueryResult *result, const SpatialObject *obj) {
    if (!result || !obj) return LSM_ERR_NULL_PTR;
    
    /* Grow if needed */
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * 2;
        SpatialObject *new_objs = (SpatialObject *)realloc(result->objects, 
                                                            new_cap * sizeof(SpatialObject));
        if (!new_objs) return LSM_ERR_ALLOC;
        result->objects = new_objs;
        result->capacity = new_cap;
    }
    
    result->objects[result->count++] = *obj;
    return LSM_OK;
}

/* ============================================================================
 * MemTable Implementation
 * ============================================================================ */

int memtable_init(LSMMemTable *mt, size_t max_size) {
    if (!mt) return LSM_ERR_NULL_PTR;
    
    memset(mt, 0, sizeof(LSMMemTable));
    mt->max_size = max_size;
    mt->capacity = 1024;
    mt->entries = (LSMEntry *)calloc(mt->capacity, sizeof(LSMEntry));
    
    if (!mt->entries) return LSM_ERR_ALLOC;
    
    mt->bounds = mbr_empty();
    kdtree_init(&mt->index);
    pthread_rwlock_init(&mt->lock, NULL);
    mt->next_sequence = 1;
    
    return LSM_OK;
}

void memtable_free(LSMMemTable *mt) {
    if (!mt) return;
    
    pthread_rwlock_wrlock(&mt->lock);
    
    free(mt->entries);
    mt->entries = NULL;
    kdtree_free(&mt->index);
    
    pthread_rwlock_unlock(&mt->lock);
    pthread_rwlock_destroy(&mt->lock);
    
    memset(mt, 0, sizeof(LSMMemTable));
}

int memtable_insert(LSMMemTable *mt, const SpatialObject *obj, uint64_t sequence) {
    if (!mt || !obj) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_wrlock(&mt->lock);
    
    /* Grow entries array if needed */
    if (mt->count >= mt->capacity) {
        size_t new_cap = mt->capacity * 2;
        LSMEntry *new_entries = (LSMEntry *)realloc(mt->entries, 
                                                     new_cap * sizeof(LSMEntry));
        if (!new_entries) {
            pthread_rwlock_unlock(&mt->lock);
            return LSM_ERR_ALLOC;
        }
        mt->entries = new_entries;
        mt->capacity = new_cap;
    }
    
    /* Add entry */
    LSMEntry *entry = &mt->entries[mt->count];
    entry->object = *obj;
    entry->sequence = sequence;
    entry->is_delete = false;
    entry->next = NULL;
    
    /* Update bounds */
    mbr_expand_mbr(&mt->bounds, &obj->mbr);
    
    mt->count++;
    
    pthread_rwlock_unlock(&mt->lock);
    return LSM_OK;
}

int memtable_delete(LSMMemTable *mt, uint64_t object_id, uint64_t sequence) {
    if (!mt) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_wrlock(&mt->lock);
    
    /* Grow entries array if needed */
    if (mt->count >= mt->capacity) {
        size_t new_cap = mt->capacity * 2;
        LSMEntry *new_entries = (LSMEntry *)realloc(mt->entries, 
                                                     new_cap * sizeof(LSMEntry));
        if (!new_entries) {
            pthread_rwlock_unlock(&mt->lock);
            return LSM_ERR_ALLOC;
        }
        mt->entries = new_entries;
        mt->capacity = new_cap;
    }
    
    /* Add tombstone entry */
    LSMEntry *entry = &mt->entries[mt->count];
    memset(entry, 0, sizeof(LSMEntry));
    entry->object.id = object_id;
    entry->sequence = sequence;
    entry->is_delete = true;
    
    mt->count++;
    
    pthread_rwlock_unlock(&mt->lock);
    return LSM_OK;
}

int memtable_query_range(LSMMemTable *mt, const MBR *region, LSMQueryResult *result) {
    if (!mt || !region || !result) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_rdlock(&mt->lock);
    
    /* Simple linear scan approach - safer than KD-tree pointers after realloc */
    for (size_t i = 0; i < mt->count; i++) {
        LSMEntry *entry = &mt->entries[i];
        if (!entry->is_delete && mbr_intersects(&entry->object.mbr, region)) {
            lsm_result_add(result, &entry->object);
        }
    }
    
    pthread_rwlock_unlock(&mt->lock);
    return LSM_OK;
}

int memtable_get(LSMMemTable *mt, uint64_t object_id, SpatialObject *obj, bool *is_delete) {
    if (!mt) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_rdlock(&mt->lock);
    
    /* Search backwards to find most recent entry for this ID */
    for (size_t i = mt->count; i > 0; i--) {
        LSMEntry *entry = &mt->entries[i - 1];
        if (entry->object.id == object_id) {
            if (obj) *obj = entry->object;
            if (is_delete) *is_delete = entry->is_delete;
            pthread_rwlock_unlock(&mt->lock);
            return LSM_OK;
        }
    }
    
    pthread_rwlock_unlock(&mt->lock);
    return LSM_ERR_NOT_FOUND;
}

bool memtable_is_full(const LSMMemTable *mt) {
    if (!mt) return true;
    return mt->count >= mt->max_size;
}

size_t memtable_size(const LSMMemTable *mt) {
    if (!mt) return 0;
    return mt->count;
}

/* ============================================================================
 * WAL Implementation
 * ============================================================================ */

LSMWriteAheadLog* wal_create(const WALConfig *config) {
    if (!config || !config->path) return NULL;
    
    LSMWriteAheadLog *wal = (LSMWriteAheadLog *)calloc(1, sizeof(LSMWriteAheadLog));
    if (!wal) return NULL;
    
    wal->config = *config;
    wal->config.path = strdup(config->path);
    
    /* Create WAL file path */
    size_t path_len = strlen(config->path) + 32;
    wal->current_path = (char *)malloc(path_len);
    if (!wal->current_path) {
        free(wal->config.path);
        free(wal);
        return NULL;
    }
    snprintf(wal->current_path, path_len, "%s/wal.log", config->path);
    
    /* Open or create WAL file */
    wal->file = fopen(wal->current_path, "a+b");
    if (!wal->file) {
        free(wal->current_path);
        free(wal->config.path);
        free(wal);
        return NULL;
    }
    
    /* Get current size */
    fseek(wal->file, 0, SEEK_END);
    wal->current_size = ftell(wal->file);
    
    pthread_mutex_init(&wal->lock, NULL);
    wal->next_sequence = 1;
    
    return wal;
}

void wal_destroy(LSMWriteAheadLog *wal) {
    if (!wal) return;
    
    if (wal->file) {
        fflush(wal->file);
        fclose(wal->file);
    }
    
    pthread_mutex_destroy(&wal->lock);
    free(wal->current_path);
    free(wal->config.path);
    free(wal);
}

int wal_write_insert(LSMWriteAheadLog *wal, const SpatialObject *obj, uint64_t sequence) {
    if (!wal || !obj) return LSM_ERR_NULL_PTR;
    
    pthread_mutex_lock(&wal->lock);
    
    /* Prepare header */
    WALEntryHeader header = {
        .magic = LSM_WAL_MAGIC,
        .entry_size = sizeof(SpatialObject),
        .sequence = sequence,
        .op_type = 0,  /* Insert */
        .checksum = crc32_compute((const uint8_t *)obj, sizeof(SpatialObject))
    };
    
    /* Write header and data */
    if (fwrite(&header, sizeof(header), 1, wal->file) != 1 ||
        fwrite(obj, sizeof(SpatialObject), 1, wal->file) != 1) {
        pthread_mutex_unlock(&wal->lock);
        return LSM_ERR_IO;
    }
    
    wal->current_size += sizeof(header) + sizeof(SpatialObject);
    
    if (wal->config.sync_on_write) {
        fflush(wal->file);
    }
    
    pthread_mutex_unlock(&wal->lock);
    return LSM_OK;
}

int wal_write_delete(LSMWriteAheadLog *wal, uint64_t object_id, uint64_t sequence) {
    if (!wal) return LSM_ERR_NULL_PTR;
    
    pthread_mutex_lock(&wal->lock);
    
    /* Prepare header */
    WALEntryHeader header = {
        .magic = LSM_WAL_MAGIC,
        .entry_size = sizeof(uint64_t),
        .sequence = sequence,
        .op_type = 1,  /* Delete */
        .checksum = crc32_compute((const uint8_t *)&object_id, sizeof(uint64_t))
    };
    
    /* Write header and object ID */
    if (fwrite(&header, sizeof(header), 1, wal->file) != 1 ||
        fwrite(&object_id, sizeof(uint64_t), 1, wal->file) != 1) {
        pthread_mutex_unlock(&wal->lock);
        return LSM_ERR_IO;
    }
    
    wal->current_size += sizeof(header) + sizeof(uint64_t);
    
    if (wal->config.sync_on_write) {
        fflush(wal->file);
    }
    
    pthread_mutex_unlock(&wal->lock);
    return LSM_OK;
}

int wal_replay(LSMWriteAheadLog *wal, LSMMemTable *mt) {
    if (!wal || !mt) return LSM_ERR_NULL_PTR;
    
    pthread_mutex_lock(&wal->lock);
    
    /* Seek to beginning */
    fseek(wal->file, 0, SEEK_SET);
    
    WALEntryHeader header;
    uint64_t max_sequence = 0;
    
    while (fread(&header, sizeof(header), 1, wal->file) == 1) {
        if (header.magic != LSM_WAL_MAGIC) {
            break;  /* End of valid entries */
        }
        
        if (header.op_type == 0) {
            /* Insert */
            SpatialObject obj;
            if (fread(&obj, sizeof(obj), 1, wal->file) != 1) break;
            
            /* Verify checksum */
            if (crc32_compute((const uint8_t *)&obj, sizeof(obj)) != header.checksum) {
                break;  /* Corrupt entry */
            }
            
            memtable_insert(mt, &obj, header.sequence);
        } else {
            /* Delete */
            uint64_t object_id;
            if (fread(&object_id, sizeof(object_id), 1, wal->file) != 1) break;
            
            /* Verify checksum */
            if (crc32_compute((const uint8_t *)&object_id, sizeof(object_id)) != header.checksum) {
                break;  /* Corrupt entry */
            }
            
            memtable_delete(mt, object_id, header.sequence);
        }
        
        if (header.sequence > max_sequence) {
            max_sequence = header.sequence;
        }
    }
    
    wal->next_sequence = max_sequence + 1;
    
    pthread_mutex_unlock(&wal->lock);
    return LSM_OK;
}

int wal_truncate(LSMWriteAheadLog *wal) {
    if (!wal) return LSM_ERR_NULL_PTR;
    
    pthread_mutex_lock(&wal->lock);
    
    if (wal->file) {
        fclose(wal->file);
    }
    
    /* Reopen and truncate */
    wal->file = fopen(wal->current_path, "w+b");
    if (!wal->file) {
        pthread_mutex_unlock(&wal->lock);
        return LSM_ERR_IO;
    }
    
    wal->current_size = 0;
    
    pthread_mutex_unlock(&wal->lock);
    return LSM_OK;
}

int wal_sync(LSMWriteAheadLog *wal) {
    if (!wal || !wal->file) return LSM_ERR_NULL_PTR;
    
    pthread_mutex_lock(&wal->lock);
    fflush(wal->file);
    pthread_mutex_unlock(&wal->lock);
    
    return LSM_OK;
}

/* ============================================================================
 * LSM Level Operations
 * ============================================================================ */

static int level_init(LSMLevel *level, size_t max_runs) {
    if (!level) return LSM_ERR_NULL_PTR;
    
    memset(level, 0, sizeof(LSMLevel));
    level->max_runs = max_runs;
    level->run_capacity = 16;
    level->runs = (LSMRun **)calloc(level->run_capacity, sizeof(LSMRun *));
    
    if (!level->runs) return LSM_ERR_ALLOC;
    
    level->bounds = mbr_empty();
    
    return LSM_OK;
}

static void level_free(LSMLevel *level) {
    if (!level) return;
    
    for (size_t i = 0; i < level->run_count; i++) {
        LSMRun *run = level->runs[i];
        if (run) {
            for (size_t j = 0; j < run->page_count; j++) {
                page_free(run->pages[j]);
            }
            free(run->pages);
            free(run);
        }
    }
    
    free(level->runs);
    memset(level, 0, sizeof(LSMLevel));
}

static int level_add_run(LSMLevel *level, LSMRun *run) {
    if (!level || !run) return LSM_ERR_NULL_PTR;
    
    /* Grow array if needed */
    if (level->run_count >= level->run_capacity) {
        size_t new_cap = level->run_capacity * 2;
        LSMRun **new_runs = (LSMRun **)realloc(level->runs, new_cap * sizeof(LSMRun *));
        if (!new_runs) return LSM_ERR_ALLOC;
        level->runs = new_runs;
        level->run_capacity = new_cap;
    }
    
    level->runs[level->run_count++] = run;
    level->total_objects += run->object_count;
    mbr_expand_mbr(&level->bounds, &run->bounds);
    
    return LSM_OK;
}

static bool level_needs_compaction(const LSMLevel *level) {
    if (!level) return false;
    return level->run_count >= level->max_runs;
}

/* ============================================================================
 * LSM-Tree Implementation
 * ============================================================================ */

LSMTree* lsm_create(const LSMConfig *config) {
    LSMTree *lsm = (LSMTree *)calloc(1, sizeof(LSMTree));
    if (!lsm) return NULL;
    
    /* Copy config */
    if (config) {
        lsm->config = *config;
        if (config->data_dir) {
            lsm->config.data_dir = strdup(config->data_dir);
        }
        if (config->wal_config.path) {
            lsm->config.wal_config.path = strdup(config->wal_config.path);
        }
    } else {
        lsm->config = lsm_default_config();
    }
    
    /* Initialize memtable */
    if (memtable_init(&lsm->memtable, lsm->config.memtable_size) != LSM_OK) {
        free(lsm);
        return NULL;
    }
    
    /* Initialize levels */
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        size_t max_runs = (i == 0) ? lsm->config.level0_compaction : 1;
        if (level_init(&lsm->levels[i], max_runs) != LSM_OK) {
            memtable_free(&lsm->memtable);
            for (size_t j = 0; j < i; j++) {
                level_free(&lsm->levels[j]);
            }
            free(lsm);
            return NULL;
        }
    }
    
    /* Initialize WAL if enabled */
    if (lsm->config.enable_wal && lsm->config.wal_config.path) {
        lsm->wal = wal_create(&lsm->config.wal_config);
        if (lsm->wal) {
            /* Replay WAL to recover memtable */
            wal_replay(lsm->wal, &lsm->memtable);
        }
    }
    
    pthread_rwlock_init(&lsm->lock, NULL);
    lsm->next_run_id = 1;
    
    return lsm;
}

void lsm_destroy(LSMTree *lsm) {
    if (!lsm) return;
    
    pthread_rwlock_wrlock(&lsm->lock);
    
    /* Free WAL */
    if (lsm->wal) {
        wal_destroy(lsm->wal);
    }
    
    /* Free memtables */
    memtable_free(&lsm->memtable);
    if (lsm->immutable) {
        memtable_free(lsm->immutable);
        free(lsm->immutable);
    }
    
    /* Free levels */
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        level_free(&lsm->levels[i]);
    }
    
    /* Free config strings */
    free(lsm->config.data_dir);
    free(lsm->config.wal_config.path);
    
    pthread_rwlock_unlock(&lsm->lock);
    pthread_rwlock_destroy(&lsm->lock);
    
    free(lsm);
}

int lsm_insert(LSMTree *lsm, const SpatialObject *obj) {
    if (!lsm || !obj) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_wrlock(&lsm->lock);
    
    uint64_t sequence = lsm->memtable.next_sequence++;
    
    /* Write to WAL first (for durability) */
    if (lsm->wal) {
        int err = wal_write_insert(lsm->wal, obj, sequence);
        if (err != LSM_OK) {
            pthread_rwlock_unlock(&lsm->lock);
            return err;
        }
    }
    
    /* Insert into memtable */
    int err = memtable_insert(&lsm->memtable, obj, sequence);
    if (err != LSM_OK) {
        pthread_rwlock_unlock(&lsm->lock);
        return err;
    }
    
    lsm->stats.inserts++;
    lsm->stats.total_objects++;
    
    /* Check if flush is needed */
    if (memtable_is_full(&lsm->memtable)) {
        pthread_rwlock_unlock(&lsm->lock);
        return lsm_flush(lsm);
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    return LSM_OK;
}

int lsm_delete(LSMTree *lsm, uint64_t object_id) {
    if (!lsm) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_wrlock(&lsm->lock);
    
    uint64_t sequence = lsm->memtable.next_sequence++;
    
    /* Write to WAL first */
    if (lsm->wal) {
        int err = wal_write_delete(lsm->wal, object_id, sequence);
        if (err != LSM_OK) {
            pthread_rwlock_unlock(&lsm->lock);
            return err;
        }
    }
    
    /* Add tombstone to memtable */
    int err = memtable_delete(&lsm->memtable, object_id, sequence);
    if (err != LSM_OK) {
        pthread_rwlock_unlock(&lsm->lock);
        return err;
    }
    
    lsm->stats.deletes++;
    if (lsm->stats.total_objects > 0) {
        lsm->stats.total_objects--;
    }
    
    /* Check if flush is needed */
    if (memtable_is_full(&lsm->memtable)) {
        pthread_rwlock_unlock(&lsm->lock);
        return lsm_flush(lsm);
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    return LSM_OK;
}

int lsm_query_range(LSMTree *lsm, const MBR *region, LSMQueryResult *result) {
    if (!lsm || !region || !result) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_rdlock(&lsm->lock);
    
    /* Query memtable first */
    memtable_query_range(&lsm->memtable, region, result);
    lsm->stats.memtable_queries++;
    
    /* Query immutable memtable if present */
    if (lsm->immutable) {
        memtable_query_range(lsm->immutable, region, result);
    }
    
    /* Query each level (use bloom filters in production for efficiency) */
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        LSMLevel *level = &lsm->levels[i];
        
        if (!mbr_intersects(&level->bounds, region)) continue;
        
        for (size_t j = 0; j < level->run_count; j++) {
            LSMRun *run = level->runs[j];
            if (!mbr_intersects(&run->bounds, region)) continue;
            
            /* Query pages in run */
            for (size_t k = 0; k < run->page_count; k++) {
                Page *page = run->pages[k];
                if (!mbr_intersects(&page->header.extent, region)) continue;
                
                for (size_t m = 0; m < page->header.object_count; m++) {
                    SpatialObject *obj = &page->objects[m];
                    if (mbr_intersects(&obj->mbr, region)) {
                        lsm_result_add(result, obj);
                    }
                }
            }
            
            lsm->stats.level_queries++;
        }
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    return LSM_OK;
}

int lsm_query_nearest(LSMTree *lsm, Point point, SpatialObject *nearest) {
    if (!lsm || !nearest) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_rdlock(&lsm->lock);
    
    double best_dist = __DBL_MAX__;
    bool found = false;
    
    /* Search memtable using linear scan (safe after realloc) */
    for (size_t i = 0; i < lsm->memtable.count; i++) {
        LSMEntry *entry = &lsm->memtable.entries[i];
        if (!entry->is_delete) {
            double dist = point_distance(&point, &entry->object.centroid);
            if (dist < best_dist) {
                best_dist = dist;
                *nearest = entry->object;
                found = true;
            }
        }
    }
    
    /* Search levels (simplified - production would use priority queue) */
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        LSMLevel *level = &lsm->levels[i];
        
        for (size_t j = 0; j < level->run_count; j++) {
            LSMRun *run = level->runs[j];
            
            for (size_t k = 0; k < run->page_count; k++) {
                Page *page = run->pages[k];
                
                for (size_t m = 0; m < page->header.object_count; m++) {
                    SpatialObject *obj = &page->objects[m];
                    double dist = point_distance(&point, &obj->centroid);
                    if (dist < best_dist) {
                        best_dist = dist;
                        *nearest = *obj;
                        found = true;
                    }
                }
            }
        }
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    return found ? LSM_OK : LSM_ERR_NOT_FOUND;
}

int lsm_get(LSMTree *lsm, uint64_t object_id, SpatialObject *obj) {
    if (!lsm) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_rdlock(&lsm->lock);
    
    /* Check memtable first */
    bool is_delete;
    if (memtable_get(&lsm->memtable, object_id, obj, &is_delete) == LSM_OK) {
        pthread_rwlock_unlock(&lsm->lock);
        return is_delete ? LSM_ERR_NOT_FOUND : LSM_OK;
    }
    
    /* Check immutable memtable */
    if (lsm->immutable) {
        if (memtable_get(lsm->immutable, object_id, obj, &is_delete) == LSM_OK) {
            pthread_rwlock_unlock(&lsm->lock);
            return is_delete ? LSM_ERR_NOT_FOUND : LSM_OK;
        }
    }
    
    /* Search levels */
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        LSMLevel *level = &lsm->levels[i];
        
        for (size_t j = 0; j < level->run_count; j++) {
            LSMRun *run = level->runs[j];
            
            for (size_t k = 0; k < run->page_count; k++) {
                Page *page = run->pages[k];
                
                for (size_t m = 0; m < page->header.object_count; m++) {
                    if (page->objects[m].id == object_id) {
                        if (obj) *obj = page->objects[m];
                        pthread_rwlock_unlock(&lsm->lock);
                        return LSM_OK;
                    }
                }
            }
        }
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    return LSM_ERR_NOT_FOUND;
}

int lsm_flush(LSMTree *lsm) {
    if (!lsm) return LSM_ERR_NULL_PTR;
    
    pthread_rwlock_wrlock(&lsm->lock);
    
    if (lsm->memtable.count == 0) {
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_OK;
    }
    
    /* Create a new run from memtable */
    LSMRun *run = (LSMRun *)calloc(1, sizeof(LSMRun));
    if (!run) {
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_ALLOC;
    }
    
    run->run_id = lsm->next_run_id++;
    run->bounds = lsm->memtable.bounds;
    run->object_count = lsm->memtable.count;
    
    /* Create pages from memtable entries */
    size_t objects_per_page = MAX_OBJECTS_PER_PAGE;
    size_t page_count = (lsm->memtable.count + objects_per_page - 1) / objects_per_page;
    
    run->pages = (Page **)calloc(page_count, sizeof(Page *));
    if (!run->pages) {
        free(run);
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_ALLOC;
    }
    
    size_t entry_idx = 0;
    for (size_t i = 0; i < page_count; i++) {
        Page *page = page_create(i + 1, 0);
        if (!page) {
            for (size_t j = 0; j < i; j++) {
                page_free(run->pages[j]);
            }
            free(run->pages);
            free(run);
            pthread_rwlock_unlock(&lsm->lock);
            return LSM_ERR_ALLOC;
        }
        
        /* Add objects to page */
        while (entry_idx < lsm->memtable.count && !page_is_full(page)) {
            LSMEntry *entry = &lsm->memtable.entries[entry_idx];
            if (!entry->is_delete) {
                page_add_object(page, &entry->object);
            }
            entry_idx++;
        }
        
        run->pages[i] = page;
        run->page_count++;
    }
    
    /* Add run to level 0 */
    level_add_run(&lsm->levels[0], run);
    lsm->stats.flushes++;
    lsm->stats.total_pages += run->page_count;
    
    /* Clear memtable */
    memtable_free(&lsm->memtable);
    memtable_init(&lsm->memtable, lsm->config.memtable_size);
    
    /* Truncate WAL */
    if (lsm->wal) {
        wal_truncate(lsm->wal);
    }
    
    pthread_rwlock_unlock(&lsm->lock);
    
    /* Check if compaction is needed */
    if (level_needs_compaction(&lsm->levels[0])) {
        lsm_compact(lsm, 0);
    }
    
    return LSM_OK;
}

int lsm_compact(LSMTree *lsm, size_t level) {
    if (!lsm) return LSM_ERR_NULL_PTR;
    if (level >= LSM_MAX_LEVELS - 1) return LSM_ERR_FULL;
    
    pthread_rwlock_wrlock(&lsm->lock);
    
    if (lsm->is_compacting) {
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_COMPACTING;
    }
    
    lsm->is_compacting = true;
    
    LSMLevel *src = &lsm->levels[level];
    LSMLevel *dst = &lsm->levels[level + 1];
    
    if (src->run_count == 0) {
        lsm->is_compacting = false;
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_OK;
    }
    
    /* Merge all runs in source level into a new run */
    LSMRun *merged = (LSMRun *)calloc(1, sizeof(LSMRun));
    if (!merged) {
        lsm->is_compacting = false;
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_ALLOC;
    }
    
    merged->run_id = lsm->next_run_id++;
    merged->bounds = src->bounds;
    
    /* Collect all objects from source runs */
    size_t total_objects = 0;
    for (size_t i = 0; i < src->run_count; i++) {
        total_objects += src->runs[i]->object_count;
    }
    
    SpatialObject *all_objects = (SpatialObject *)malloc(total_objects * sizeof(SpatialObject));
    if (!all_objects) {
        free(merged);
        lsm->is_compacting = false;
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_ALLOC;
    }
    
    size_t obj_idx = 0;
    for (size_t i = 0; i < src->run_count; i++) {
        LSMRun *run = src->runs[i];
        for (size_t j = 0; j < run->page_count; j++) {
            Page *page = run->pages[j];
            for (size_t k = 0; k < page->header.object_count; k++) {
                all_objects[obj_idx++] = page->objects[k];
            }
        }
    }
    
    /* Create new pages for merged run */
    size_t objects_per_page = MAX_OBJECTS_PER_PAGE;
    size_t page_count = (obj_idx + objects_per_page - 1) / objects_per_page;
    
    merged->pages = (Page **)calloc(page_count, sizeof(Page *));
    if (!merged->pages) {
        free(all_objects);
        free(merged);
        lsm->is_compacting = false;
        pthread_rwlock_unlock(&lsm->lock);
        return LSM_ERR_ALLOC;
    }
    
    obj_idx = 0;
    for (size_t i = 0; i < page_count; i++) {
        Page *page = page_create(i + 1, 0);
        if (!page) {
            for (size_t j = 0; j < i; j++) {
                page_free(merged->pages[j]);
            }
            free(merged->pages);
            free(all_objects);
            free(merged);
            lsm->is_compacting = false;
            pthread_rwlock_unlock(&lsm->lock);
            return LSM_ERR_ALLOC;
        }
        
        while (obj_idx < total_objects && !page_is_full(page)) {
            page_add_object(page, &all_objects[obj_idx++]);
        }
        
        merged->pages[i] = page;
        merged->page_count++;
    }
    
    merged->object_count = total_objects;
    free(all_objects);
    
    /* Add merged run to destination level */
    level_add_run(dst, merged);
    
    /* Free source level runs */
    size_t old_pages = 0;
    for (size_t i = 0; i < src->run_count; i++) {
        LSMRun *run = src->runs[i];
        old_pages += run->page_count;
        for (size_t j = 0; j < run->page_count; j++) {
            page_free(run->pages[j]);
        }
        free(run->pages);
        free(run);
    }
    
    /* Clear source level */
    src->run_count = 0;
    src->total_objects = 0;
    src->bounds = mbr_empty();
    
    /* Update stats */
    lsm->stats.compactions++;
    lsm->stats.total_pages = lsm->stats.total_pages - old_pages + merged->page_count;
    
    /* Calculate write amplification */
    if (lsm->stats.inserts > 0) {
        lsm->stats.write_amplification = (double)lsm->stats.compactions / 
                                          (double)lsm->stats.inserts;
    }
    
    lsm->is_compacting = false;
    pthread_rwlock_unlock(&lsm->lock);
    
    /* Check if next level needs compaction */
    if (level_needs_compaction(dst)) {
        lsm_compact(lsm, level + 1);
    }
    
    return LSM_OK;
}

int lsm_compact_all(LSMTree *lsm) {
    if (!lsm) return LSM_ERR_NULL_PTR;
    
    /* First flush memtable */
    lsm_flush(lsm);
    
    /* Compact each level from bottom up */
    for (size_t i = 0; i < LSM_MAX_LEVELS - 1; i++) {
        if (lsm->levels[i].run_count > 0) {
            int err = lsm_compact(lsm, i);
            if (err != LSM_OK && err != LSM_ERR_COMPACTING) {
                return err;
            }
        }
    }
    
    return LSM_OK;
}

void lsm_get_stats(const LSMTree *lsm, LSMStats *stats) {
    if (!lsm || !stats) return;
    *stats = lsm->stats;
}

void lsm_print_stats(const LSMTree *lsm, FILE *out) {
    if (!lsm || !out) return;
    
    fprintf(out, "=== LSM-Tree Statistics ===\n");
    fprintf(out, "Inserts: %lu\n", (unsigned long)lsm->stats.inserts);
    fprintf(out, "Deletes: %lu\n", (unsigned long)lsm->stats.deletes);
    fprintf(out, "Flushes: %lu\n", (unsigned long)lsm->stats.flushes);
    fprintf(out, "Compactions: %lu\n", (unsigned long)lsm->stats.compactions);
    fprintf(out, "Total objects: %zu\n", lsm->stats.total_objects);
    fprintf(out, "Total pages: %zu\n", lsm->stats.total_pages);
    fprintf(out, "Write amplification: %.2f\n", lsm->stats.write_amplification);
    fprintf(out, "\n");
    
    fprintf(out, "MemTable: %zu / %zu entries\n", 
            lsm->memtable.count, lsm->memtable.max_size);
    
    fprintf(out, "\nLevels:\n");
    for (size_t i = 0; i < LSM_MAX_LEVELS; i++) {
        const LSMLevel *level = &lsm->levels[i];
        if (level->run_count > 0 || level->total_objects > 0) {
            fprintf(out, "  L%zu: %zu runs, %zu objects\n", 
                    i, level->run_count, level->total_objects);
        }
    }
}

