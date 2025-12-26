/**
 * @file city_demo.c
 * @brief Example demonstrating Urbis spatial index for city map data
 * 
 * This example shows how to:
 * - Load city map data (roads, buildings, landmarks)
 * - Build a spatial index with KD-tree partitioning
 * - Query adjacent pages to minimize disk seeks
 * - Perform spatial queries
 */

#include "../include/urbis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ============================================================================
 * Sample City Data Generator
 * ============================================================================ */

/**
 * @brief Generate a random road (linestring)
 */
static void generate_road(UrbisIndex *idx, double base_x, double base_y) {
    Point points[10];
    int num_points = 5 + rand() % 6;  /* 5-10 points */
    
    double x = base_x;
    double y = base_y;
    
    for (int i = 0; i < num_points; i++) {
        points[i] = urbis_point(x, y);
        x += (rand() % 200) - 50;  /* Random walk */
        y += (rand() % 200) - 50;
    }
    
    urbis_insert_linestring(idx, points, num_points);
}

/**
 * @brief Generate a random building (polygon)
 */
static void generate_building(UrbisIndex *idx, double x, double y) {
    double w = 20 + rand() % 50;  /* Width 20-70 */
    double h = 20 + rand() % 50;  /* Height 20-70 */
    
    Point exterior[5] = {
        urbis_point(x, y),
        urbis_point(x + w, y),
        urbis_point(x + w, y + h),
        urbis_point(x, y + h),
        urbis_point(x, y)  /* Close ring */
    };
    
    urbis_insert_polygon(idx, exterior, 5);
}

/**
 * @brief Generate a city with roads, buildings, and landmarks
 */
static void generate_city(UrbisIndex *idx, int num_roads, int num_buildings, 
                          int num_landmarks, double city_size) {
    printf("Generating city data...\n");
    printf("  City size: %.0f x %.0f\n", city_size, city_size);
    printf("  Roads: %d\n", num_roads);
    printf("  Buildings: %d\n", num_buildings);
    printf("  Landmarks: %d\n", num_landmarks);
    
    /* Generate roads */
    for (int i = 0; i < num_roads; i++) {
        double x = (double)(rand() % (int)city_size);
        double y = (double)(rand() % (int)city_size);
        generate_road(idx, x, y);
    }
    
    /* Generate buildings along a grid */
    int buildings_generated = 0;
    double block_size = city_size / sqrt(num_buildings);
    
    for (double x = 0; x < city_size && buildings_generated < num_buildings; x += block_size) {
        for (double y = 0; y < city_size && buildings_generated < num_buildings; y += block_size) {
            /* Offset within block */
            double bx = x + (rand() % (int)(block_size * 0.8));
            double by = y + (rand() % (int)(block_size * 0.8));
            generate_building(idx, bx, by);
            buildings_generated++;
        }
    }
    
    /* Generate landmarks (points of interest) */
    for (int i = 0; i < num_landmarks; i++) {
        double x = (double)(rand() % (int)city_size);
        double y = (double)(rand() % (int)city_size);
        urbis_insert_point(idx, x, y);
    }
    
    printf("Total objects generated: %zu\n\n", urbis_count(idx));
}

/* ============================================================================
 * Demo Functions
 * ============================================================================ */

/**
 * @brief Demonstrate range query
 */
static void demo_range_query(UrbisIndex *idx, double city_size) {
    printf("=== Range Query Demo ===\n");
    
    /* Query a region in the center of the city */
    double center = city_size / 2;
    double radius = city_size / 10;
    
    MBR region = urbis_mbr(center - radius, center - radius,
                          center + radius, center + radius);
    
    printf("Querying region: (%.0f, %.0f) to (%.0f, %.0f)\n",
           region.min_x, region.min_y, region.max_x, region.max_y);
    
    clock_t start = clock();
    UrbisObjectList *result = urbis_query_range(idx, &region);
    clock_t end = clock();
    
    if (result) {
        printf("Found %zu objects in %.3f ms\n", result->count,
               (double)(end - start) / CLOCKS_PER_SEC * 1000);
        
        /* Count by type */
        int points = 0, lines = 0, polygons = 0;
        for (size_t i = 0; i < result->count; i++) {
            switch (result->objects[i]->type) {
                case GEOM_POINT: points++; break;
                case GEOM_LINESTRING: lines++; break;
                case GEOM_POLYGON: polygons++; break;
            }
        }
        printf("  Points (landmarks): %d\n", points);
        printf("  LineStrings (roads): %d\n", lines);
        printf("  Polygons (buildings): %d\n", polygons);
        
        urbis_object_list_free(result);
    }
    printf("\n");
}

/**
 * @brief Demonstrate adjacent page lookup (key feature for disk optimization)
 */
static void demo_adjacent_pages(UrbisIndex *idx, double city_size) {
    printf("=== Adjacent Pages Demo (Disk-Aware) ===\n");
    
    /* Query different regions and compare seek estimates */
    struct {
        const char *name;
        MBR region;
    } queries[] = {
        { "Small region (city center)", 
          urbis_mbr(city_size*0.45, city_size*0.45, city_size*0.55, city_size*0.55) },
        { "Medium region (quarter city)",
          urbis_mbr(0, 0, city_size*0.5, city_size*0.5) },
        { "Large region (half city)",
          urbis_mbr(0, 0, city_size, city_size*0.5) },
    };
    
    for (int i = 0; i < 3; i++) {
        printf("\n%s:\n", queries[i].name);
        printf("  Region: (%.0f,%.0f) to (%.0f,%.0f)\n",
               queries[i].region.min_x, queries[i].region.min_y,
               queries[i].region.max_x, queries[i].region.max_y);
        
        UrbisPageList *pages = urbis_find_adjacent_pages(idx, &queries[i].region);
        
        if (pages) {
            printf("  Pages accessed: %zu\n", pages->count);
            printf("  Estimated disk seeks: %zu\n", pages->estimated_seeks);
            
            /* Pages on same track = no seeks */
            if (pages->count > 0) {
                double seek_ratio = (double)pages->estimated_seeks / pages->count;
                printf("  Seek ratio: %.2f (lower is better)\n", seek_ratio);
            }
            
            urbis_page_list_free(pages);
        }
    }
    printf("\n");
}

/**
 * @brief Demonstrate k-nearest neighbor query
 */
static void demo_knn_query(UrbisIndex *idx, double city_size) {
    printf("=== K-Nearest Neighbor Query Demo ===\n");
    
    /* Query point in center of city */
    double qx = city_size / 2;
    double qy = city_size / 2;
    
    printf("Finding 5 nearest objects to (%.0f, %.0f)...\n", qx, qy);
    
    clock_t start = clock();
    UrbisObjectList *result = urbis_query_knn(idx, qx, qy, 5);
    clock_t end = clock();
    
    if (result) {
        printf("Found %zu nearest neighbors in %.3f ms:\n", result->count,
               (double)(end - start) / CLOCKS_PER_SEC * 1000);
        
        for (size_t i = 0; i < result->count && i < 5; i++) {
            SpatialObject *obj = result->objects[i];
            const char *type_str = 
                obj->type == GEOM_POINT ? "Point" :
                obj->type == GEOM_LINESTRING ? "Line" : "Polygon";
            
            Point query_pt = urbis_point(qx, qy);
            double dist = point_distance(&query_pt, &obj->centroid);
            printf("  %zu. %s at (%.1f, %.1f), distance: %.2f\n",
                   i + 1, type_str, obj->centroid.x, obj->centroid.y, dist);
        }
        
        urbis_object_list_free(result);
    }
    printf("\n");
}

/**
 * @brief Demonstrate index statistics
 */
static void demo_statistics(UrbisIndex *idx) {
    printf("=== Index Statistics ===\n");
    
    UrbisStats stats;
    urbis_get_stats(idx, &stats);
    
    printf("Objects: %zu\n", stats.total_objects);
    printf("Blocks (KD-tree partitions): %zu\n", stats.total_blocks);
    printf("Pages: %zu\n", stats.total_pages);
    printf("Tracks: %zu\n", stats.total_tracks);
    printf("\n");
    printf("KD-tree depth: %zu\n", stats.kdtree_depth);
    printf("Quadtree depth: %zu\n", stats.quadtree_depth);
    printf("Avg objects/page: %.2f\n", stats.avg_objects_per_page);
    printf("Page utilization: %.1f%%\n", stats.page_utilization * 100);
    printf("\n");
    printf("Spatial bounds:\n");
    printf("  Min: (%.2f, %.2f)\n", stats.bounds.min_x, stats.bounds.min_y);
    printf("  Max: (%.2f, %.2f)\n", stats.bounds.max_x, stats.bounds.max_y);
    printf("\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("Urbis City Map Spatial Index Demo\n");
    printf("Version: %s\n", urbis_version());
    printf("========================================\n\n");
    
    /* Configuration */
    double city_size = 10000.0;  /* 10km x 10km city */
    int num_roads = 200;
    int num_buildings = 500;
    int num_landmarks = 100;
    
    /* Parse command line arguments */
    if (argc > 1) {
        city_size = atof(argv[1]);
    }
    if (argc > 2) {
        num_roads = atoi(argv[2]);
        num_buildings = num_roads * 3;
        num_landmarks = num_roads / 2;
    }
    
    /* Seed random number generator */
    srand((unsigned int)time(NULL));
    
    /* Create index with custom configuration */
    UrbisConfig config = urbis_default_config();
    config.block_size = 256;      /* Smaller blocks for demo */
    config.page_capacity = 32;    /* Smaller pages for demo */
    config.enable_quadtree = true;
    
    UrbisIndex *idx = urbis_create(&config);
    if (!idx) {
        fprintf(stderr, "Failed to create index\n");
        return 1;
    }
    
    /* Generate city data */
    generate_city(idx, num_roads, num_buildings, num_landmarks, city_size);
    
    /* Build spatial index */
    printf("Building spatial index...\n");
    clock_t start = clock();
    int err = urbis_build(idx);
    clock_t end = clock();
    
    if (err != URBIS_OK) {
        fprintf(stderr, "Failed to build index\n");
        urbis_destroy(idx);
        return 1;
    }
    
    printf("Index built in %.3f ms\n\n", 
           (double)(end - start) / CLOCKS_PER_SEC * 1000);
    
    /* Run demos */
    demo_statistics(idx);
    demo_range_query(idx, city_size);
    demo_adjacent_pages(idx, city_size);
    demo_knn_query(idx, city_size);
    
    /* Cleanup */
    urbis_destroy(idx);
    
    printf("Demo complete!\n");
    return 0;
}

