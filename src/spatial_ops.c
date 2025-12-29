/**
 * @file spatial_ops.c
 * @brief Implementation of advanced spatial operations
 */

#include "../include/spatial_ops.h"
#include "../include/geometry.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Calculate cross product of vectors OA and OB
 */
static double cross_product(const Point *o, const Point *a, const Point *b) {
    return (a->x - o->x) * (b->y - o->y) - (a->y - o->y) * (b->x - o->x);
}

/**
 * @brief Compare points for sorting (by x, then by y)
 */
static int compare_points(const void *a, const void *b) {
    const Point *p1 = (const Point *)a;
    const Point *p2 = (const Point *)b;
    
    if (p1->x != p2->x) {
        return (p1->x < p2->x) ? -1 : 1;
    }
    return (p1->y < p2->y) ? -1 : (p1->y > p2->y) ? 1 : 0;
}

/**
 * @brief Perpendicular distance from point to line segment
 */
static double perpendicular_distance(const Point *p, const Point *line_start, const Point *line_end) {
    double dx = line_end->x - line_start->x;
    double dy = line_end->y - line_start->y;
    double len_sq = dx * dx + dy * dy;
    
    if (len_sq < 1e-10) {
        return point_distance(p, line_start);
    }
    
    double t = ((p->x - line_start->x) * dx + (p->y - line_start->y) * dy) / len_sq;
    t = fmax(0, fmin(1, t));
    
    Point proj = {
        line_start->x + t * dx,
        line_start->y + t * dy
    };
    
    return point_distance(p, &proj);
}

/* ============================================================================
 * Buffer Operations
 * ============================================================================ */

Polygon* buffer_point(const Point *center, double distance, int segments) {
    if (!center || distance <= 0 || segments < 3) {
        return NULL;
    }
    
    Polygon *poly = malloc(sizeof(Polygon));
    if (!poly) return NULL;
    
    if (polygon_init(poly, segments + 1) != GEOM_OK) {
        free(poly);
        return NULL;
    }
    
    // Create circular buffer
    for (int i = 0; i < segments; i++) {
        double angle = 2.0 * M_PI * i / segments;
        Point p = {
            center->x + distance * cos(angle),
            center->y + distance * sin(angle)
        };
        polygon_add_exterior_point(poly, p);
    }
    
    // Close the polygon
    Point first = {
        center->x + distance * cos(0),
        center->y + distance * sin(0)
    };
    polygon_add_exterior_point(poly, first);
    
    return poly;
}

Polygon* buffer_linestring(const LineString *line, double distance, int segments) {
    if (!line || line->count < 2 || distance <= 0) {
        return NULL;
    }
    
    // Calculate buffer size - need points on both sides plus caps
    size_t cap_points = segments / 2;
    size_t buffer_size = line->count * 2 + cap_points * 2 + 2;
    
    Polygon *poly = malloc(sizeof(Polygon));
    if (!poly) return NULL;
    
    if (polygon_init(poly, buffer_size) != GEOM_OK) {
        free(poly);
        return NULL;
    }
    
    // Build left side offset
    for (size_t i = 0; i < line->count; i++) {
        Point p = line->points[i];
        double dx = 0, dy = 0;
        
        if (i < line->count - 1) {
            dx = line->points[i + 1].x - p.x;
            dy = line->points[i + 1].y - p.y;
        } else {
            dx = p.x - line->points[i - 1].x;
            dy = p.y - line->points[i - 1].y;
        }
        
        double len = sqrt(dx * dx + dy * dy);
        if (len > 1e-10) {
            dx /= len;
            dy /= len;
        }
        
        // Perpendicular offset (left side)
        Point offset = {
            p.x - dy * distance,
            p.y + dx * distance
        };
        polygon_add_exterior_point(poly, offset);
    }
    
    // Add end cap (semicircle)
    Point end = line->points[line->count - 1];
    Point prev = line->points[line->count - 2];
    double end_angle = atan2(end.y - prev.y, end.x - prev.x);
    
    for (size_t i = 0; i <= cap_points; i++) {
        double angle = end_angle - M_PI / 2 + M_PI * i / cap_points;
        Point cap = {
            end.x + distance * cos(angle),
            end.y + distance * sin(angle)
        };
        polygon_add_exterior_point(poly, cap);
    }
    
    // Build right side offset (reverse direction)
    for (size_t i = line->count; i > 0; i--) {
        Point p = line->points[i - 1];
        double dx = 0, dy = 0;
        
        if (i > 1) {
            dx = p.x - line->points[i - 2].x;
            dy = p.y - line->points[i - 2].y;
        } else if (line->count > 1) {
            dx = line->points[1].x - p.x;
            dy = line->points[1].y - p.y;
        }
        
        double len = sqrt(dx * dx + dy * dy);
        if (len > 1e-10) {
            dx /= len;
            dy /= len;
        }
        
        // Perpendicular offset (right side when going backwards)
        Point offset = {
            p.x - dy * distance,
            p.y + dx * distance
        };
        polygon_add_exterior_point(poly, offset);
    }
    
    // Add start cap
    Point start = line->points[0];
    Point next = line->points[1];
    double start_angle = atan2(next.y - start.y, next.x - start.x);
    
    for (size_t i = 0; i <= cap_points; i++) {
        double angle = start_angle + M_PI / 2 + M_PI * i / cap_points;
        Point cap = {
            start.x + distance * cos(angle),
            start.y + distance * sin(angle)
        };
        polygon_add_exterior_point(poly, cap);
    }
    
    // Close polygon
    if (poly->ext_count > 0) {
        polygon_add_exterior_point(poly, poly->exterior[0]);
    }
    
    return poly;
}

Polygon* buffer_polygon(const Polygon *poly, double distance, int segments) {
    if (!poly || poly->ext_count < 3) {
        return NULL;
    }
    
    Polygon *result = malloc(sizeof(Polygon));
    if (!result) return NULL;
    
    // For positive distance, expand; for negative, shrink
    size_t new_capacity = poly->ext_count * 2;
    if (polygon_init(result, new_capacity) != GEOM_OK) {
        free(result);
        return NULL;
    }
    
    // Offset each edge
    for (size_t i = 0; i < poly->ext_count - 1; i++) {
        Point p1 = poly->exterior[i];
        Point p2 = poly->exterior[(i + 1) % (poly->ext_count - 1)];
        
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        double len = sqrt(dx * dx + dy * dy);
        
        if (len < 1e-10) continue;
        
        dx /= len;
        dy /= len;
        
        // Normal vector (perpendicular, pointing outward for CCW polygon)
        double nx = -dy;
        double ny = dx;
        
        // Offset points
        Point offset1 = {
            p1.x + nx * distance,
            p1.y + ny * distance
        };
        
        polygon_add_exterior_point(result, offset1);
        
        // Add rounded corner if needed
        if (distance > 0 && segments > 0) {
            Point next_p1 = poly->exterior[(i + 1) % (poly->ext_count - 1)];
            Point next_p2 = poly->exterior[(i + 2) % (poly->ext_count - 1)];
            
            double next_dx = next_p2.x - next_p1.x;
            double next_dy = next_p2.y - next_p1.y;
            double next_len = sqrt(next_dx * next_dx + next_dy * next_dy);
            
            if (next_len > 1e-10) {
                next_dx /= next_len;
                next_dy /= next_len;
                
                double next_nx = -next_dy;
                double next_ny = next_dx;
                
                // Angle between edges
                double angle1 = atan2(ny, nx);
                double angle2 = atan2(next_ny, next_nx);
                double angle_diff = angle2 - angle1;
                
                // Normalize angle
                while (angle_diff > M_PI) angle_diff -= 2 * M_PI;
                while (angle_diff < -M_PI) angle_diff += 2 * M_PI;
                
                // Add arc for convex corners
                if (angle_diff < 0) {
                    int arc_segments = (int)(fabs(angle_diff) / (2 * M_PI) * segments) + 1;
                    for (int j = 0; j <= arc_segments; j++) {
                        double angle = angle1 + angle_diff * j / arc_segments;
                        Point arc = {
                            p2.x + distance * cos(angle),
                            p2.y + distance * sin(angle)
                        };
                        polygon_add_exterior_point(result, arc);
                    }
                }
            }
        }
    }
    
    // Close the polygon
    if (result->ext_count > 0) {
        polygon_add_exterior_point(result, result->exterior[0]);
    }
    
    return result;
}

Polygon* spatial_buffer(const SpatialObject *obj, double distance, int segments) {
    if (!obj) return NULL;
    
    if (segments <= 0) {
        segments = BUFFER_DEFAULT_SEGMENTS;
    }
    
    switch (obj->type) {
        case GEOM_POINT:
            return buffer_point(&obj->geom.point, distance, segments);
        case GEOM_LINESTRING:
            return buffer_linestring(&obj->geom.line, distance, segments);
        case GEOM_POLYGON:
            return buffer_polygon(&obj->geom.polygon, distance, segments);
        default:
            return NULL;
    }
}

/* ============================================================================
 * Spatial Predicates
 * ============================================================================ */

bool point_in_polygon(const Point *p, const Polygon *poly) {
    if (!p || !poly || poly->ext_count < 3) {
        return false;
    }
    
    // Ray casting algorithm
    bool inside = false;
    size_t n = poly->ext_count;
    
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Point pi = poly->exterior[i];
        Point pj = poly->exterior[j];
        
        if (((pi.y > p->y) != (pj.y > p->y)) &&
            (p->x < (pj.x - pi.x) * (p->y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    
    // Check holes
    for (size_t h = 0; h < poly->num_holes; h++) {
        bool in_hole = false;
        size_t hn = poly->hole_counts[h];
        
        for (size_t i = 0, j = hn - 1; i < hn; j = i++) {
            Point pi = poly->holes[h][i];
            Point pj = poly->holes[h][j];
            
            if (((pi.y > p->y) != (pj.y > p->y)) &&
                (p->x < (pj.x - pi.x) * (p->y - pi.y) / (pj.y - pi.y) + pi.x)) {
                in_hole = !in_hole;
            }
        }
        
        if (in_hole) {
            inside = false;
            break;
        }
    }
    
    return inside;
}

bool segments_intersect(const Point *a1, const Point *a2, const Point *b1, const Point *b2) {
    double d1 = cross_product(b1, b2, a1);
    double d2 = cross_product(b1, b2, a2);
    double d3 = cross_product(a1, a2, b1);
    double d4 = cross_product(a1, a2, b2);
    
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }
    
    // Check for collinear cases
    double epsilon = 1e-10;
    if (fabs(d1) < epsilon && fabs(d2) < epsilon) {
        // Collinear - check overlap
        double min_ax = fmin(a1->x, a2->x), max_ax = fmax(a1->x, a2->x);
        double min_ay = fmin(a1->y, a2->y), max_ay = fmax(a1->y, a2->y);
        double min_bx = fmin(b1->x, b2->x), max_bx = fmax(b1->x, b2->x);
        double min_by = fmin(b1->y, b2->y), max_by = fmax(b1->y, b2->y);
        
        return !(max_ax < min_bx || max_bx < min_ax || max_ay < min_by || max_by < min_ay);
    }
    
    return false;
}

bool segment_intersection_point(const Point *a1, const Point *a2,
                                 const Point *b1, const Point *b2,
                                 Point *intersection) {
    double d1 = cross_product(b1, b2, a1);
    double d2 = cross_product(b1, b2, a2);
    
    if (fabs(d1 - d2) < 1e-10) {
        return false;  // Parallel
    }
    
    double t = d1 / (d1 - d2);
    
    if (t < 0 || t > 1) {
        return false;
    }
    
    double d3 = cross_product(a1, a2, b1);
    double d4 = cross_product(a1, a2, b2);
    double u = d3 / (d3 - d4);
    
    if (u < 0 || u > 1) {
        return false;
    }
    
    if (intersection) {
        intersection->x = a1->x + t * (a2->x - a1->x);
        intersection->y = a1->y + t * (a2->y - a1->y);
    }
    
    return true;
}

bool spatial_intersects(const SpatialObject *a, const SpatialObject *b) {
    if (!a || !b) return false;
    
    // Quick MBR check first
    if (!mbr_intersects(&a->mbr, &b->mbr)) {
        return false;
    }
    
    // Point-Point
    if (a->type == GEOM_POINT && b->type == GEOM_POINT) {
        return point_distance(&a->geom.point, &b->geom.point) < 1e-10;
    }
    
    // Point-Polygon
    if (a->type == GEOM_POINT && b->type == GEOM_POLYGON) {
        return point_in_polygon(&a->geom.point, &b->geom.polygon);
    }
    if (a->type == GEOM_POLYGON && b->type == GEOM_POINT) {
        return point_in_polygon(&b->geom.point, &a->geom.polygon);
    }
    
    // LineString-LineString
    if (a->type == GEOM_LINESTRING && b->type == GEOM_LINESTRING) {
        const LineString *la = &a->geom.line;
        const LineString *lb = &b->geom.line;
        
        for (size_t i = 0; i < la->count - 1; i++) {
            for (size_t j = 0; j < lb->count - 1; j++) {
                if (segments_intersect(&la->points[i], &la->points[i + 1],
                                       &lb->points[j], &lb->points[j + 1])) {
                    return true;
                }
            }
        }
        return false;
    }
    
    // Polygon-Polygon
    if (a->type == GEOM_POLYGON && b->type == GEOM_POLYGON) {
        const Polygon *pa = &a->geom.polygon;
        const Polygon *pb = &b->geom.polygon;
        
        // Check if any vertex of a is in b
        for (size_t i = 0; i < pa->ext_count; i++) {
            if (point_in_polygon(&pa->exterior[i], pb)) {
                return true;
            }
        }
        
        // Check if any vertex of b is in a
        for (size_t i = 0; i < pb->ext_count; i++) {
            if (point_in_polygon(&pb->exterior[i], pa)) {
                return true;
            }
        }
        
        // Check if any edges intersect
        for (size_t i = 0; i < pa->ext_count - 1; i++) {
            for (size_t j = 0; j < pb->ext_count - 1; j++) {
                if (segments_intersect(&pa->exterior[i], &pa->exterior[i + 1],
                                       &pb->exterior[j], &pb->exterior[j + 1])) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    // LineString-Polygon
    if ((a->type == GEOM_LINESTRING && b->type == GEOM_POLYGON) ||
        (a->type == GEOM_POLYGON && b->type == GEOM_LINESTRING)) {
        const LineString *line = (a->type == GEOM_LINESTRING) ? &a->geom.line : &b->geom.line;
        const Polygon *poly = (a->type == GEOM_POLYGON) ? &a->geom.polygon : &b->geom.polygon;
        
        // Check if any line point is in polygon
        for (size_t i = 0; i < line->count; i++) {
            if (point_in_polygon(&line->points[i], poly)) {
                return true;
            }
        }
        
        // Check if any line segment intersects polygon edges
        for (size_t i = 0; i < line->count - 1; i++) {
            for (size_t j = 0; j < poly->ext_count - 1; j++) {
                if (segments_intersect(&line->points[i], &line->points[i + 1],
                                       &poly->exterior[j], &poly->exterior[j + 1])) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    // Point-LineString: check distance
    if ((a->type == GEOM_POINT && b->type == GEOM_LINESTRING) ||
        (a->type == GEOM_LINESTRING && b->type == GEOM_POINT)) {
        const Point *pt = (a->type == GEOM_POINT) ? &a->geom.point : &b->geom.point;
        const LineString *line = (a->type == GEOM_LINESTRING) ? &a->geom.line : &b->geom.line;
        
        for (size_t i = 0; i < line->count - 1; i++) {
            if (perpendicular_distance(pt, &line->points[i], &line->points[i + 1]) < 1e-10) {
                return true;
            }
        }
        return false;
    }
    
    return false;
}

bool spatial_contains(const SpatialObject *a, const SpatialObject *b) {
    if (!a || !b) return false;
    
    // Only polygons can contain other geometries
    if (a->type != GEOM_POLYGON) {
        return false;
    }
    
    // MBR check
    if (!mbr_contains_mbr(&a->mbr, &b->mbr)) {
        return false;
    }
    
    const Polygon *poly = &a->geom.polygon;
    
    switch (b->type) {
        case GEOM_POINT:
            return point_in_polygon(&b->geom.point, poly);
            
        case GEOM_LINESTRING: {
            const LineString *line = &b->geom.line;
            for (size_t i = 0; i < line->count; i++) {
                if (!point_in_polygon(&line->points[i], poly)) {
                    return false;
                }
            }
            return true;
        }
        
        case GEOM_POLYGON: {
            const Polygon *other = &b->geom.polygon;
            for (size_t i = 0; i < other->ext_count; i++) {
                if (!point_in_polygon(&other->exterior[i], poly)) {
                    return false;
                }
            }
            return true;
        }
        
        default:
            return false;
    }
}

bool spatial_within(const SpatialObject *a, const SpatialObject *b) {
    return spatial_contains(b, a);
}

double spatial_distance(const SpatialObject *a, const SpatialObject *b) {
    if (!a || !b) return DBL_MAX;
    
    // Point-Point
    if (a->type == GEOM_POINT && b->type == GEOM_POINT) {
        return point_distance(&a->geom.point, &b->geom.point);
    }
    
    // Use centroid distance as approximation for complex geometries
    // TODO: Implement exact distance calculations
    return point_distance(&a->centroid, &b->centroid);
}

bool spatial_within_distance(const SpatialObject *a, const SpatialObject *b, double distance) {
    if (!a || !b) return false;
    
    // Quick MBR check with buffer
    MBR a_expanded = {
        a->mbr.min_x - distance,
        a->mbr.min_y - distance,
        a->mbr.max_x + distance,
        a->mbr.max_y + distance
    };
    
    if (!mbr_intersects(&a_expanded, &b->mbr)) {
        return false;
    }
    
    return spatial_distance(a, b) <= distance;
}

/* ============================================================================
 * Convex Hull
 * ============================================================================ */

Point* convex_hull(const Point *points, size_t count, size_t *hull_count) {
    if (!points || count < 3 || !hull_count) {
        if (hull_count) *hull_count = 0;
        return NULL;
    }
    
    // Copy and sort points
    Point *sorted = malloc(count * sizeof(Point));
    if (!sorted) return NULL;
    
    memcpy(sorted, points, count * sizeof(Point));
    qsort(sorted, count, sizeof(Point), compare_points);
    
    // Build hull
    Point *hull = malloc(2 * count * sizeof(Point));
    if (!hull) {
        free(sorted);
        return NULL;
    }
    
    size_t k = 0;
    
    // Build lower hull
    for (size_t i = 0; i < count; i++) {
        while (k >= 2 && cross_product(&hull[k - 2], &hull[k - 1], &sorted[i]) <= 0) {
            k--;
        }
        hull[k++] = sorted[i];
    }
    
    // Build upper hull
    size_t lower_size = k;
    for (size_t i = count - 1; i > 0; i--) {
        while (k > lower_size && cross_product(&hull[k - 2], &hull[k - 1], &sorted[i - 1]) <= 0) {
            k--;
        }
        hull[k++] = sorted[i - 1];
    }
    
    free(sorted);
    
    // Resize to actual size
    Point *result = realloc(hull, k * sizeof(Point));
    if (!result) {
        *hull_count = k;
        return hull;
    }
    
    *hull_count = k;
    return result;
}

Polygon* spatial_convex_hull(const SpatialObject *obj) {
    if (!obj) return NULL;
    
    Point *points = NULL;
    size_t count = 0;
    
    switch (obj->type) {
        case GEOM_POINT:
            // Single point - return degenerate polygon
            points = malloc(sizeof(Point));
            if (points) {
                points[0] = obj->geom.point;
                count = 1;
            }
            break;
            
        case GEOM_LINESTRING:
            count = obj->geom.line.count;
            points = malloc(count * sizeof(Point));
            if (points) {
                memcpy(points, obj->geom.line.points, count * sizeof(Point));
            }
            break;
            
        case GEOM_POLYGON:
            count = obj->geom.polygon.ext_count;
            points = malloc(count * sizeof(Point));
            if (points) {
                memcpy(points, obj->geom.polygon.exterior, count * sizeof(Point));
            }
            break;
            
        default:
            return NULL;
    }
    
    if (!points || count < 3) {
        free(points);
        return NULL;
    }
    
    size_t hull_count = 0;
    Point *hull_points = convex_hull(points, count, &hull_count);
    free(points);
    
    if (!hull_points || hull_count < 3) {
        free(hull_points);
        return NULL;
    }
    
    Polygon *hull = malloc(sizeof(Polygon));
    if (!hull) {
        free(hull_points);
        return NULL;
    }
    
    if (polygon_init(hull, hull_count) != GEOM_OK) {
        free(hull_points);
        free(hull);
        return NULL;
    }
    
    for (size_t i = 0; i < hull_count; i++) {
        polygon_add_exterior_point(hull, hull_points[i]);
    }
    
    free(hull_points);
    return hull;
}

/* ============================================================================
 * Spatial Join Support
 * ============================================================================ */

SpatialJoinResult* join_result_create(size_t initial_capacity) {
    SpatialJoinResult *result = malloc(sizeof(SpatialJoinResult));
    if (!result) return NULL;
    
    result->pairs = malloc(initial_capacity * sizeof(JoinPair));
    if (!result->pairs) {
        free(result);
        return NULL;
    }
    
    result->count = 0;
    result->capacity = initial_capacity;
    return result;
}

int join_result_add(SpatialJoinResult *result, uint64_t id_a, uint64_t id_b, double distance) {
    if (!result) return -1;
    
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * 2;
        JoinPair *new_pairs = realloc(result->pairs, new_cap * sizeof(JoinPair));
        if (!new_pairs) return -1;
        
        result->pairs = new_pairs;
        result->capacity = new_cap;
    }
    
    result->pairs[result->count].id_a = id_a;
    result->pairs[result->count].id_b = id_b;
    result->pairs[result->count].distance = distance;
    result->count++;
    
    return 0;
}

void join_result_free(SpatialJoinResult *result) {
    if (result) {
        free(result->pairs);
        free(result);
    }
}

/* ============================================================================
 * Grid Aggregation
 * ============================================================================ */

GridAggregation* grid_aggregation_create(const MBR *bounds, double cell_size) {
    if (!bounds || cell_size <= 0) return NULL;
    
    GridAggregation *grid = malloc(sizeof(GridAggregation));
    if (!grid) return NULL;
    
    double width = bounds->max_x - bounds->min_x;
    double height = bounds->max_y - bounds->min_y;
    
    grid->cols = (size_t)ceil(width / cell_size);
    grid->rows = (size_t)ceil(height / cell_size);
    grid->bounds = *bounds;
    grid->cell_width = cell_size;
    grid->cell_height = cell_size;
    
    if (grid->cols == 0) grid->cols = 1;
    if (grid->rows == 0) grid->rows = 1;
    
    size_t total_cells = grid->rows * grid->cols;
    grid->cells = calloc(total_cells, sizeof(GridCell));
    if (!grid->cells) {
        free(grid);
        return NULL;
    }
    
    // Initialize cell bounds
    for (size_t r = 0; r < grid->rows; r++) {
        for (size_t c = 0; c < grid->cols; c++) {
            GridCell *cell = &grid->cells[r * grid->cols + c];
            cell->bounds.min_x = bounds->min_x + c * cell_size;
            cell->bounds.min_y = bounds->min_y + r * cell_size;
            cell->bounds.max_x = cell->bounds.min_x + cell_size;
            cell->bounds.max_y = cell->bounds.min_y + cell_size;
            cell->value = 0;
            cell->count = 0;
            cell->sum = 0;
            cell->sum_sq = 0;
        }
    }
    
    return grid;
}

void grid_aggregation_add(GridAggregation *grid, const SpatialObject *obj,
                          double value, AggregationType agg_type) {
    if (!grid || !obj) return;
    
    // Find cell containing centroid
    double x = obj->centroid.x;
    double y = obj->centroid.y;
    
    if (x < grid->bounds.min_x || x > grid->bounds.max_x ||
        y < grid->bounds.min_y || y > grid->bounds.max_y) {
        return;
    }
    
    size_t col = (size_t)((x - grid->bounds.min_x) / grid->cell_width);
    size_t row = (size_t)((y - grid->bounds.min_y) / grid->cell_height);
    
    if (col >= grid->cols) col = grid->cols - 1;
    if (row >= grid->rows) row = grid->rows - 1;
    
    GridCell *cell = &grid->cells[row * grid->cols + col];
    cell->count++;
    cell->sum += value;
    cell->sum_sq += value * value;
    
    switch (agg_type) {
        case AGG_COUNT:
            cell->value = (double)cell->count;
            break;
        case AGG_SUM:
            cell->value = cell->sum;
            break;
        case AGG_MIN:
            if (cell->count == 1 || value < cell->value) {
                cell->value = value;
            }
            break;
        case AGG_MAX:
            if (cell->count == 1 || value > cell->value) {
                cell->value = value;
            }
            break;
        default:
            break;
    }
}

void grid_aggregation_finalize(GridAggregation *grid, AggregationType agg_type) {
    if (!grid) return;
    
    for (size_t i = 0; i < grid->rows * grid->cols; i++) {
        GridCell *cell = &grid->cells[i];
        
        if (cell->count == 0) continue;
        
        switch (agg_type) {
            case AGG_AVG:
                cell->value = cell->sum / cell->count;
                break;
            case AGG_STDDEV: {
                double mean = cell->sum / cell->count;
                double variance = (cell->sum_sq / cell->count) - (mean * mean);
                cell->value = sqrt(fmax(0, variance));
                break;
            }
            default:
                break;
        }
    }
}

GridCell* grid_aggregation_get_cell(GridAggregation *grid, size_t row, size_t col) {
    if (!grid || row >= grid->rows || col >= grid->cols) {
        return NULL;
    }
    return &grid->cells[row * grid->cols + col];
}

GridCell* grid_aggregation_get_cell_at(GridAggregation *grid, double x, double y) {
    if (!grid) return NULL;
    
    if (x < grid->bounds.min_x || x > grid->bounds.max_x ||
        y < grid->bounds.min_y || y > grid->bounds.max_y) {
        return NULL;
    }
    
    size_t col = (size_t)((x - grid->bounds.min_x) / grid->cell_width);
    size_t row = (size_t)((y - grid->bounds.min_y) / grid->cell_height);
    
    if (col >= grid->cols) col = grid->cols - 1;
    if (row >= grid->rows) row = grid->rows - 1;
    
    return &grid->cells[row * grid->cols + col];
}

void grid_aggregation_free(GridAggregation *grid) {
    if (grid) {
        free(grid->cells);
        free(grid);
    }
}

void region_aggregation_free(RegionAggregation *agg) {
    if (agg) {
        free(agg->regions);
        free(agg);
    }
}

/* ============================================================================
 * Simplification
 * ============================================================================ */

/**
 * @brief Douglas-Peucker simplification helper
 */
static void douglas_peucker(const Point *points, size_t start, size_t end,
                            double tolerance, bool *keep) {
    if (end <= start + 1) return;
    
    double max_dist = 0;
    size_t max_idx = start;
    
    for (size_t i = start + 1; i < end; i++) {
        double dist = perpendicular_distance(&points[i], &points[start], &points[end]);
        if (dist > max_dist) {
            max_dist = dist;
            max_idx = i;
        }
    }
    
    if (max_dist > tolerance) {
        keep[max_idx] = true;
        douglas_peucker(points, start, max_idx, tolerance, keep);
        douglas_peucker(points, max_idx, end, tolerance, keep);
    }
}

LineString* simplify_linestring(const LineString *line, double tolerance) {
    if (!line || line->count < 3) {
        return NULL;
    }
    
    bool *keep = calloc(line->count, sizeof(bool));
    if (!keep) return NULL;
    
    keep[0] = true;
    keep[line->count - 1] = true;
    
    douglas_peucker(line->points, 0, line->count - 1, tolerance, keep);
    
    // Count kept points
    size_t count = 0;
    for (size_t i = 0; i < line->count; i++) {
        if (keep[i]) count++;
    }
    
    LineString *result = malloc(sizeof(LineString));
    if (!result) {
        free(keep);
        return NULL;
    }
    
    if (linestring_init(result, count) != GEOM_OK) {
        free(keep);
        free(result);
        return NULL;
    }
    
    for (size_t i = 0; i < line->count; i++) {
        if (keep[i]) {
            linestring_add_point(result, line->points[i]);
        }
    }
    
    free(keep);
    return result;
}

Polygon* simplify_polygon(const Polygon *poly, double tolerance) {
    if (!poly || poly->ext_count < 4) {
        return NULL;
    }
    
    // Simplify exterior ring
    LineString ext_line = {
        .points = poly->exterior,
        .count = poly->ext_count,
        .capacity = poly->ext_count
    };
    
    LineString *simplified_ext = simplify_linestring(&ext_line, tolerance);
    if (!simplified_ext || simplified_ext->count < 4) {
        if (simplified_ext) {
            linestring_free(simplified_ext);
            free(simplified_ext);
        }
        return NULL;
    }
    
    Polygon *result = malloc(sizeof(Polygon));
    if (!result) {
        linestring_free(simplified_ext);
        free(simplified_ext);
        return NULL;
    }
    
    if (polygon_init(result, simplified_ext->count) != GEOM_OK) {
        linestring_free(simplified_ext);
        free(simplified_ext);
        free(result);
        return NULL;
    }
    
    for (size_t i = 0; i < simplified_ext->count; i++) {
        polygon_add_exterior_point(result, simplified_ext->points[i]);
    }
    
    linestring_free(simplified_ext);
    free(simplified_ext);
    
    return result;
}

LineString* densify_linestring(const LineString *line, double max_distance) {
    if (!line || line->count < 2 || max_distance <= 0) {
        return NULL;
    }
    
    // Calculate total length and estimate points needed
    double total_length = linestring_length(line);
    size_t estimated_points = (size_t)(total_length / max_distance) + line->count;
    
    LineString *result = malloc(sizeof(LineString));
    if (!result) return NULL;
    
    if (linestring_init(result, estimated_points) != GEOM_OK) {
        free(result);
        return NULL;
    }
    
    for (size_t i = 0; i < line->count - 1; i++) {
        Point p1 = line->points[i];
        Point p2 = line->points[i + 1];
        
        linestring_add_point(result, p1);
        
        double dist = point_distance(&p1, &p2);
        if (dist > max_distance) {
            int segments = (int)ceil(dist / max_distance);
            for (int j = 1; j < segments; j++) {
                double t = (double)j / segments;
                Point interp = {
                    p1.x + t * (p2.x - p1.x),
                    p1.y + t * (p2.y - p1.y)
                };
                linestring_add_point(result, interp);
            }
        }
    }
    
    // Add last point
    linestring_add_point(result, line->points[line->count - 1]);
    
    return result;
}

