/**
 * @file test_performance.c
 * @brief Performance benchmarks for Urbis improvements
 * 
 * Tests:
 * 1. Parallel KD-tree construction
 * 2. Page compression
 * 3. LSM-tree incremental updates
 */

#include "spatial_index.h"
#include "kdtree.h"
#include "compression.h"
#include "lsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

/* ============================================================================
 * Timing Utilities
 * ============================================================================ */

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static KDPointData* generate_random_points(size_t count) {
    KDPointData *points = malloc(count * sizeof(KDPointData));
    if (!points) return NULL;
    
    srand(42);  /* Fixed seed for reproducibility */
    
    for (size_t i = 0; i < count; i++) {
        points[i].point.x = (double)rand() / RAND_MAX * 1000.0;
        points[i].point.y = (double)rand() / RAND_MAX * 1000.0;
        points[i].object_id = i + 1;
        points[i].data = NULL;
    }
    
    return points;
}

static SpatialObject* generate_random_objects(size_t count) {
    SpatialObject *objects = calloc(count, sizeof(SpatialObject));
    if (!objects) return NULL;
    
    srand(42);
    
    for (size_t i = 0; i < count; i++) {
        double x = (double)rand() / RAND_MAX * 1000.0;
        double y = (double)rand() / RAND_MAX * 1000.0;
        double w = (double)rand() / RAND_MAX * 10.0 + 1.0;
        double h = (double)rand() / RAND_MAX * 10.0 + 1.0;
        
        objects[i].id = i + 1;
        objects[i].type = GEOM_POLYGON;
        objects[i].centroid = point_create(x + w/2, y + h/2);
        objects[i].mbr = mbr_create(x, y, x + w, y + h);
    }
    
    return objects;
}

/* ============================================================================
 * Benchmark: Parallel KD-Tree Construction
 * ============================================================================ */

static void benchmark_parallel_kdtree(void) {
    printf("\n=== Parallel KD-Tree Construction Benchmark ===\n\n");
    
    size_t sizes[] = {10000, 50000, 100000};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    printf("%-12s %-15s %-15s %-10s\n", 
           "Points", "Sequential(ms)", "Parallel(ms)", "Speedup");
    printf("%-12s %-15s %-15s %-10s\n",
           "------", "-------------", "-----------", "-------");
    
    for (size_t i = 0; i < num_sizes; i++) {
        size_t count = sizes[i];
        KDPointData *points = generate_random_points(count);
        if (!points) continue;
        
        /* Sequential build */
        KDTree tree_seq;
        kdtree_init(&tree_seq);
        
        double start = get_time_ms();
        kdtree_bulk_load(&tree_seq, points, count);
        double seq_time = get_time_ms() - start;
        
        kdtree_free(&tree_seq);
        
        /* Parallel build */
        KDTree tree_par;
        kdtree_init(&tree_par);
        
        KDParallelConfig config = kdtree_parallel_default_config();
        config.parallel_threshold = 5000;
        
        start = get_time_ms();
        kdtree_bulk_load_parallel(&tree_par, points, count, &config);
        double par_time = get_time_ms() - start;
        
        kdtree_free(&tree_par);
        
        double speedup = seq_time / par_time;
        
        printf("%-12zu %-15.2f %-15.2f %-10.2fx\n", 
               count, seq_time, par_time, speedup);
        
        free(points);
    }
}

/* ============================================================================
 * Benchmark: Compression
 * ============================================================================ */

static void benchmark_compression(void) {
    printf("\n=== Compression Benchmark ===\n\n");
    
    size_t sizes[] = {1024, 4096, 16384, 65536, 262144};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    printf("%-12s %-15s %-15s %-12s %-15s\n",
           "Size", "Original(B)", "Compressed(B)", "Ratio", "Speed(MB/s)");
    printf("%-12s %-15s %-15s %-12s %-15s\n",
           "----", "-----------", "-------------", "-----", "-----------");
    
    CompressionConfig config = compression_default_config();
    
    for (size_t i = 0; i < num_sizes; i++) {
        size_t size = sizes[i];
        
        /* Generate test data (spatial-like data with some patterns) */
        uint8_t *data = malloc(size);
        if (!data) continue;
        
        srand(42);
        for (size_t j = 0; j < size; j++) {
            /* Mix of random and patterned data */
            if (j % 8 == 0) {
                data[j] = (uint8_t)(j % 256);
            } else {
                data[j] = (uint8_t)(rand() % 256);
            }
        }
        
        /* Compress */
        size_t bound = compress_bound(size, COMPRESS_LZ4);
        uint8_t *compressed = malloc(bound);
        if (!compressed) {
            free(data);
            continue;
        }
        
        size_t compressed_size = 0;
        double start = get_time_ms();
        
        int iterations = 100;
        for (int it = 0; it < iterations; it++) {
            compress_data(data, size, compressed, bound, &compressed_size, &config);
        }
        
        double elapsed = get_time_ms() - start;
        double speed_mbps = (size * iterations / 1024.0 / 1024.0) / (elapsed / 1000.0);
        
        double ratio = compressed_size > 0 ? (double)compressed_size / size : 1.0;
        
        printf("%-12zu %-15zu %-15zu %-12.2f %-15.2f\n",
               size, size, compressed_size, ratio, speed_mbps);
        
        free(data);
        free(compressed);
    }
}

/* ============================================================================
 * Benchmark: LSM-Tree Incremental Updates
 * ============================================================================ */

static void benchmark_lsm(void) {
    printf("\n=== LSM-Tree Incremental Updates Benchmark ===\n\n");
    
    size_t counts[] = {1000, 5000, 10000};
    size_t num_counts = sizeof(counts) / sizeof(counts[0]);
    
    printf("%-12s %-15s %-15s %-15s %-12s\n",
           "Objects", "Insert(ms)", "Query(ms)", "Flush(ms)", "Ops/sec");
    printf("%-12s %-15s %-15s %-15s %-12s\n",
           "-------", "---------", "--------", "--------", "-------");
    
    for (size_t i = 0; i < num_counts; i++) {
        size_t count = counts[i];
        SpatialObject *objects = generate_random_objects(count);
        if (!objects) continue;
        
        LSMConfig config = lsm_default_config();
        config.memtable_size = count * 2;  /* Prevent early flush */
        
        LSMTree *lsm = lsm_create(&config);
        if (!lsm) {
            free(objects);
            continue;
        }
        
        /* Insert benchmark */
        double start = get_time_ms();
        for (size_t j = 0; j < count; j++) {
            lsm_insert(lsm, &objects[j]);
        }
        double insert_time = get_time_ms() - start;
        
        /* Query benchmark */
        MBR query = mbr_create(250, 250, 750, 750);  /* Center region */
        LSMQueryResult result;
        lsm_result_init(&result, 1000);
        
        start = get_time_ms();
        int query_iterations = 100;
        for (int it = 0; it < query_iterations; it++) {
            lsm_result_clear(&result);
            lsm_query_range(lsm, &query, &result);
        }
        double query_time = (get_time_ms() - start) / query_iterations;
        
        lsm_result_free(&result);
        
        /* Flush benchmark */
        start = get_time_ms();
        lsm_flush(lsm);
        double flush_time = get_time_ms() - start;
        
        double ops_per_sec = count / (insert_time / 1000.0);
        
        printf("%-12zu %-15.2f %-15.2f %-15.2f %-12.0f\n",
               count, insert_time, query_time, flush_time, ops_per_sec);
        
        lsm_destroy(lsm);
        free(objects);
    }
}

/* ============================================================================
 * Benchmark: Full Index with All Improvements
 * ============================================================================ */

static void benchmark_full_index(void) {
    printf("\n=== Full Index Benchmark (All Improvements) ===\n\n");
    
    size_t count = 10000;
    SpatialObject *objects = generate_random_objects(count);
    if (!objects) {
        printf("Failed to generate test data\n");
        return;
    }
    
    /* Standard index */
    printf("Testing with %zu objects...\n\n", count);
    
    SpatialIndexConfig config_std = spatial_index_default_config();
    SpatialIndex *idx_std = spatial_index_create(&config_std);
    
    double start = get_time_ms();
    for (size_t i = 0; i < count; i++) {
        spatial_index_insert(idx_std, &objects[i]);
    }
    double std_insert = get_time_ms() - start;
    
    start = get_time_ms();
    spatial_index_build(idx_std);
    double std_build = get_time_ms() - start;
    
    /* Index with parallel build */
    SpatialIndexConfig config_par = spatial_index_default_config();
    config_par.use_parallel_build = true;
    config_par.parallel_threshold = 10000;
    SpatialIndex *idx_par = spatial_index_create(&config_par);
    
    for (size_t i = 0; i < count; i++) {
        spatial_index_insert(idx_par, &objects[i]);
    }
    
    start = get_time_ms();
    spatial_index_build(idx_par);
    double par_build = get_time_ms() - start;
    
    /* Index with LSM */
    SpatialIndexConfig config_lsm = spatial_index_default_config();
    config_lsm.enable_lsm = true;
    config_lsm.lsm_config = lsm_default_config();
    config_lsm.lsm_config.memtable_size = count * 2;
    SpatialIndex *idx_lsm = spatial_index_create(&config_lsm);
    
    start = get_time_ms();
    for (size_t i = 0; i < count; i++) {
        spatial_index_insert(idx_lsm, &objects[i]);
    }
    double lsm_insert = get_time_ms() - start;
    
    /* Query benchmark */
    MBR query = mbr_create(250, 250, 750, 750);
    SpatialQueryResult result;
    spatial_result_init(&result, 1000);
    
    start = get_time_ms();
    int iterations = 100;
    for (int it = 0; it < iterations; it++) {
        spatial_result_clear(&result);
        spatial_index_query_range(idx_std, &query, &result);
    }
    double std_query = (get_time_ms() - start) / iterations;
    size_t std_results = result.count;
    
    start = get_time_ms();
    for (int it = 0; it < iterations; it++) {
        spatial_result_clear(&result);
        spatial_index_query_range(idx_lsm, &query, &result);
    }
    double lsm_query = (get_time_ms() - start) / iterations;
    size_t lsm_results = result.count;
    
    spatial_result_free(&result);
    
    /* Print results */
    printf("%-25s %-15s %-15s\n", "Metric", "Standard", "Improved");
    printf("%-25s %-15s %-15s\n", "------", "--------", "--------");
    printf("%-25s %-15.2f %-15.2f ms\n", "Insert Time", std_insert, lsm_insert);
    printf("%-25s %-15.2f %-15.2f ms\n", "Build Time", std_build, par_build);
    printf("%-25s %-15.2f %-15.2f ms\n", "Query Time", std_query, lsm_query);
    printf("%-25s %-15zu %-15zu\n", "Query Results", std_results, lsm_results);
    printf("\n");
    printf("Build Speedup: %.2fx\n", std_build / par_build);
    printf("Insert Speedup (LSM): %.2fx\n", std_insert / lsm_insert);
    
    /* Cleanup */
    spatial_index_destroy(idx_std);
    spatial_index_destroy(idx_par);
    spatial_index_destroy(idx_lsm);
    free(objects);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("============================================\n");
    printf("   Urbis Performance Benchmark Suite\n");
    printf("============================================\n");
    
    benchmark_parallel_kdtree();
    benchmark_compression();
    benchmark_lsm();
    benchmark_full_index();
    
    printf("\n============================================\n");
    printf("   Benchmark Complete\n");
    printf("============================================\n");
    
    return 0;
}

