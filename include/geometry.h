/**
 * @file geometry.h
 * @brief Geometry primitives and operations for the Urbis GIS system
 * 
 * Provides Point, LineString, Polygon, and MBR (Minimum Bounding Rectangle)
 * types along with centroid calculation and spatial operations.
 */

#ifndef URBIS_GEOMETRY_H
#define URBIS_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Basic Types
 * ============================================================================ */

/**
 * @brief 2D Point structure
 */
typedef struct {
    double x;
    double y;
} Point;

/**
 * @brief LineString - a sequence of connected points
 */
typedef struct {
    Point *points;
    size_t count;
    size_t capacity;
} LineString;

/**
 * @brief Polygon with exterior ring and optional holes
 */
typedef struct {
    Point *exterior;          /**< Exterior ring points */
    size_t ext_count;         /**< Number of exterior points */
    size_t ext_capacity;      /**< Capacity of exterior array */
    Point **holes;            /**< Array of interior rings (holes) */
    size_t *hole_counts;      /**< Number of points in each hole */
    size_t *hole_capacities;  /**< Capacity of each hole array */
    size_t num_holes;         /**< Number of holes */
    size_t holes_capacity;    /**< Capacity of holes array */
} Polygon;

/**
 * @brief Minimum Bounding Rectangle (axis-aligned bounding box)
 */
typedef struct {
    double min_x;
    double min_y;
    double max_x;
    double max_y;
} MBR;

/**
 * @brief Geometry type enumeration
 */
typedef enum {
    GEOM_POINT = 0,
    GEOM_LINESTRING = 1,
    GEOM_POLYGON = 2
} GeomType;

/**
 * @brief Spatial object containing geometry and metadata
 */
typedef struct {
    uint64_t id;              /**< Unique identifier */
    GeomType type;            /**< Geometry type */
    union {
        Point point;
        LineString line;
        Polygon polygon;
    } geom;
    Point centroid;           /**< Computed centroid for indexing */
    MBR mbr;                  /**< Bounding box */
    void *properties;         /**< User-defined properties */
    size_t properties_size;   /**< Size of properties data */
} SpatialObject;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    GEOM_OK = 0,
    GEOM_ERR_NULL_PTR = -1,
    GEOM_ERR_ALLOC = -2,
    GEOM_ERR_INVALID_GEOM = -3,
    GEOM_ERR_EMPTY_GEOM = -4,
    GEOM_ERR_INDEX_OUT_OF_BOUNDS = -5
} GeomError;

/* ============================================================================
 * Point Operations
 * ============================================================================ */

/**
 * @brief Create a point
 */
Point point_create(double x, double y);

/**
 * @brief Calculate squared distance between two points
 */
double point_distance_sq(const Point *a, const Point *b);

/**
 * @brief Calculate Euclidean distance between two points
 */
double point_distance(const Point *a, const Point *b);

/**
 * @brief Check if two points are equal (within epsilon)
 */
bool point_equals(const Point *a, const Point *b, double epsilon);

/* ============================================================================
 * LineString Operations
 * ============================================================================ */

/**
 * @brief Initialize a linestring with given capacity
 */
int linestring_init(LineString *ls, size_t capacity);

/**
 * @brief Free linestring memory
 */
void linestring_free(LineString *ls);

/**
 * @brief Add a point to the linestring
 */
int linestring_add_point(LineString *ls, Point p);

/**
 * @brief Calculate linestring centroid
 */
int linestring_centroid(const LineString *ls, Point *centroid);

/**
 * @brief Calculate linestring MBR
 */
int linestring_mbr(const LineString *ls, MBR *mbr);

/**
 * @brief Calculate total length of linestring
 */
double linestring_length(const LineString *ls);

/**
 * @brief Deep copy a linestring
 */
int linestring_copy(LineString *dest, const LineString *src);

/* ============================================================================
 * Polygon Operations
 * ============================================================================ */

/**
 * @brief Initialize a polygon with given exterior capacity
 */
int polygon_init(Polygon *poly, size_t ext_capacity);

/**
 * @brief Free polygon memory
 */
void polygon_free(Polygon *poly);

/**
 * @brief Add a point to the exterior ring
 */
int polygon_add_exterior_point(Polygon *poly, Point p);

/**
 * @brief Add a new hole to the polygon
 */
int polygon_add_hole(Polygon *poly, size_t capacity);

/**
 * @brief Add a point to a specific hole
 */
int polygon_add_hole_point(Polygon *poly, size_t hole_idx, Point p);

/**
 * @brief Calculate polygon centroid (geometric center)
 */
int polygon_centroid(const Polygon *poly, Point *centroid);

/**
 * @brief Calculate polygon MBR
 */
int polygon_mbr(const Polygon *poly, MBR *mbr);

/**
 * @brief Calculate polygon area (signed, positive for CCW)
 */
double polygon_area(const Polygon *poly);

/**
 * @brief Check if polygon exterior ring is clockwise
 */
bool polygon_is_clockwise(const Polygon *poly);

/**
 * @brief Deep copy a polygon
 */
int polygon_copy(Polygon *dest, const Polygon *src);

/* ============================================================================
 * MBR Operations
 * ============================================================================ */

/**
 * @brief Create an MBR from min/max coordinates
 */
MBR mbr_create(double min_x, double min_y, double max_x, double max_y);

/**
 * @brief Create an empty (invalid) MBR
 */
MBR mbr_empty(void);

/**
 * @brief Check if MBR is empty/invalid
 */
bool mbr_is_empty(const MBR *mbr);

/**
 * @brief Expand MBR to include a point
 */
void mbr_expand_point(MBR *mbr, const Point *p);

/**
 * @brief Expand MBR to include another MBR
 */
void mbr_expand_mbr(MBR *mbr, const MBR *other);

/**
 * @brief Check if two MBRs intersect
 */
bool mbr_intersects(const MBR *a, const MBR *b);

/**
 * @brief Check if MBR contains a point
 */
bool mbr_contains_point(const MBR *mbr, const Point *p);

/**
 * @brief Check if MBR a contains MBR b entirely
 */
bool mbr_contains_mbr(const MBR *a, const MBR *b);

/**
 * @brief Calculate MBR centroid
 */
Point mbr_centroid(const MBR *mbr);

/**
 * @brief Calculate MBR area
 */
double mbr_area(const MBR *mbr);

/**
 * @brief Calculate intersection of two MBRs
 */
MBR mbr_intersection(const MBR *a, const MBR *b);

/**
 * @brief Calculate union of two MBRs
 */
MBR mbr_union(const MBR *a, const MBR *b);

/* ============================================================================
 * SpatialObject Operations
 * ============================================================================ */

/**
 * @brief Initialize a spatial object as a point
 */
int spatial_object_init_point(SpatialObject *obj, uint64_t id, Point p);

/**
 * @brief Initialize a spatial object as a linestring
 */
int spatial_object_init_linestring(SpatialObject *obj, uint64_t id, size_t capacity);

/**
 * @brief Initialize a spatial object as a polygon
 */
int spatial_object_init_polygon(SpatialObject *obj, uint64_t id, size_t ext_capacity);

/**
 * @brief Free spatial object resources
 */
void spatial_object_free(SpatialObject *obj);

/**
 * @brief Update centroid and MBR after geometry modification
 */
int spatial_object_update_derived(SpatialObject *obj);

/**
 * @brief Deep copy a spatial object
 */
int spatial_object_copy(SpatialObject *dest, const SpatialObject *src);

/**
 * @brief Set properties data on a spatial object
 */
int spatial_object_set_properties(SpatialObject *obj, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_GEOMETRY_H */

