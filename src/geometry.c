/**
 * @file geometry.c
 * @brief Implementation of geometry primitives and operations
 */

#include "geometry.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define EPSILON 1e-10
#define GROWTH_FACTOR 2

/* ============================================================================
 * Point Operations
 * ============================================================================ */

Point point_create(double x, double y) {
    Point p = { .x = x, .y = y };
    return p;
}

double point_distance_sq(const Point *a, const Point *b) {
    if (!a || !b) return 0.0;
    double dx = b->x - a->x;
    double dy = b->y - a->y;
    return dx * dx + dy * dy;
}

double point_distance(const Point *a, const Point *b) {
    return sqrt(point_distance_sq(a, b));
}

bool point_equals(const Point *a, const Point *b, double epsilon) {
    if (!a || !b) return false;
    return fabs(a->x - b->x) < epsilon && fabs(a->y - b->y) < epsilon;
}

/* ============================================================================
 * LineString Operations
 * ============================================================================ */

int linestring_init(LineString *ls, size_t capacity) {
    if (!ls) return GEOM_ERR_NULL_PTR;
    
    ls->count = 0;
    ls->capacity = capacity > 0 ? capacity : 16;
    ls->points = (Point *)malloc(ls->capacity * sizeof(Point));
    
    if (!ls->points) {
        ls->capacity = 0;
        return GEOM_ERR_ALLOC;
    }
    
    return GEOM_OK;
}

void linestring_free(LineString *ls) {
    if (!ls) return;
    free(ls->points);
    ls->points = NULL;
    ls->count = 0;
    ls->capacity = 0;
}

int linestring_add_point(LineString *ls, Point p) {
    if (!ls) return GEOM_ERR_NULL_PTR;
    
    /* Grow array if needed */
    if (ls->count >= ls->capacity) {
        size_t new_capacity = ls->capacity * GROWTH_FACTOR;
        Point *new_points = (Point *)realloc(ls->points, new_capacity * sizeof(Point));
        if (!new_points) return GEOM_ERR_ALLOC;
        ls->points = new_points;
        ls->capacity = new_capacity;
    }
    
    ls->points[ls->count++] = p;
    return GEOM_OK;
}

int linestring_centroid(const LineString *ls, Point *centroid) {
    if (!ls || !centroid) return GEOM_ERR_NULL_PTR;
    if (ls->count == 0) return GEOM_ERR_EMPTY_GEOM;
    
    /* 
     * For a linestring, centroid is the weighted average of segment midpoints,
     * weighted by segment length.
     */
    if (ls->count == 1) {
        *centroid = ls->points[0];
        return GEOM_OK;
    }
    
    double total_length = 0.0;
    double cx = 0.0, cy = 0.0;
    
    for (size_t i = 0; i < ls->count - 1; i++) {
        const Point *p1 = &ls->points[i];
        const Point *p2 = &ls->points[i + 1];
        
        double seg_length = point_distance(p1, p2);
        double mid_x = (p1->x + p2->x) / 2.0;
        double mid_y = (p1->y + p2->y) / 2.0;
        
        cx += mid_x * seg_length;
        cy += mid_y * seg_length;
        total_length += seg_length;
    }
    
    if (total_length < EPSILON) {
        /* Degenerate case: all points coincident */
        centroid->x = ls->points[0].x;
        centroid->y = ls->points[0].y;
    } else {
        centroid->x = cx / total_length;
        centroid->y = cy / total_length;
    }
    
    return GEOM_OK;
}

int linestring_mbr(const LineString *ls, MBR *mbr) {
    if (!ls || !mbr) return GEOM_ERR_NULL_PTR;
    if (ls->count == 0) return GEOM_ERR_EMPTY_GEOM;
    
    *mbr = mbr_empty();
    for (size_t i = 0; i < ls->count; i++) {
        mbr_expand_point(mbr, &ls->points[i]);
    }
    
    return GEOM_OK;
}

double linestring_length(const LineString *ls) {
    if (!ls || ls->count < 2) return 0.0;
    
    double length = 0.0;
    for (size_t i = 0; i < ls->count - 1; i++) {
        length += point_distance(&ls->points[i], &ls->points[i + 1]);
    }
    return length;
}

int linestring_copy(LineString *dest, const LineString *src) {
    if (!dest || !src) return GEOM_ERR_NULL_PTR;
    
    int err = linestring_init(dest, src->count);
    if (err != GEOM_OK) return err;
    
    memcpy(dest->points, src->points, src->count * sizeof(Point));
    dest->count = src->count;
    
    return GEOM_OK;
}

/* ============================================================================
 * Polygon Operations
 * ============================================================================ */

int polygon_init(Polygon *poly, size_t ext_capacity) {
    if (!poly) return GEOM_ERR_NULL_PTR;
    
    memset(poly, 0, sizeof(Polygon));
    
    poly->ext_capacity = ext_capacity > 0 ? ext_capacity : 16;
    poly->exterior = (Point *)malloc(poly->ext_capacity * sizeof(Point));
    
    if (!poly->exterior) {
        poly->ext_capacity = 0;
        return GEOM_ERR_ALLOC;
    }
    
    return GEOM_OK;
}

void polygon_free(Polygon *poly) {
    if (!poly) return;
    
    free(poly->exterior);
    
    if (poly->holes) {
        for (size_t i = 0; i < poly->num_holes; i++) {
            free(poly->holes[i]);
        }
        free(poly->holes);
        free(poly->hole_counts);
        free(poly->hole_capacities);
    }
    
    memset(poly, 0, sizeof(Polygon));
}

int polygon_add_exterior_point(Polygon *poly, Point p) {
    if (!poly) return GEOM_ERR_NULL_PTR;
    
    if (poly->ext_count >= poly->ext_capacity) {
        size_t new_capacity = poly->ext_capacity * GROWTH_FACTOR;
        Point *new_ext = (Point *)realloc(poly->exterior, new_capacity * sizeof(Point));
        if (!new_ext) return GEOM_ERR_ALLOC;
        poly->exterior = new_ext;
        poly->ext_capacity = new_capacity;
    }
    
    poly->exterior[poly->ext_count++] = p;
    return GEOM_OK;
}

int polygon_add_hole(Polygon *poly, size_t capacity) {
    if (!poly) return GEOM_ERR_NULL_PTR;
    
    /* Grow holes array if needed */
    if (poly->num_holes >= poly->holes_capacity) {
        size_t new_capacity = poly->holes_capacity == 0 ? 4 : poly->holes_capacity * GROWTH_FACTOR;
        
        Point **new_holes = (Point **)realloc(poly->holes, new_capacity * sizeof(Point *));
        size_t *new_counts = (size_t *)realloc(poly->hole_counts, new_capacity * sizeof(size_t));
        size_t *new_caps = (size_t *)realloc(poly->hole_capacities, new_capacity * sizeof(size_t));
        
        if (!new_holes || !new_counts || !new_caps) {
            free(new_holes);
            free(new_counts);
            free(new_caps);
            return GEOM_ERR_ALLOC;
        }
        
        poly->holes = new_holes;
        poly->hole_counts = new_counts;
        poly->hole_capacities = new_caps;
        poly->holes_capacity = new_capacity;
    }
    
    size_t cap = capacity > 0 ? capacity : 16;
    poly->holes[poly->num_holes] = (Point *)malloc(cap * sizeof(Point));
    if (!poly->holes[poly->num_holes]) return GEOM_ERR_ALLOC;
    
    poly->hole_counts[poly->num_holes] = 0;
    poly->hole_capacities[poly->num_holes] = cap;
    poly->num_holes++;
    
    return GEOM_OK;
}

int polygon_add_hole_point(Polygon *poly, size_t hole_idx, Point p) {
    if (!poly) return GEOM_ERR_NULL_PTR;
    if (hole_idx >= poly->num_holes) return GEOM_ERR_INDEX_OUT_OF_BOUNDS;
    
    if (poly->hole_counts[hole_idx] >= poly->hole_capacities[hole_idx]) {
        size_t new_capacity = poly->hole_capacities[hole_idx] * GROWTH_FACTOR;
        Point *new_hole = (Point *)realloc(poly->holes[hole_idx], new_capacity * sizeof(Point));
        if (!new_hole) return GEOM_ERR_ALLOC;
        poly->holes[hole_idx] = new_hole;
        poly->hole_capacities[hole_idx] = new_capacity;
    }
    
    poly->holes[hole_idx][poly->hole_counts[hole_idx]++] = p;
    return GEOM_OK;
}

/**
 * @brief Calculate signed area of a ring (positive for CCW)
 */
static double ring_signed_area(const Point *ring, size_t count) {
    if (count < 3) return 0.0;
    
    double area = 0.0;
    for (size_t i = 0; i < count; i++) {
        size_t j = (i + 1) % count;
        area += ring[i].x * ring[j].y;
        area -= ring[j].x * ring[i].y;
    }
    return area / 2.0;
}

/**
 * @brief Calculate centroid of a ring
 */
static void ring_centroid(const Point *ring, size_t count, double *cx, double *cy) {
    if (count < 3) {
        *cx = *cy = 0.0;
        return;
    }
    
    double area = ring_signed_area(ring, count);
    if (fabs(area) < EPSILON) {
        /* Degenerate polygon: use average of points */
        *cx = *cy = 0.0;
        for (size_t i = 0; i < count; i++) {
            *cx += ring[i].x;
            *cy += ring[i].y;
        }
        *cx /= count;
        *cy /= count;
        return;
    }
    
    *cx = *cy = 0.0;
    for (size_t i = 0; i < count; i++) {
        size_t j = (i + 1) % count;
        double cross = ring[i].x * ring[j].y - ring[j].x * ring[i].y;
        *cx += (ring[i].x + ring[j].x) * cross;
        *cy += (ring[i].y + ring[j].y) * cross;
    }
    
    *cx /= (6.0 * area);
    *cy /= (6.0 * area);
}

int polygon_centroid(const Polygon *poly, Point *centroid) {
    if (!poly || !centroid) return GEOM_ERR_NULL_PTR;
    if (poly->ext_count < 3) return GEOM_ERR_INVALID_GEOM;
    
    double cx, cy;
    ring_centroid(poly->exterior, poly->ext_count, &cx, &cy);
    
    /* 
     * For polygons with holes, we'd need to compute a weighted centroid.
     * For simplicity (and because holes are rare in typical GIS data),
     * we use the exterior ring centroid.
     */
    centroid->x = cx;
    centroid->y = cy;
    
    return GEOM_OK;
}

int polygon_mbr(const Polygon *poly, MBR *mbr) {
    if (!poly || !mbr) return GEOM_ERR_NULL_PTR;
    if (poly->ext_count == 0) return GEOM_ERR_EMPTY_GEOM;
    
    *mbr = mbr_empty();
    for (size_t i = 0; i < poly->ext_count; i++) {
        mbr_expand_point(mbr, &poly->exterior[i]);
    }
    
    return GEOM_OK;
}

double polygon_area(const Polygon *poly) {
    if (!poly || poly->ext_count < 3) return 0.0;
    
    double area = fabs(ring_signed_area(poly->exterior, poly->ext_count));
    
    /* Subtract hole areas */
    for (size_t i = 0; i < poly->num_holes; i++) {
        if (poly->hole_counts[i] >= 3) {
            area -= fabs(ring_signed_area(poly->holes[i], poly->hole_counts[i]));
        }
    }
    
    return area;
}

bool polygon_is_clockwise(const Polygon *poly) {
    if (!poly || poly->ext_count < 3) return false;
    return ring_signed_area(poly->exterior, poly->ext_count) < 0;
}

int polygon_copy(Polygon *dest, const Polygon *src) {
    if (!dest || !src) return GEOM_ERR_NULL_PTR;
    
    int err = polygon_init(dest, src->ext_count);
    if (err != GEOM_OK) return err;
    
    memcpy(dest->exterior, src->exterior, src->ext_count * sizeof(Point));
    dest->ext_count = src->ext_count;
    
    /* Copy holes */
    for (size_t i = 0; i < src->num_holes; i++) {
        err = polygon_add_hole(dest, src->hole_counts[i]);
        if (err != GEOM_OK) {
            polygon_free(dest);
            return err;
        }
        memcpy(dest->holes[i], src->holes[i], src->hole_counts[i] * sizeof(Point));
        dest->hole_counts[i] = src->hole_counts[i];
    }
    
    return GEOM_OK;
}

/* ============================================================================
 * MBR Operations
 * ============================================================================ */

MBR mbr_create(double min_x, double min_y, double max_x, double max_y) {
    MBR mbr = {
        .min_x = min_x,
        .min_y = min_y,
        .max_x = max_x,
        .max_y = max_y
    };
    return mbr;
}

MBR mbr_empty(void) {
    MBR mbr = {
        .min_x = DBL_MAX,
        .min_y = DBL_MAX,
        .max_x = -DBL_MAX,
        .max_y = -DBL_MAX
    };
    return mbr;
}

bool mbr_is_empty(const MBR *mbr) {
    if (!mbr) return true;
    return mbr->min_x > mbr->max_x || mbr->min_y > mbr->max_y;
}

void mbr_expand_point(MBR *mbr, const Point *p) {
    if (!mbr || !p) return;
    
    if (p->x < mbr->min_x) mbr->min_x = p->x;
    if (p->y < mbr->min_y) mbr->min_y = p->y;
    if (p->x > mbr->max_x) mbr->max_x = p->x;
    if (p->y > mbr->max_y) mbr->max_y = p->y;
}

void mbr_expand_mbr(MBR *mbr, const MBR *other) {
    if (!mbr || !other || mbr_is_empty(other)) return;
    
    if (other->min_x < mbr->min_x) mbr->min_x = other->min_x;
    if (other->min_y < mbr->min_y) mbr->min_y = other->min_y;
    if (other->max_x > mbr->max_x) mbr->max_x = other->max_x;
    if (other->max_y > mbr->max_y) mbr->max_y = other->max_y;
}

bool mbr_intersects(const MBR *a, const MBR *b) {
    if (!a || !b) return false;
    if (mbr_is_empty(a) || mbr_is_empty(b)) return false;
    
    return !(a->max_x < b->min_x || a->min_x > b->max_x ||
             a->max_y < b->min_y || a->min_y > b->max_y);
}

bool mbr_contains_point(const MBR *mbr, const Point *p) {
    if (!mbr || !p) return false;
    if (mbr_is_empty(mbr)) return false;
    
    return p->x >= mbr->min_x && p->x <= mbr->max_x &&
           p->y >= mbr->min_y && p->y <= mbr->max_y;
}

bool mbr_contains_mbr(const MBR *a, const MBR *b) {
    if (!a || !b) return false;
    if (mbr_is_empty(a) || mbr_is_empty(b)) return false;
    
    return b->min_x >= a->min_x && b->max_x <= a->max_x &&
           b->min_y >= a->min_y && b->max_y <= a->max_y;
}

Point mbr_centroid(const MBR *mbr) {
    Point p = { .x = 0.0, .y = 0.0 };
    if (!mbr || mbr_is_empty(mbr)) return p;
    
    p.x = (mbr->min_x + mbr->max_x) / 2.0;
    p.y = (mbr->min_y + mbr->max_y) / 2.0;
    return p;
}

double mbr_area(const MBR *mbr) {
    if (!mbr || mbr_is_empty(mbr)) return 0.0;
    return (mbr->max_x - mbr->min_x) * (mbr->max_y - mbr->min_y);
}

MBR mbr_intersection(const MBR *a, const MBR *b) {
    if (!a || !b || !mbr_intersects(a, b)) {
        return mbr_empty();
    }
    
    MBR result = {
        .min_x = fmax(a->min_x, b->min_x),
        .min_y = fmax(a->min_y, b->min_y),
        .max_x = fmin(a->max_x, b->max_x),
        .max_y = fmin(a->max_y, b->max_y)
    };
    return result;
}

MBR mbr_union(const MBR *a, const MBR *b) {
    if (!a || mbr_is_empty(a)) {
        return b ? *b : mbr_empty();
    }
    if (!b || mbr_is_empty(b)) {
        return *a;
    }
    
    MBR result = {
        .min_x = fmin(a->min_x, b->min_x),
        .min_y = fmin(a->min_y, b->min_y),
        .max_x = fmax(a->max_x, b->max_x),
        .max_y = fmax(a->max_y, b->max_y)
    };
    return result;
}

/* ============================================================================
 * SpatialObject Operations
 * ============================================================================ */

int spatial_object_init_point(SpatialObject *obj, uint64_t id, Point p) {
    if (!obj) return GEOM_ERR_NULL_PTR;
    
    memset(obj, 0, sizeof(SpatialObject));
    obj->id = id;
    obj->type = GEOM_POINT;
    obj->geom.point = p;
    obj->centroid = p;
    obj->mbr = mbr_create(p.x, p.y, p.x, p.y);
    
    return GEOM_OK;
}

int spatial_object_init_linestring(SpatialObject *obj, uint64_t id, size_t capacity) {
    if (!obj) return GEOM_ERR_NULL_PTR;
    
    memset(obj, 0, sizeof(SpatialObject));
    obj->id = id;
    obj->type = GEOM_LINESTRING;
    
    int err = linestring_init(&obj->geom.line, capacity);
    if (err != GEOM_OK) return err;
    
    obj->centroid = point_create(0, 0);
    obj->mbr = mbr_empty();
    
    return GEOM_OK;
}

int spatial_object_init_polygon(SpatialObject *obj, uint64_t id, size_t ext_capacity) {
    if (!obj) return GEOM_ERR_NULL_PTR;
    
    memset(obj, 0, sizeof(SpatialObject));
    obj->id = id;
    obj->type = GEOM_POLYGON;
    
    int err = polygon_init(&obj->geom.polygon, ext_capacity);
    if (err != GEOM_OK) return err;
    
    obj->centroid = point_create(0, 0);
    obj->mbr = mbr_empty();
    
    return GEOM_OK;
}

void spatial_object_free(SpatialObject *obj) {
    if (!obj) return;
    
    switch (obj->type) {
        case GEOM_LINESTRING:
            linestring_free(&obj->geom.line);
            break;
        case GEOM_POLYGON:
            polygon_free(&obj->geom.polygon);
            break;
        case GEOM_POINT:
        default:
            break;
    }
    
    free(obj->properties);
    memset(obj, 0, sizeof(SpatialObject));
}

int spatial_object_update_derived(SpatialObject *obj) {
    if (!obj) return GEOM_ERR_NULL_PTR;
    
    int err = GEOM_OK;
    
    switch (obj->type) {
        case GEOM_POINT:
            obj->centroid = obj->geom.point;
            obj->mbr = mbr_create(obj->geom.point.x, obj->geom.point.y,
                                  obj->geom.point.x, obj->geom.point.y);
            break;
            
        case GEOM_LINESTRING:
            err = linestring_centroid(&obj->geom.line, &obj->centroid);
            if (err != GEOM_OK) return err;
            err = linestring_mbr(&obj->geom.line, &obj->mbr);
            break;
            
        case GEOM_POLYGON:
            err = polygon_centroid(&obj->geom.polygon, &obj->centroid);
            if (err != GEOM_OK) return err;
            err = polygon_mbr(&obj->geom.polygon, &obj->mbr);
            break;
    }
    
    return err;
}

int spatial_object_copy(SpatialObject *dest, const SpatialObject *src) {
    if (!dest || !src) return GEOM_ERR_NULL_PTR;
    
    memset(dest, 0, sizeof(SpatialObject));
    dest->id = src->id;
    dest->type = src->type;
    dest->centroid = src->centroid;
    dest->mbr = src->mbr;
    
    int err = GEOM_OK;
    
    switch (src->type) {
        case GEOM_POINT:
            dest->geom.point = src->geom.point;
            break;
            
        case GEOM_LINESTRING:
            err = linestring_copy(&dest->geom.line, &src->geom.line);
            break;
            
        case GEOM_POLYGON:
            err = polygon_copy(&dest->geom.polygon, &src->geom.polygon);
            break;
    }
    
    if (err != GEOM_OK) return err;
    
    if (src->properties && src->properties_size > 0) {
        err = spatial_object_set_properties(dest, src->properties, src->properties_size);
    }
    
    return err;
}

int spatial_object_set_properties(SpatialObject *obj, const void *data, size_t size) {
    if (!obj) return GEOM_ERR_NULL_PTR;
    
    free(obj->properties);
    obj->properties = NULL;
    obj->properties_size = 0;
    
    if (data && size > 0) {
        obj->properties = malloc(size);
        if (!obj->properties) return GEOM_ERR_ALLOC;
        memcpy(obj->properties, data, size);
        obj->properties_size = size;
    }
    
    return GEOM_OK;
}

