/**
 * @file spatial_ops.h
 * @brief Advanced spatial operations for the Urbis GIS system
 * 
 * Provides spatial analysis operations including:
 * - Buffer generation
 * - Geometry intersection/union/difference
 * - Spatial predicates (intersects, contains, within)
 * - Voronoi diagrams and Delaunay triangulation
 * - Spatial aggregation
 */

#ifndef URBIS_SPATIAL_OPS_H
#define URBIS_SPATIAL_OPS_H

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

#define BUFFER_DEFAULT_SEGMENTS 32
#define VORONOI_MAX_POINTS 100000

/* ============================================================================
 * Buffer Operations
 * ============================================================================ */

/**
 * @brief Create a buffer polygon around a point
 * @param center Center point
 * @param distance Buffer radius
 * @param segments Number of segments for circle approximation
 * @return New polygon representing the buffer, or NULL on error
 */
Polygon* buffer_point(const Point *center, double distance, int segments);

/**
 * @brief Create a buffer polygon around a linestring
 * @param line Input linestring
 * @param distance Buffer distance
 * @param segments Number of segments for rounded corners
 * @return New polygon representing the buffer, or NULL on error
 */
Polygon* buffer_linestring(const LineString *line, double distance, int segments);

/**
 * @brief Create a buffer polygon around a polygon
 * @param poly Input polygon
 * @param distance Buffer distance (positive = expand, negative = shrink)
 * @param segments Number of segments for rounded corners
 * @return New polygon representing the buffer, or NULL on error
 */
Polygon* buffer_polygon(const Polygon *poly, double distance, int segments);

/**
 * @brief Create a buffer around any spatial object
 * @param obj Input spatial object
 * @param distance Buffer distance
 * @param segments Number of segments for curves
 * @return New polygon representing the buffer, or NULL on error
 */
Polygon* spatial_buffer(const SpatialObject *obj, double distance, int segments);

/* ============================================================================
 * Spatial Predicates
 * ============================================================================ */

/**
 * @brief Check if two spatial objects intersect
 */
bool spatial_intersects(const SpatialObject *a, const SpatialObject *b);

/**
 * @brief Check if object A contains object B
 */
bool spatial_contains(const SpatialObject *a, const SpatialObject *b);

/**
 * @brief Check if object A is within object B
 */
bool spatial_within(const SpatialObject *a, const SpatialObject *b);

/**
 * @brief Check if object A is within distance of object B
 */
bool spatial_within_distance(const SpatialObject *a, const SpatialObject *b, double distance);

/**
 * @brief Calculate minimum distance between two spatial objects
 */
double spatial_distance(const SpatialObject *a, const SpatialObject *b);

/**
 * @brief Check if a point is inside a polygon
 */
bool point_in_polygon(const Point *p, const Polygon *poly);

/**
 * @brief Check if two line segments intersect
 */
bool segments_intersect(const Point *a1, const Point *a2, const Point *b1, const Point *b2);

/**
 * @brief Get intersection point of two line segments (if they intersect)
 */
bool segment_intersection_point(const Point *a1, const Point *a2, 
                                 const Point *b1, const Point *b2,
                                 Point *intersection);

/* ============================================================================
 * Geometry Set Operations
 * ============================================================================ */

/**
 * @brief Compute intersection of two polygons
 * @param a First polygon
 * @param b Second polygon
 * @return New polygon representing intersection, or NULL if no intersection
 */
Polygon* polygon_intersection(const Polygon *a, const Polygon *b);

/**
 * @brief Compute union of two polygons
 * @param a First polygon
 * @param b Second polygon
 * @return New polygon representing union, or NULL on error
 */
Polygon* polygon_union(const Polygon *a, const Polygon *b);

/**
 * @brief Compute difference of two polygons (A - B)
 * @param a First polygon
 * @param b Second polygon to subtract
 * @return New polygon representing difference, or NULL on error
 */
Polygon* polygon_difference(const Polygon *a, const Polygon *b);

/**
 * @brief Compute symmetric difference of two polygons
 * @param a First polygon
 * @param b Second polygon
 * @return New polygon representing symmetric difference, or NULL on error
 */
Polygon* polygon_symmetric_difference(const Polygon *a, const Polygon *b);

/* ============================================================================
 * Voronoi Diagram
 * ============================================================================ */

/**
 * @brief Voronoi cell with associated point
 */
typedef struct {
    Polygon cell;        /**< Voronoi cell polygon */
    uint64_t point_id;   /**< ID of generating point */
    Point site;          /**< Generating point location */
} VoronoiCell;

/**
 * @brief Voronoi diagram result
 */
typedef struct {
    VoronoiCell *cells;  /**< Array of Voronoi cells */
    size_t count;        /**< Number of cells */
    MBR bounds;          /**< Bounding box of diagram */
} VoronoiDiagram;

/**
 * @brief Generate Voronoi diagram from points
 * @param points Array of input points
 * @param point_ids Array of point IDs (can be NULL)
 * @param count Number of points
 * @param bounds Clipping bounds (can be NULL for auto-bounds)
 * @return Voronoi diagram, or NULL on error
 */
VoronoiDiagram* voronoi_create(const Point *points, const uint64_t *point_ids,
                                size_t count, const MBR *bounds);

/**
 * @brief Free Voronoi diagram
 */
void voronoi_free(VoronoiDiagram *diagram);

/**
 * @brief Find which Voronoi cell contains a point
 * @return Index of containing cell, or -1 if not found
 */
int voronoi_find_cell(const VoronoiDiagram *diagram, const Point *p);

/* ============================================================================
 * Delaunay Triangulation
 * ============================================================================ */

/**
 * @brief Triangle with vertex indices
 */
typedef struct {
    size_t v1, v2, v3;   /**< Indices into points array */
    Point circumcenter;  /**< Circumcenter of triangle */
    double circumradius; /**< Circumradius */
} DelaunayTriangle;

/**
 * @brief Delaunay triangulation result
 */
typedef struct {
    DelaunayTriangle *triangles; /**< Array of triangles */
    size_t count;                /**< Number of triangles */
    Point *points;               /**< Copy of input points */
    size_t num_points;           /**< Number of points */
} DelaunayTriangulation;

/**
 * @brief Generate Delaunay triangulation from points
 * @param points Array of input points
 * @param count Number of points
 * @return Delaunay triangulation, or NULL on error
 */
DelaunayTriangulation* delaunay_create(const Point *points, size_t count);

/**
 * @brief Free Delaunay triangulation
 */
void delaunay_free(DelaunayTriangulation *tri);

/**
 * @brief Find triangle containing a point
 * @return Index of containing triangle, or -1 if not found
 */
int delaunay_find_triangle(const DelaunayTriangulation *tri, const Point *p);

/**
 * @brief Get neighbors of a point in triangulation
 * @param tri Triangulation
 * @param point_idx Index of point
 * @param neighbors Output array for neighbor indices
 * @param max_neighbors Maximum neighbors to return
 * @return Number of neighbors found
 */
size_t delaunay_get_neighbors(const DelaunayTriangulation *tri, size_t point_idx,
                               size_t *neighbors, size_t max_neighbors);

/* ============================================================================
 * Spatial Join
 * ============================================================================ */

/**
 * @brief Spatial join types
 */
typedef enum {
    JOIN_INTERSECTS,  /**< Objects that intersect */
    JOIN_WITHIN,      /**< Objects within distance */
    JOIN_CONTAINS,    /**< A contains B */
    JOIN_NEAREST      /**< Nearest neighbor join */
} SpatialJoinType;

/**
 * @brief Join result pair
 */
typedef struct {
    uint64_t id_a;    /**< ID from first dataset */
    uint64_t id_b;    /**< ID from second dataset */
    double distance;  /**< Distance between objects */
} JoinPair;

/**
 * @brief Spatial join result
 */
typedef struct {
    JoinPair *pairs;  /**< Array of matching pairs */
    size_t count;     /**< Number of pairs */
    size_t capacity;  /**< Allocated capacity */
} SpatialJoinResult;

/**
 * @brief Create empty join result
 */
SpatialJoinResult* join_result_create(size_t initial_capacity);

/**
 * @brief Add pair to join result
 */
int join_result_add(SpatialJoinResult *result, uint64_t id_a, uint64_t id_b, double distance);

/**
 * @brief Free join result
 */
void join_result_free(SpatialJoinResult *result);

/* ============================================================================
 * Spatial Aggregation
 * ============================================================================ */

/**
 * @brief Aggregation types
 */
typedef enum {
    AGG_COUNT,   /**< Count of objects */
    AGG_SUM,     /**< Sum of property values */
    AGG_AVG,     /**< Average of property values */
    AGG_MIN,     /**< Minimum property value */
    AGG_MAX,     /**< Maximum property value */
    AGG_STDDEV   /**< Standard deviation */
} AggregationType;

/**
 * @brief Grid cell with aggregated value
 */
typedef struct {
    MBR bounds;       /**< Cell bounding box */
    double value;     /**< Aggregated value */
    size_t count;     /**< Number of objects in cell */
    double sum;       /**< Sum for incremental calculation */
    double sum_sq;    /**< Sum of squares for stddev */
} GridCell;

/**
 * @brief Grid aggregation result
 */
typedef struct {
    GridCell *cells;  /**< 2D array of cells (row-major) */
    size_t rows;      /**< Number of rows */
    size_t cols;      /**< Number of columns */
    MBR bounds;       /**< Overall bounds */
    double cell_width;  /**< Width of each cell */
    double cell_height; /**< Height of each cell */
} GridAggregation;

/**
 * @brief Create grid aggregation structure
 * @param bounds Overall bounds
 * @param cell_size Size of each grid cell
 * @return Grid aggregation structure, or NULL on error
 */
GridAggregation* grid_aggregation_create(const MBR *bounds, double cell_size);

/**
 * @brief Add object to grid aggregation
 * @param grid Grid aggregation
 * @param obj Spatial object
 * @param value Value to aggregate (for SUM, AVG, etc.)
 * @param agg_type Aggregation type
 */
void grid_aggregation_add(GridAggregation *grid, const SpatialObject *obj,
                          double value, AggregationType agg_type);

/**
 * @brief Finalize grid aggregation (compute averages, etc.)
 */
void grid_aggregation_finalize(GridAggregation *grid, AggregationType agg_type);

/**
 * @brief Get cell at grid position
 */
GridCell* grid_aggregation_get_cell(GridAggregation *grid, size_t row, size_t col);

/**
 * @brief Get cell containing point
 */
GridCell* grid_aggregation_get_cell_at(GridAggregation *grid, double x, double y);

/**
 * @brief Free grid aggregation
 */
void grid_aggregation_free(GridAggregation *grid);

/**
 * @brief Region aggregation result
 */
typedef struct {
    uint64_t region_id;  /**< ID of region polygon */
    double value;        /**< Aggregated value */
    size_t count;        /**< Number of objects in region */
} RegionAggregate;

/**
 * @brief Region aggregation result set
 */
typedef struct {
    RegionAggregate *regions; /**< Array of region results */
    size_t count;             /**< Number of regions */
} RegionAggregation;

/**
 * @brief Free region aggregation
 */
void region_aggregation_free(RegionAggregation *agg);

/* ============================================================================
 * Convex Hull
 * ============================================================================ */

/**
 * @brief Compute convex hull of points
 * @param points Array of input points
 * @param count Number of points
 * @param hull_count Output: number of points in hull
 * @return Array of hull points (caller must free), or NULL on error
 */
Point* convex_hull(const Point *points, size_t count, size_t *hull_count);

/**
 * @brief Compute convex hull of spatial object
 */
Polygon* spatial_convex_hull(const SpatialObject *obj);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Simplify a linestring using Douglas-Peucker algorithm
 * @param line Input linestring
 * @param tolerance Simplification tolerance
 * @return Simplified linestring, or NULL on error
 */
LineString* simplify_linestring(const LineString *line, double tolerance);

/**
 * @brief Simplify a polygon using Douglas-Peucker algorithm
 */
Polygon* simplify_polygon(const Polygon *poly, double tolerance);

/**
 * @brief Densify a linestring by adding points
 * @param line Input linestring
 * @param max_distance Maximum distance between points
 * @return Densified linestring, or NULL on error
 */
LineString* densify_linestring(const LineString *line, double max_distance);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_SPATIAL_OPS_H */

