/**
 * @file real_map_demo.c
 * @brief Demo using real OpenStreetMap data
 * 
 * This example loads real city map data (buildings, roads, POIs)
 * downloaded from OpenStreetMap and demonstrates the disk-aware
 * spatial indexing capabilities.
 */

#include "../include/urbis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Check if a file exists
 */
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/**
 * @brief Get file size
 */
static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return st.st_size;
}

/**
 * @brief Format large numbers with commas
 */
static void format_number(size_t n, char *buf, size_t buf_size) {
    if (n < 1000) {
        snprintf(buf, buf_size, "%zu", n);
        return;
    }
    
    char temp[32];
    snprintf(temp, sizeof(temp), "%zu", n);
    size_t len = strlen(temp);
    
    size_t j = 0;
    for (size_t i = 0; i < len && j < buf_size - 1; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            buf[j++] = ',';
        }
        buf[j++] = temp[i];
    }
    buf[j] = '\0';
}

/* ============================================================================
 * Demo Functions
 * ============================================================================ */

/**
 * @brief Demonstrate loading and indexing real map data
 */
static int demo_load_real_data(const char *geojson_path) {
    printf("========================================\n");
    printf("Urbis Real Map Demo\n");
    printf("Version: %s\n", urbis_version());
    printf("========================================\n\n");
    
    /* Check file */
    if (!file_exists(geojson_path)) {
        fprintf(stderr, "Error: GeoJSON file not found: %s\n", geojson_path);
        fprintf(stderr, "\nTo download real map data, run:\n");
        fprintf(stderr, "  ./examples/download_osm.sh\n");
        return 1;
    }
    
    long fsize = file_size(geojson_path);
    printf("Loading: %s\n", geojson_path);
    printf("File size: %.2f KB\n\n", fsize / 1024.0);
    
    /* Create index with optimized configuration */
    UrbisConfig config = urbis_default_config();
    config.block_size = 512;       /* Smaller blocks for dense city data */
    config.page_capacity = 32;     /* Reasonable page size */
    config.cache_size = 256;       /* Larger cache for real data */
    config.enable_quadtree = true;
    
    UrbisIndex *idx = urbis_create(&config);
    if (!idx) {
        fprintf(stderr, "Error: Failed to create index\n");
        return 1;
    }
    
    /* Load GeoJSON data */
    printf("=== Loading GeoJSON Data ===\n");
    clock_t start = clock();
    
    int err = urbis_load_geojson(idx, geojson_path);
    if (err != URBIS_OK) {
        fprintf(stderr, "Error: Failed to load GeoJSON (error %d)\n", err);
        urbis_destroy(idx);
        return 1;
    }
    
    clock_t end = clock();
    double load_time = (double)(end - start) / CLOCKS_PER_SEC * 1000;
    
    char num_buf[32];
    format_number(urbis_count(idx), num_buf, sizeof(num_buf));
    printf("Loaded %s features in %.2f ms\n", num_buf, load_time);
    printf("Load rate: %.0f features/sec\n\n", urbis_count(idx) / (load_time / 1000));
    
    /* Build spatial index */
    printf("=== Building Spatial Index ===\n");
    start = clock();
    
    err = urbis_build(idx);
    if (err != URBIS_OK) {
        fprintf(stderr, "Error: Failed to build index\n");
        urbis_destroy(idx);
        return 1;
    }
    
    end = clock();
    double build_time = (double)(end - start) / CLOCKS_PER_SEC * 1000;
    printf("Index built in %.2f ms\n\n", build_time);
    
    /* Print statistics */
    printf("=== Index Statistics ===\n");
    UrbisStats stats;
    urbis_get_stats(idx, &stats);
    
    format_number(stats.total_objects, num_buf, sizeof(num_buf));
    printf("Total objects: %s\n", num_buf);
    printf("Blocks (KD-tree partitions): %zu\n", stats.total_blocks);
    printf("Pages: %zu\n", stats.total_pages);
    printf("Tracks (disk groups): %zu\n", stats.total_tracks);
    printf("\n");
    printf("KD-tree depth: %zu\n", stats.kdtree_depth);
    printf("Quadtree depth: %zu\n", stats.quadtree_depth);
    printf("Avg objects/page: %.2f\n", stats.avg_objects_per_page);
    printf("Page utilization: %.1f%%\n", stats.page_utilization * 100);
    printf("\n");
    printf("Spatial bounds:\n");
    printf("  Longitude: %.6f to %.6f\n", stats.bounds.min_x, stats.bounds.max_x);
    printf("  Latitude:  %.6f to %.6f\n", stats.bounds.min_y, stats.bounds.max_y);
    printf("\n");
    
    /* Spatial queries */
    printf("=== Spatial Query Performance ===\n");
    
    /* Calculate center of the data */
    double center_lon = (stats.bounds.min_x + stats.bounds.max_x) / 2;
    double center_lat = (stats.bounds.min_y + stats.bounds.max_y) / 2;
    double width = stats.bounds.max_x - stats.bounds.min_x;
    double height = stats.bounds.max_y - stats.bounds.min_y;
    
    /* Test different query sizes */
    struct {
        const char *name;
        double size_pct;
    } query_sizes[] = {
        { "Small (1%)", 0.01 },
        { "Medium (5%)", 0.05 },
        { "Large (25%)", 0.25 },
        { "Full extent", 1.0 }
    };
    
    printf("\nRange Queries:\n");
    printf("%-15s %10s %10s %12s\n", "Query Size", "Objects", "Time (ms)", "Rate");
    printf("%-15s %10s %10s %12s\n", "-----------", "-------", "---------", "--------");
    
    for (int i = 0; i < 4; i++) {
        double w = width * query_sizes[i].size_pct;
        double h = height * query_sizes[i].size_pct;
        
        MBR region = urbis_mbr(
            center_lon - w/2, center_lat - h/2,
            center_lon + w/2, center_lat + h/2
        );
        
        start = clock();
        UrbisObjectList *result = urbis_query_range(idx, &region);
        end = clock();
        
        double query_time = (double)(end - start) / CLOCKS_PER_SEC * 1000;
        
        if (result) {
            char rate_buf[32];
            if (query_time > 0) {
                snprintf(rate_buf, sizeof(rate_buf), "%.0f/ms", result->count / query_time);
            } else {
                snprintf(rate_buf, sizeof(rate_buf), "instant");
            }
            
            printf("%-15s %10zu %10.3f %12s\n", 
                   query_sizes[i].name, result->count, query_time, rate_buf);
            urbis_object_list_free(result);
        }
    }
    
    /* Adjacent page analysis */
    printf("\n=== Disk-Aware Performance Analysis ===\n");
    printf("\nAdjacent Page Queries (demonstrating seek minimization):\n");
    printf("%-15s %8s %8s %10s %12s\n", 
           "Query Size", "Pages", "Seeks", "Seek Ratio", "Status");
    printf("%-15s %8s %8s %10s %12s\n", 
           "-----------", "-----", "-----", "----------", "------");
    
    for (int i = 0; i < 3; i++) {
        double w = width * query_sizes[i].size_pct;
        double h = height * query_sizes[i].size_pct;
        
        MBR region = urbis_mbr(
            center_lon - w/2, center_lat - h/2,
            center_lon + w/2, center_lat + h/2
        );
        
        UrbisPageList *pages = urbis_find_adjacent_pages(idx, &region);
        
        if (pages && pages->count > 0) {
            double seek_ratio = (double)pages->estimated_seeks / pages->count;
            const char *status = seek_ratio < 0.3 ? "EXCELLENT" :
                                 seek_ratio < 0.5 ? "GOOD" :
                                 seek_ratio < 0.7 ? "OK" : "POOR";
            
            printf("%-15s %8zu %8zu %10.2f %12s\n",
                   query_sizes[i].name, pages->count, pages->estimated_seeks,
                   seek_ratio, status);
            
            urbis_page_list_free(pages);
        }
    }
    
    printf("\nNote: Lower seek ratio = better disk I/O performance\n");
    printf("      Pages on same track require no additional seeks\n");
    
    /* K-NN Query */
    printf("\n=== K-Nearest Neighbor Query ===\n");
    printf("Finding 10 nearest features to center point...\n");
    printf("Query point: (%.6f, %.6f)\n\n", center_lon, center_lat);
    
    start = clock();
    UrbisObjectList *knn_result = urbis_query_knn(idx, center_lon, center_lat, 10);
    end = clock();
    
    if (knn_result) {
        printf("Found %zu neighbors in %.3f ms:\n", knn_result->count,
               (double)(end - start) / CLOCKS_PER_SEC * 1000);
        
        for (size_t i = 0; i < knn_result->count && i < 5; i++) {
            SpatialObject *obj = knn_result->objects[i];
            const char *type_str = 
                obj->type == GEOM_POINT ? "POI" :
                obj->type == GEOM_LINESTRING ? "Road" : "Building";
            
            Point query_pt = urbis_point(center_lon, center_lat);
            double dist = point_distance(&query_pt, &obj->centroid);
            
            printf("  %zu. %s at (%.6f, %.6f), distance: %.6f\n",
                   i + 1, type_str, obj->centroid.x, obj->centroid.y, dist);
        }
        
        urbis_object_list_free(knn_result);
    }
    
    /* Cleanup */
    printf("\n========================================\n");
    printf("Demo complete!\n");
    printf("========================================\n");
    
    urbis_destroy(idx);
    return 0;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    const char *geojson_path = "examples/data/san_francisco.geojson";
    
    if (argc > 1) {
        geojson_path = argv[1];
    }
    
    return demo_load_real_data(geojson_path);
}

