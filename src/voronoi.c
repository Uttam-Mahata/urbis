/**
 * @file voronoi.c
 * @brief Voronoi diagram and Delaunay triangulation implementation
 * 
 * Uses Bowyer-Watson algorithm for Delaunay triangulation,
 * and derives Voronoi diagram from the dual graph.
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
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Edge for triangulation
 */
typedef struct {
    size_t v1, v2;
} Edge;

/**
 * @brief Triangle for incremental construction
 */
typedef struct {
    size_t v1, v2, v3;
    Point circumcenter;
    double circumradius_sq;
    bool is_bad;
} TriangleWork;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Calculate circumcircle of a triangle
 */
static void calculate_circumcircle(const Point *p1, const Point *p2, const Point *p3,
                                    Point *center, double *radius_sq) {
    double ax = p2->x - p1->x;
    double ay = p2->y - p1->y;
    double bx = p3->x - p1->x;
    double by = p3->y - p1->y;
    
    double d = 2.0 * (ax * by - ay * bx);
    
    if (fabs(d) < 1e-10) {
        // Degenerate case - collinear points
        center->x = (p1->x + p2->x + p3->x) / 3.0;
        center->y = (p1->y + p2->y + p3->y) / 3.0;
        *radius_sq = DBL_MAX;
        return;
    }
    
    double ax_sq = ax * ax;
    double ay_sq = ay * ay;
    double bx_sq = bx * bx;
    double by_sq = by * by;
    
    double ux = (by * (ax_sq + ay_sq) - ay * (bx_sq + by_sq)) / d;
    double uy = (ax * (bx_sq + by_sq) - bx * (ax_sq + ay_sq)) / d;
    
    center->x = p1->x + ux;
    center->y = p1->y + uy;
    *radius_sq = ux * ux + uy * uy;
}

/**
 * @brief Check if point is inside circumcircle
 */
static bool point_in_circumcircle(const Point *p, const Point *center, double radius_sq) {
    double dx = p->x - center->x;
    double dy = p->y - center->y;
    return (dx * dx + dy * dy) < radius_sq;
}

/**
 * @brief Check if two edges are equal (order independent)
 */
static bool edges_equal(const Edge *e1, const Edge *e2) {
    return (e1->v1 == e2->v1 && e1->v2 == e2->v2) ||
           (e1->v1 == e2->v2 && e1->v2 == e2->v1);
}

/**
 * @brief Check if triangle shares vertex with super triangle
 */
static bool shares_vertex_with_super(const TriangleWork *tri, size_t super_start) {
    return tri->v1 >= super_start || tri->v2 >= super_start || tri->v3 >= super_start;
}

/* ============================================================================
 * Delaunay Triangulation (Bowyer-Watson Algorithm)
 * ============================================================================ */

DelaunayTriangulation* delaunay_create(const Point *points, size_t count) {
    if (!points || count < 3) {
        return NULL;
    }
    
    // Allocate result
    DelaunayTriangulation *result = malloc(sizeof(DelaunayTriangulation));
    if (!result) return NULL;
    
    // Copy points
    result->points = malloc(count * sizeof(Point));
    if (!result->points) {
        free(result);
        return NULL;
    }
    memcpy(result->points, points, count * sizeof(Point));
    result->num_points = count;
    
    // Find bounding box
    double min_x = DBL_MAX, min_y = DBL_MAX;
    double max_x = -DBL_MAX, max_y = -DBL_MAX;
    
    for (size_t i = 0; i < count; i++) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    
    double dx = max_x - min_x;
    double dy = max_y - min_y;
    double delta_max = fmax(dx, dy) * 2.0;
    double mid_x = (min_x + max_x) / 2.0;
    double mid_y = (min_y + max_y) / 2.0;
    
    // Create super triangle (large enough to contain all points)
    Point super[3] = {
        { mid_x - 2.0 * delta_max, mid_y - delta_max },
        { mid_x, mid_y + 2.0 * delta_max },
        { mid_x + 2.0 * delta_max, mid_y - delta_max }
    };
    
    // Working points array (original + super triangle)
    size_t work_count = count + 3;
    Point *work_points = malloc(work_count * sizeof(Point));
    if (!work_points) {
        free(result->points);
        free(result);
        return NULL;
    }
    memcpy(work_points, points, count * sizeof(Point));
    work_points[count] = super[0];
    work_points[count + 1] = super[1];
    work_points[count + 2] = super[2];
    
    // Initialize with super triangle
    size_t tri_capacity = count * 4;
    TriangleWork *triangles = malloc(tri_capacity * sizeof(TriangleWork));
    if (!triangles) {
        free(work_points);
        free(result->points);
        free(result);
        return NULL;
    }
    
    triangles[0].v1 = count;
    triangles[0].v2 = count + 1;
    triangles[0].v3 = count + 2;
    triangles[0].is_bad = false;
    calculate_circumcircle(&super[0], &super[1], &super[2],
                           &triangles[0].circumcenter, &triangles[0].circumradius_sq);
    size_t tri_count = 1;
    
    // Edge buffer for polygon hole
    size_t edge_capacity = 256;
    Edge *polygon_edges = malloc(edge_capacity * sizeof(Edge));
    if (!polygon_edges) {
        free(triangles);
        free(work_points);
        free(result->points);
        free(result);
        return NULL;
    }
    
    // Insert points one by one
    for (size_t i = 0; i < count; i++) {
        const Point *p = &points[i];
        size_t edge_count = 0;
        
        // Find triangles whose circumcircle contains the point
        for (size_t j = 0; j < tri_count; j++) {
            if (point_in_circumcircle(p, &triangles[j].circumcenter, 
                                       triangles[j].circumradius_sq)) {
                triangles[j].is_bad = true;
                
                // Add edges to polygon
                Edge edges[3] = {
                    { triangles[j].v1, triangles[j].v2 },
                    { triangles[j].v2, triangles[j].v3 },
                    { triangles[j].v3, triangles[j].v1 }
                };
                
                for (int k = 0; k < 3; k++) {
                    // Ensure capacity
                    if (edge_count >= edge_capacity) {
                        edge_capacity *= 2;
                        Edge *new_edges = realloc(polygon_edges, edge_capacity * sizeof(Edge));
                        if (!new_edges) {
                            free(polygon_edges);
                            free(triangles);
                            free(work_points);
                            free(result->points);
                            free(result);
                            return NULL;
                        }
                        polygon_edges = new_edges;
                    }
                    polygon_edges[edge_count++] = edges[k];
                }
            }
        }
        
        // Remove bad triangles
        size_t new_tri_count = 0;
        for (size_t j = 0; j < tri_count; j++) {
            if (!triangles[j].is_bad) {
                triangles[new_tri_count++] = triangles[j];
            }
        }
        tri_count = new_tri_count;
        
        // Find unique edges (edges that appear only once)
        for (size_t j = 0; j < edge_count; j++) {
            bool is_unique = true;
            for (size_t k = 0; k < edge_count; k++) {
                if (j != k && edges_equal(&polygon_edges[j], &polygon_edges[k])) {
                    is_unique = false;
                    break;
                }
            }
            
            if (is_unique) {
                // Create new triangle with this edge and the new point
                if (tri_count >= tri_capacity) {
                    tri_capacity *= 2;
                    TriangleWork *new_tri = realloc(triangles, tri_capacity * sizeof(TriangleWork));
                    if (!new_tri) {
                        free(polygon_edges);
                        free(triangles);
                        free(work_points);
                        free(result->points);
                        free(result);
                        return NULL;
                    }
                    triangles = new_tri;
                }
                
                TriangleWork *new_t = &triangles[tri_count];
                new_t->v1 = polygon_edges[j].v1;
                new_t->v2 = polygon_edges[j].v2;
                new_t->v3 = i;
                new_t->is_bad = false;
                
                calculate_circumcircle(&work_points[new_t->v1],
                                       &work_points[new_t->v2],
                                       &work_points[new_t->v3],
                                       &new_t->circumcenter,
                                       &new_t->circumradius_sq);
                tri_count++;
            }
        }
    }
    
    free(polygon_edges);
    free(work_points);
    
    // Remove triangles that share vertices with super triangle
    size_t final_count = 0;
    for (size_t i = 0; i < tri_count; i++) {
        if (!shares_vertex_with_super(&triangles[i], count)) {
            triangles[final_count++] = triangles[i];
        }
    }
    
    // Copy to result
    result->triangles = malloc(final_count * sizeof(DelaunayTriangle));
    if (!result->triangles) {
        free(triangles);
        free(result->points);
        free(result);
        return NULL;
    }
    
    for (size_t i = 0; i < final_count; i++) {
        result->triangles[i].v1 = triangles[i].v1;
        result->triangles[i].v2 = triangles[i].v2;
        result->triangles[i].v3 = triangles[i].v3;
        result->triangles[i].circumcenter = triangles[i].circumcenter;
        result->triangles[i].circumradius = sqrt(triangles[i].circumradius_sq);
    }
    result->count = final_count;
    
    free(triangles);
    return result;
}

void delaunay_free(DelaunayTriangulation *tri) {
    if (tri) {
        free(tri->triangles);
        free(tri->points);
        free(tri);
    }
}

int delaunay_find_triangle(const DelaunayTriangulation *tri, const Point *p) {
    if (!tri || !p) return -1;
    
    for (size_t i = 0; i < tri->count; i++) {
        const DelaunayTriangle *t = &tri->triangles[i];
        const Point *p1 = &tri->points[t->v1];
        const Point *p2 = &tri->points[t->v2];
        const Point *p3 = &tri->points[t->v3];
        
        // Check if point is inside triangle using barycentric coordinates
        double d1 = (p->x - p2->x) * (p1->y - p2->y) - (p1->x - p2->x) * (p->y - p2->y);
        double d2 = (p->x - p3->x) * (p2->y - p3->y) - (p2->x - p3->x) * (p->y - p3->y);
        double d3 = (p->x - p1->x) * (p3->y - p1->y) - (p3->x - p1->x) * (p->y - p1->y);
        
        bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        
        if (!(has_neg && has_pos)) {
            return (int)i;
        }
    }
    
    return -1;
}

size_t delaunay_get_neighbors(const DelaunayTriangulation *tri, size_t point_idx,
                               size_t *neighbors, size_t max_neighbors) {
    if (!tri || !neighbors || point_idx >= tri->num_points) {
        return 0;
    }
    
    size_t count = 0;
    
    for (size_t i = 0; i < tri->count && count < max_neighbors; i++) {
        const DelaunayTriangle *t = &tri->triangles[i];
        
        if (t->v1 == point_idx) {
            // Add v2 and v3 if not already present
            bool found2 = false, found3 = false;
            for (size_t j = 0; j < count; j++) {
                if (neighbors[j] == t->v2) found2 = true;
                if (neighbors[j] == t->v3) found3 = true;
            }
            if (!found2 && count < max_neighbors) neighbors[count++] = t->v2;
            if (!found3 && count < max_neighbors) neighbors[count++] = t->v3;
        }
        else if (t->v2 == point_idx) {
            bool found1 = false, found3 = false;
            for (size_t j = 0; j < count; j++) {
                if (neighbors[j] == t->v1) found1 = true;
                if (neighbors[j] == t->v3) found3 = true;
            }
            if (!found1 && count < max_neighbors) neighbors[count++] = t->v1;
            if (!found3 && count < max_neighbors) neighbors[count++] = t->v3;
        }
        else if (t->v3 == point_idx) {
            bool found1 = false, found2 = false;
            for (size_t j = 0; j < count; j++) {
                if (neighbors[j] == t->v1) found1 = true;
                if (neighbors[j] == t->v2) found2 = true;
            }
            if (!found1 && count < max_neighbors) neighbors[count++] = t->v1;
            if (!found2 && count < max_neighbors) neighbors[count++] = t->v2;
        }
    }
    
    return count;
}

/* ============================================================================
 * Voronoi Diagram
 * ============================================================================ */

VoronoiDiagram* voronoi_create(const Point *points, const uint64_t *point_ids,
                                size_t count, const MBR *bounds) {
    if (!points || count < 2) {
        return NULL;
    }
    
    // First create Delaunay triangulation
    DelaunayTriangulation *delaunay = delaunay_create(points, count);
    if (!delaunay) {
        return NULL;
    }
    
    // Compute bounds if not provided
    MBR actual_bounds;
    if (bounds) {
        actual_bounds = *bounds;
    } else {
        actual_bounds.min_x = DBL_MAX;
        actual_bounds.min_y = DBL_MAX;
        actual_bounds.max_x = -DBL_MAX;
        actual_bounds.max_y = -DBL_MAX;
        
        for (size_t i = 0; i < count; i++) {
            if (points[i].x < actual_bounds.min_x) actual_bounds.min_x = points[i].x;
            if (points[i].y < actual_bounds.min_y) actual_bounds.min_y = points[i].y;
            if (points[i].x > actual_bounds.max_x) actual_bounds.max_x = points[i].x;
            if (points[i].y > actual_bounds.max_y) actual_bounds.max_y = points[i].y;
        }
        
        // Expand bounds slightly
        double dx = (actual_bounds.max_x - actual_bounds.min_x) * 0.1;
        double dy = (actual_bounds.max_y - actual_bounds.min_y) * 0.1;
        actual_bounds.min_x -= dx;
        actual_bounds.min_y -= dy;
        actual_bounds.max_x += dx;
        actual_bounds.max_y += dy;
    }
    
    // Allocate Voronoi diagram
    VoronoiDiagram *voronoi = malloc(sizeof(VoronoiDiagram));
    if (!voronoi) {
        delaunay_free(delaunay);
        return NULL;
    }
    
    voronoi->cells = malloc(count * sizeof(VoronoiCell));
    if (!voronoi->cells) {
        free(voronoi);
        delaunay_free(delaunay);
        return NULL;
    }
    
    voronoi->count = count;
    voronoi->bounds = actual_bounds;
    
    // For each point, find surrounding circumcenters to form Voronoi cell
    for (size_t i = 0; i < count; i++) {
        VoronoiCell *cell = &voronoi->cells[i];
        cell->point_id = point_ids ? point_ids[i] : i;
        cell->site = points[i];
        
        // Collect circumcenters of triangles containing this point
        Point *circumcenters = malloc(delaunay->count * sizeof(Point));
        size_t cc_count = 0;
        
        for (size_t j = 0; j < delaunay->count; j++) {
            const DelaunayTriangle *t = &delaunay->triangles[j];
            if (t->v1 == i || t->v2 == i || t->v3 == i) {
                circumcenters[cc_count++] = t->circumcenter;
            }
        }
        
        // Sort circumcenters by angle around the site
        if (cc_count > 0) {
            // Calculate angles
            double *angles = malloc(cc_count * sizeof(double));
            for (size_t j = 0; j < cc_count; j++) {
                angles[j] = atan2(circumcenters[j].y - cell->site.y,
                                  circumcenters[j].x - cell->site.x);
            }
            
            // Simple bubble sort by angle
            for (size_t j = 0; j < cc_count - 1; j++) {
                for (size_t k = j + 1; k < cc_count; k++) {
                    if (angles[j] > angles[k]) {
                        double tmp_a = angles[j];
                        angles[j] = angles[k];
                        angles[k] = tmp_a;
                        Point tmp_p = circumcenters[j];
                        circumcenters[j] = circumcenters[k];
                        circumcenters[k] = tmp_p;
                    }
                }
            }
            free(angles);
        }
        
        // Initialize cell polygon
        polygon_init(&cell->cell, cc_count + 1);
        
        // Clip to bounds and add points
        for (size_t j = 0; j < cc_count; j++) {
            Point p = circumcenters[j];
            // Clamp to bounds
            if (p.x < actual_bounds.min_x) p.x = actual_bounds.min_x;
            if (p.y < actual_bounds.min_y) p.y = actual_bounds.min_y;
            if (p.x > actual_bounds.max_x) p.x = actual_bounds.max_x;
            if (p.y > actual_bounds.max_y) p.y = actual_bounds.max_y;
            
            polygon_add_exterior_point(&cell->cell, p);
        }
        
        // Close polygon
        if (cc_count > 0) {
            Point first = circumcenters[0];
            if (first.x < actual_bounds.min_x) first.x = actual_bounds.min_x;
            if (first.y < actual_bounds.min_y) first.y = actual_bounds.min_y;
            if (first.x > actual_bounds.max_x) first.x = actual_bounds.max_x;
            if (first.y > actual_bounds.max_y) first.y = actual_bounds.max_y;
            polygon_add_exterior_point(&cell->cell, first);
        }
        
        free(circumcenters);
    }
    
    delaunay_free(delaunay);
    return voronoi;
}

void voronoi_free(VoronoiDiagram *diagram) {
    if (diagram) {
        for (size_t i = 0; i < diagram->count; i++) {
            polygon_free(&diagram->cells[i].cell);
        }
        free(diagram->cells);
        free(diagram);
    }
}

int voronoi_find_cell(const VoronoiDiagram *diagram, const Point *p) {
    if (!diagram || !p) return -1;
    
    // Simple nearest neighbor search
    double min_dist = DBL_MAX;
    int nearest = -1;
    
    for (size_t i = 0; i < diagram->count; i++) {
        double dist = point_distance_sq(p, &diagram->cells[i].site);
        if (dist < min_dist) {
            min_dist = dist;
            nearest = (int)i;
        }
    }
    
    return nearest;
}

