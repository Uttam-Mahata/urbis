/**
 * @file parser.c
 * @brief GeoJSON and WKT parser implementation
 */

#define _GNU_SOURCE  /* For strcasecmp/strncasecmp */
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* For strcasecmp/strncasecmp */
#include <ctype.h>
#include <math.h>
#include <errno.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define GROWTH_FACTOR 2

/**
 * @brief Skip whitespace in parser state
 */
static void skip_whitespace(ParserState *state) {
    while (state->pos < state->len) {
        char c = state->input[state->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            state->pos++;
            state->column++;
        } else if (c == '\n') {
            state->pos++;
            state->line++;
            state->column = 1;
        } else {
            break;
        }
    }
}

/**
 * @brief Peek at current character
 */
static char peek(ParserState *state) {
    if (state->pos >= state->len) return '\0';
    return state->input[state->pos];
}

/**
 * @brief Consume current character
 */
static char consume(ParserState *state) {
    if (state->pos >= state->len) return '\0';
    char c = state->input[state->pos++];
    if (c == '\n') {
        state->line++;
        state->column = 1;
    } else {
        state->column++;
    }
    return c;
}

/**
 * @brief Expect a specific character
 */
static int expect(ParserState *state, char expected) {
    skip_whitespace(state);
    if (peek(state) != expected) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Expected '%c' at line %d, column %d",
                 expected, state->line, state->column);
        return PARSE_ERR_SYNTAX;
    }
    consume(state);
    return PARSE_OK;
}

/**
 * @brief Parse a JSON string
 */
static int parse_string(ParserState *state, char **out) {
    skip_whitespace(state);
    
    if (peek(state) != '"') {
        return PARSE_ERR_SYNTAX;
    }
    consume(state);
    
    size_t start = state->pos;
    size_t len = 0;
    
    while (state->pos < state->len) {
        char c = state->input[state->pos];
        if (c == '"') {
            break;
        }
        if (c == '\\') {
            state->pos++;
            if (state->pos >= state->len) break;
        }
        state->pos++;
        len++;
    }
    
    *out = (char *)malloc(len + 1);
    if (!*out) return PARSE_ERR_ALLOC;
    
    /* Copy string, handling escapes */
    size_t j = 0;
    for (size_t i = start; i < start + len && j < len; i++) {
        char c = state->input[i];
        if (c == '\\' && i + 1 < start + len) {
            i++;
            c = state->input[i];
            switch (c) {
                case 'n': (*out)[j++] = '\n'; break;
                case 't': (*out)[j++] = '\t'; break;
                case 'r': (*out)[j++] = '\r'; break;
                case '"': (*out)[j++] = '"'; break;
                case '\\': (*out)[j++] = '\\'; break;
                default: (*out)[j++] = c; break;
            }
        } else {
            (*out)[j++] = c;
        }
    }
    (*out)[j] = '\0';
    
    consume(state);  /* Closing quote */
    
    return PARSE_OK;
}

/**
 * @brief Parse a JSON number
 */
static int parse_number(ParserState *state, double *out) {
    skip_whitespace(state);
    
    char *end;
    *out = strtod(&state->input[state->pos], &end);
    
    if (end == &state->input[state->pos]) {
        return PARSE_ERR_SYNTAX;
    }
    
    state->pos = end - state->input;
    
    return PARSE_OK;
}

/**
 * @brief Parse a JSON value (forward declaration)
 */
static int parse_value(ParserState *state, JsonValue *value);

/**
 * @brief Parse a JSON array
 */
static int parse_array(ParserState *state, JsonValue *value) {
    if (expect(state, '[') != PARSE_OK) return PARSE_ERR_SYNTAX;
    
    value->type = JSON_ARRAY;
    value->data.array.items = NULL;
    value->data.array.count = 0;
    
    size_t capacity = 8;
    value->data.array.items = (JsonValue *)malloc(capacity * sizeof(JsonValue));
    if (!value->data.array.items) return PARSE_ERR_ALLOC;
    
    skip_whitespace(state);
    if (peek(state) == ']') {
        consume(state);
        return PARSE_OK;
    }
    
    while (1) {
        if (value->data.array.count >= capacity) {
            capacity *= 2;
            JsonValue *new_items = (JsonValue *)realloc(value->data.array.items,
                                                         capacity * sizeof(JsonValue));
            if (!new_items) return PARSE_ERR_ALLOC;
            value->data.array.items = new_items;
        }
        
        int err = parse_value(state, &value->data.array.items[value->data.array.count]);
        if (err != PARSE_OK) return err;
        value->data.array.count++;
        
        skip_whitespace(state);
        if (peek(state) == ']') {
            consume(state);
            break;
        }
        if (expect(state, ',') != PARSE_OK) return PARSE_ERR_SYNTAX;
    }
    
    return PARSE_OK;
}

/**
 * @brief Parse a JSON object
 */
static int parse_object(ParserState *state, JsonValue *value) {
    if (expect(state, '{') != PARSE_OK) return PARSE_ERR_SYNTAX;
    
    value->type = JSON_OBJECT;
    value->data.object.keys = NULL;
    value->data.object.values = NULL;
    value->data.object.count = 0;
    
    size_t capacity = 8;
    value->data.object.keys = (char **)malloc(capacity * sizeof(char *));
    value->data.object.values = (JsonValue *)malloc(capacity * sizeof(JsonValue));
    if (!value->data.object.keys || !value->data.object.values) {
        /* Cleanup on allocation failure */
        free(value->data.object.keys);
        free(value->data.object.values);
        return PARSE_ERR_ALLOC;
    }
    
    skip_whitespace(state);
    if (peek(state) == '}') {
        consume(state);
        return PARSE_OK;
    }
    
    while (1) {
        if (value->data.object.count >= capacity) {
            capacity *= 2;
            char **new_keys = (char **)realloc(value->data.object.keys,
                                                capacity * sizeof(char *));
            JsonValue *new_vals = (JsonValue *)realloc(value->data.object.values,
                                                        capacity * sizeof(JsonValue));
            if (!new_keys || !new_vals) {
                /* Handle realloc failure: ensure we keep valid pointers if one succeeded */
                if (new_keys) value->data.object.keys = new_keys;
                if (new_vals) value->data.object.values = new_vals;
                return PARSE_ERR_ALLOC;
            }
            value->data.object.keys = new_keys;
            value->data.object.values = new_vals;
        }
        
        /* Parse key */
        int err = parse_string(state, &value->data.object.keys[value->data.object.count]);
        if (err != PARSE_OK) {
             return err;
        }
        
        /* Expect colon */
        if (expect(state, ':') != PARSE_OK) {
            /* Free the key we just parsed since we won't increment count */
            free(value->data.object.keys[value->data.object.count]);
            return PARSE_ERR_SYNTAX;
        }
        
        /* Parse value */
        err = parse_value(state, &value->data.object.values[value->data.object.count]);
        if (err != PARSE_OK) {
            /* Free the key we just parsed since we won't increment count */
            free(value->data.object.keys[value->data.object.count]);
            return err;
        }
        
        value->data.object.count++;
        
        skip_whitespace(state);
        if (peek(state) == '}') {
            consume(state);
            break;
        }
        if (expect(state, ',') != PARSE_OK) return PARSE_ERR_SYNTAX;
    }
    
    return PARSE_OK;
}

/**
 * @brief Parse a JSON value
 */
static int parse_value(ParserState *state, JsonValue *value) {
    skip_whitespace(state);
    
    char c = peek(state);
    
    if (c == '"') {
        value->type = JSON_STRING;
        return parse_string(state, &value->data.string);
    }
    
    if (c == '[') {
        return parse_array(state, value);
    }
    
    if (c == '{') {
        return parse_object(state, value);
    }
    
    if (c == 't') {
        if (strncmp(&state->input[state->pos], "true", 4) == 0) {
            state->pos += 4;
            value->type = JSON_BOOL;
            value->data.boolean = true;
            return PARSE_OK;
        }
    }
    
    if (c == 'f') {
        if (strncmp(&state->input[state->pos], "false", 5) == 0) {
            state->pos += 5;
            value->type = JSON_BOOL;
            value->data.boolean = false;
            return PARSE_OK;
        }
    }
    
    if (c == 'n') {
        if (strncmp(&state->input[state->pos], "null", 4) == 0) {
            state->pos += 4;
            value->type = JSON_NULL;
            return PARSE_OK;
        }
    }
    
    if (c == '-' || isdigit(c)) {
        value->type = JSON_NUMBER;
        return parse_number(state, &value->data.number);
    }
    
    snprintf(state->error_msg, sizeof(state->error_msg),
             "Unexpected character '%c' at line %d, column %d",
             c, state->line, state->column);
    return PARSE_ERR_SYNTAX;
}

/**
 * @brief Parse GeoJSON coordinates into a Point
 */
static int parse_coordinates_point(const JsonValue *coords, Point *p) {
    if (coords->type != JSON_ARRAY || coords->data.array.count < 2) {
        return PARSE_ERR_INVALID_GEOM;
    }
    
    if (coords->data.array.items[0].type != JSON_NUMBER ||
        coords->data.array.items[1].type != JSON_NUMBER) {
        return PARSE_ERR_INVALID_GEOM;
    }
    
    p->x = coords->data.array.items[0].data.number;
    p->y = coords->data.array.items[1].data.number;
    
    return PARSE_OK;
}

/**
 * @brief Parse GeoJSON geometry
 */
static int parse_geojson_geometry(const JsonValue *geom, SpatialObject *obj) {
    JsonValue *type = json_object_get(geom, "type");
    JsonValue *coords = json_object_get(geom, "coordinates");
    
    if (!type || type->type != JSON_STRING || !coords) {
        return PARSE_ERR_INVALID_GEOM;
    }
    
    const char *type_str = type->data.string;
    
    if (strcmp(type_str, "Point") == 0) {
        Point p;
        int err = parse_coordinates_point(coords, &p);
        if (err != PARSE_OK) return err;
        
        return spatial_object_init_point(obj, 0, p);
    }
    
    if (strcmp(type_str, "LineString") == 0) {
        if (coords->type != JSON_ARRAY) return PARSE_ERR_INVALID_GEOM;
        
        int err = spatial_object_init_linestring(obj, 0, coords->data.array.count);
        if (err != GEOM_OK) return PARSE_ERR_ALLOC;
        
        for (size_t i = 0; i < coords->data.array.count; i++) {
            Point p;
            err = parse_coordinates_point(&coords->data.array.items[i], &p);
            if (err != PARSE_OK) {
                spatial_object_free(obj);
                return err;
            }
            linestring_add_point(&obj->geom.line, p);
        }
        
        spatial_object_update_derived(obj);
        return PARSE_OK;
    }
    
    if (strcmp(type_str, "Polygon") == 0) {
        if (coords->type != JSON_ARRAY || coords->data.array.count < 1) {
            return PARSE_ERR_INVALID_GEOM;
        }
        
        /* First ring is exterior */
        JsonValue *ext_ring = &coords->data.array.items[0];
        if (ext_ring->type != JSON_ARRAY) return PARSE_ERR_INVALID_GEOM;
        
        int err = spatial_object_init_polygon(obj, 0, ext_ring->data.array.count);
        if (err != GEOM_OK) return PARSE_ERR_ALLOC;
        
        for (size_t i = 0; i < ext_ring->data.array.count; i++) {
            Point p;
            err = parse_coordinates_point(&ext_ring->data.array.items[i], &p);
            if (err != PARSE_OK) {
                spatial_object_free(obj);
                return err;
            }
            polygon_add_exterior_point(&obj->geom.polygon, p);
        }
        
        /* Additional rings are holes */
        for (size_t h = 1; h < coords->data.array.count; h++) {
            JsonValue *hole_ring = &coords->data.array.items[h];
            if (hole_ring->type != JSON_ARRAY) continue;
            
            err = polygon_add_hole(&obj->geom.polygon, hole_ring->data.array.count);
            if (err != GEOM_OK) continue;
            
            for (size_t i = 0; i < hole_ring->data.array.count; i++) {
                Point p;
                err = parse_coordinates_point(&hole_ring->data.array.items[i], &p);
                if (err == PARSE_OK) {
                    polygon_add_hole_point(&obj->geom.polygon, h - 1, p);
                }
            }
        }
        
        spatial_object_update_derived(obj);
        return PARSE_OK;
    }
    
    return PARSE_ERR_UNSUPPORTED;
}

/**
 * @brief Parse a GeoJSON feature
 */
static int parse_geojson_feature(const JsonValue *feature, ParsedFeature *parsed) {
    memset(parsed, 0, sizeof(ParsedFeature));
    
    JsonValue *geometry = json_object_get(feature, "geometry");
    if (!geometry) return PARSE_ERR_INVALID_GEOM;
    
    int err = parse_geojson_geometry(geometry, &parsed->object);
    if (err != PARSE_OK) return err;
    
    /* Parse ID if present */
    JsonValue *id = json_object_get(feature, "id");
    if (id) {
        if (id->type == JSON_NUMBER) {
            parsed->object.id = (uint64_t)id->data.number;
        } else if (id->type == JSON_STRING) {
            parsed->id_str = strdup(id->data.string);
        }
    }
    
    /* Copy properties */
    JsonValue *props = json_object_get(feature, "properties");
    if (props && props->type == JSON_OBJECT) {
        parsed->properties = *props;
        /* Note: shallow copy - properties share memory with parsed JSON */
    }
    
    return PARSE_OK;
}

/* ============================================================================
 * GeoJSON Parsing
 * ============================================================================ */

int geojson_parse_string(const char *json, FeatureCollection *result) {
    if (!json || !result) return PARSE_ERR_NULL_PTR;
    
    int err = feature_collection_init(result, 64);
    if (err != PARSE_OK) return err;
    
    ParserState state = {
        .input = json,
        .pos = 0,
        .len = strlen(json),
        .line = 1,
        .column = 1
    };
    
    /* Initialize root to empty/zero so we can safely free it if parse_value partially fails */
    JsonValue root;
    memset(&root, 0, sizeof(JsonValue));

    err = parse_value(&state, &root);
    if (err != PARSE_OK) {
        /* Ensure any partial allocations in root are freed */
        json_value_free(&root);
        feature_collection_free(result);
        return err;
    }
    
    if (root.type != JSON_OBJECT) {
        json_value_free(&root);
        feature_collection_free(result);
        return PARSE_ERR_SYNTAX;
    }
    
    JsonValue *type = json_object_get(&root, "type");
    if (!type || type->type != JSON_STRING) {
        json_value_free(&root);
        feature_collection_free(result);
        return PARSE_ERR_SYNTAX;
    }
    
    if (strcmp(type->data.string, "FeatureCollection") == 0) {
        JsonValue *features = json_object_get(&root, "features");
        if (!features || features->type != JSON_ARRAY) {
            json_value_free(&root);
            feature_collection_free(result);
            return PARSE_ERR_SYNTAX;
        }
        
        for (size_t i = 0; i < features->data.array.count; i++) {
            ParsedFeature parsed;
            err = parse_geojson_feature(&features->data.array.items[i], &parsed);
            if (err == PARSE_OK) {
                feature_collection_add(result, &parsed);
            }
        }
    } else if (strcmp(type->data.string, "Feature") == 0) {
        ParsedFeature parsed;
        err = parse_geojson_feature(&root, &parsed);
        if (err == PARSE_OK) {
            feature_collection_add(result, &parsed);
        }
    } else {
        /* Single geometry */
        ParsedFeature parsed;
        memset(&parsed, 0, sizeof(parsed));
        err = parse_geojson_geometry(&root, &parsed.object);
        if (err == PARSE_OK) {
            feature_collection_add(result, &parsed);
        }
    }
    
    /* Update bounds */
    result->bounds = mbr_empty();
    for (size_t i = 0; i < result->count; i++) {
        mbr_expand_mbr(&result->bounds, &result->features[i].object.mbr);
    }
    
    json_value_free(&root);
    
    return PARSE_OK;
}

int geojson_parse_file(const char *path, FeatureCollection *result) {
    if (!path || !result) return PARSE_ERR_NULL_PTR;
    
    FILE *file = fopen(path, "r");
    if (!file) return PARSE_ERR_IO;
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(file);
        return PARSE_ERR_IO;
    }
    
    /* Read entire file */
    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return PARSE_ERR_ALLOC;
    }
    
    size_t read = fread(buffer, 1, size, file);
    fclose(file);
    
    buffer[read] = '\0';
    
    int err = geojson_parse_string(buffer, result);
    free(buffer);
    
    return err;
}

int geojson_parse_stream(FILE *file, const ParserCallbacks *callbacks) {
    if (!file || !callbacks) return PARSE_ERR_NULL_PTR;
    
    /* Simple implementation: read file and parse */
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = (char *)malloc(size + 1);
    if (!buffer) return PARSE_ERR_ALLOC;
    
    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    
    FeatureCollection fc;
    int err = geojson_parse_string(buffer, &fc);
    free(buffer);
    
    if (err != PARSE_OK) {
        if (callbacks->on_error) {
            callbacks->on_error(callbacks->user_data, "Parse error", 0, 0);
        }
        return err;
    }
    
    /* Call callback for each feature */
    for (size_t i = 0; i < fc.count; i++) {
        if (callbacks->on_feature) {
            err = callbacks->on_feature(callbacks->user_data, &fc.features[i]);
            if (err != 0) break;
        }
    }
    
    feature_collection_free(&fc);
    
    return PARSE_OK;
}

int geojson_parse_geometry(const char *json, SpatialObject *obj) {
    if (!json || !obj) return PARSE_ERR_NULL_PTR;
    
    ParserState state = {
        .input = json,
        .pos = 0,
        .len = strlen(json),
        .line = 1,
        .column = 1
    };
    
    JsonValue root;
    int err = parse_value(&state, &root);
    if (err != PARSE_OK) return err;
    
    err = parse_geojson_geometry(&root, obj);
    json_value_free(&root);
    
    return err;
}

int geojson_export(const SpatialObject *obj, char *buffer, size_t buffer_size) {
    if (!obj || !buffer || buffer_size == 0) return PARSE_ERR_NULL_PTR;
    
    int written = 0;
    
    switch (obj->type) {
        case GEOM_POINT:
            written = snprintf(buffer, buffer_size,
                "{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f]}",
                obj->geom.point.x, obj->geom.point.y);
            break;
            
        case GEOM_LINESTRING: {
            written = snprintf(buffer, buffer_size, "{\"type\":\"LineString\",\"coordinates\":[");
            for (size_t i = 0; i < obj->geom.line.count && written < (int)buffer_size; i++) {
                if (i > 0) written += snprintf(buffer + written, buffer_size - written, ",");
                written += snprintf(buffer + written, buffer_size - written,
                    "[%.6f,%.6f]",
                    obj->geom.line.points[i].x, obj->geom.line.points[i].y);
            }
            written += snprintf(buffer + written, buffer_size - written, "]}");
            break;
        }
            
        case GEOM_POLYGON: {
            written = snprintf(buffer, buffer_size, "{\"type\":\"Polygon\",\"coordinates\":[[");
            for (size_t i = 0; i < obj->geom.polygon.ext_count && written < (int)buffer_size; i++) {
                if (i > 0) written += snprintf(buffer + written, buffer_size - written, ",");
                written += snprintf(buffer + written, buffer_size - written,
                    "[%.6f,%.6f]",
                    obj->geom.polygon.exterior[i].x, obj->geom.polygon.exterior[i].y);
            }
            written += snprintf(buffer + written, buffer_size - written, "]]}");
            break;
        }
    }
    
    return written;
}

int geojson_export_collection(const FeatureCollection *fc, char *buffer, size_t buffer_size) {
    if (!fc || !buffer || buffer_size == 0) return PARSE_ERR_NULL_PTR;
    
    int written = snprintf(buffer, buffer_size, "{\"type\":\"FeatureCollection\",\"features\":[");
    
    for (size_t i = 0; i < fc->count && written < (int)buffer_size; i++) {
        if (i > 0) written += snprintf(buffer + written, buffer_size - written, ",");
        
        written += snprintf(buffer + written, buffer_size - written, "{\"type\":\"Feature\",\"geometry\":");
        
        char geom_buf[4096];
        geojson_export(&fc->features[i].object, geom_buf, sizeof(geom_buf));
        written += snprintf(buffer + written, buffer_size - written, "%s,\"properties\":{}}", geom_buf);
    }
    
    written += snprintf(buffer + written, buffer_size - written, "]}");
    
    return written;
}

/* ============================================================================
 * WKT Parsing
 * ============================================================================ */

int wkt_parse(const char *wkt, SpatialObject *obj) {
    if (!wkt || !obj) return PARSE_ERR_NULL_PTR;
    
    /* Skip whitespace */
    while (*wkt && isspace(*wkt)) wkt++;
    
    if (strncasecmp(wkt, "POINT", 5) == 0) {
        wkt += 5;
        while (*wkt && (isspace(*wkt) || *wkt == '(')) wkt++;
        
        double x, y;
        if (sscanf(wkt, "%lf %lf", &x, &y) != 2) {
            return PARSE_ERR_SYNTAX;
        }
        
        return spatial_object_init_point(obj, 0, point_create(x, y));
    }
    
    if (strncasecmp(wkt, "LINESTRING", 10) == 0) {
        wkt += 10;
        while (*wkt && (isspace(*wkt) || *wkt == '(')) wkt++;
        
        int err = spatial_object_init_linestring(obj, 0, 16);
        if (err != GEOM_OK) return PARSE_ERR_ALLOC;
        
        while (*wkt && *wkt != ')') {
            double x, y;
            if (sscanf(wkt, "%lf %lf", &x, &y) == 2) {
                linestring_add_point(&obj->geom.line, point_create(x, y));
            }
            
            /* Skip to next coordinate */
            while (*wkt && *wkt != ',' && *wkt != ')') wkt++;
            if (*wkt == ',') wkt++;
            while (*wkt && isspace(*wkt)) wkt++;
        }
        
        spatial_object_update_derived(obj);
        return PARSE_OK;
    }
    
    if (strncasecmp(wkt, "POLYGON", 7) == 0) {
        wkt += 7;
        while (*wkt && (isspace(*wkt) || *wkt == '(')) wkt++;
        
        int err = spatial_object_init_polygon(obj, 0, 16);
        if (err != GEOM_OK) return PARSE_ERR_ALLOC;
        
        /* Skip to first ring */
        while (*wkt && *wkt != '(') wkt++;
        if (*wkt == '(') wkt++;
        
        while (*wkt && *wkt != ')') {
            double x, y;
            if (sscanf(wkt, "%lf %lf", &x, &y) == 2) {
                polygon_add_exterior_point(&obj->geom.polygon, point_create(x, y));
            }
            
            while (*wkt && *wkt != ',' && *wkt != ')') wkt++;
            if (*wkt == ',') wkt++;
            while (*wkt && isspace(*wkt)) wkt++;
        }
        
        spatial_object_update_derived(obj);
        return PARSE_OK;
    }
    
    return PARSE_ERR_UNSUPPORTED;
}

int wkt_export(const SpatialObject *obj, char *buffer, size_t buffer_size) {
    if (!obj || !buffer || buffer_size == 0) return PARSE_ERR_NULL_PTR;
    
    int written = 0;
    
    switch (obj->type) {
        case GEOM_POINT:
            written = snprintf(buffer, buffer_size, "POINT (%.6f %.6f)",
                obj->geom.point.x, obj->geom.point.y);
            break;
            
        case GEOM_LINESTRING: {
            written = snprintf(buffer, buffer_size, "LINESTRING (");
            for (size_t i = 0; i < obj->geom.line.count && written < (int)buffer_size; i++) {
                if (i > 0) written += snprintf(buffer + written, buffer_size - written, ", ");
                written += snprintf(buffer + written, buffer_size - written,
                    "%.6f %.6f",
                    obj->geom.line.points[i].x, obj->geom.line.points[i].y);
            }
            written += snprintf(buffer + written, buffer_size - written, ")");
            break;
        }
            
        case GEOM_POLYGON: {
            written = snprintf(buffer, buffer_size, "POLYGON ((");
            for (size_t i = 0; i < obj->geom.polygon.ext_count && written < (int)buffer_size; i++) {
                if (i > 0) written += snprintf(buffer + written, buffer_size - written, ", ");
                written += snprintf(buffer + written, buffer_size - written,
                    "%.6f %.6f",
                    obj->geom.polygon.exterior[i].x, obj->geom.polygon.exterior[i].y);
            }
            written += snprintf(buffer + written, buffer_size - written, "))");
            break;
        }
    }
    
    return written;
}

/* ============================================================================
 * Feature Collection Operations
 * ============================================================================ */

int feature_collection_init(FeatureCollection *fc, size_t capacity) {
    if (!fc) return PARSE_ERR_NULL_PTR;
    
    memset(fc, 0, sizeof(FeatureCollection));
    
    fc->capacity = capacity > 0 ? capacity : 64;
    fc->features = (ParsedFeature *)calloc(fc->capacity, sizeof(ParsedFeature));
    
    if (!fc->features) return PARSE_ERR_ALLOC;
    
    fc->bounds = mbr_empty();
    
    return PARSE_OK;
}

void feature_collection_free(FeatureCollection *fc) {
    if (!fc) return;
    
    for (size_t i = 0; i < fc->count; i++) {
        spatial_object_free(&fc->features[i].object);
        free(fc->features[i].id_str);
    }
    
    free(fc->features);
    memset(fc, 0, sizeof(FeatureCollection));
}

int feature_collection_add(FeatureCollection *fc, const ParsedFeature *feature) {
    if (!fc || !feature) return PARSE_ERR_NULL_PTR;
    
    if (fc->count >= fc->capacity) {
        size_t new_cap = fc->capacity * GROWTH_FACTOR;
        ParsedFeature *new_features = (ParsedFeature *)realloc(fc->features,
                                                                new_cap * sizeof(ParsedFeature));
        if (!new_features) return PARSE_ERR_ALLOC;
        fc->features = new_features;
        fc->capacity = new_cap;
    }
    
    fc->features[fc->count++] = *feature;
    mbr_expand_mbr(&fc->bounds, &feature->object.mbr);
    
    return PARSE_OK;
}

ParsedFeature* feature_collection_get(FeatureCollection *fc, size_t index) {
    if (!fc || index >= fc->count) return NULL;
    return &fc->features[index];
}

void feature_collection_clear(FeatureCollection *fc) {
    if (!fc) return;
    
    for (size_t i = 0; i < fc->count; i++) {
        spatial_object_free(&fc->features[i].object);
        free(fc->features[i].id_str);
    }
    
    fc->count = 0;
    fc->bounds = mbr_empty();
}

/* ============================================================================
 * JSON Value Operations
 * ============================================================================ */

void json_value_free(JsonValue *value) {
    if (!value) return;
    
    switch (value->type) {
        case JSON_STRING:
            free(value->data.string);
            break;
            
        case JSON_ARRAY:
            for (size_t i = 0; i < value->data.array.count; i++) {
                json_value_free(&value->data.array.items[i]);
            }
            free(value->data.array.items);
            break;
            
        case JSON_OBJECT:
            for (size_t i = 0; i < value->data.object.count; i++) {
                free(value->data.object.keys[i]);
                json_value_free(&value->data.object.values[i]);
            }
            free(value->data.object.keys);
            free(value->data.object.values);
            break;
            
        default:
            break;
    }
    
    memset(value, 0, sizeof(JsonValue));
}

JsonValue* json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || !key || obj->type != JSON_OBJECT) return NULL;
    
    for (size_t i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            return &obj->data.object.values[i];
        }
    }
    
    return NULL;
}

JsonValue* json_array_get(const JsonValue *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY || index >= arr->data.array.count) {
        return NULL;
    }
    return &arr->data.array.items[index];
}

bool json_is_null(const JsonValue *value) {
    return !value || value->type == JSON_NULL;
}

bool json_get_bool(const JsonValue *value, bool default_val) {
    if (!value || value->type != JSON_BOOL) return default_val;
    return value->data.boolean;
}

double json_get_number(const JsonValue *value, double default_val) {
    if (!value || value->type != JSON_NUMBER) return default_val;
    return value->data.number;
}

const char* json_get_string(const JsonValue *value, const char *default_val) {
    if (!value || value->type != JSON_STRING) return default_val;
    return value->data.string;
}

/* ============================================================================
 * Parser Utilities
 * ============================================================================ */

const char* parser_get_error(const ParserState *state) {
    if (!state) return "Unknown error";
    return state->error_msg;
}

bool geojson_validate(const char *json) {
    if (!json) return false;
    
    FeatureCollection fc;
    int err = geojson_parse_string(json, &fc);
    
    if (err == PARSE_OK) {
        feature_collection_free(&fc);
        return true;
    }
    
    return false;
}

bool wkt_validate(const char *wkt) {
    if (!wkt) return false;
    
    SpatialObject obj;
    int err = wkt_parse(wkt, &obj);
    
    if (err == PARSE_OK) {
        spatial_object_free(&obj);
        return true;
    }
    
    return false;
}

GeomType parser_detect_type(const char *input) {
    if (!input) return GEOM_POINT;
    
    while (*input && isspace(*input)) input++;
    
    if (strncasecmp(input, "POINT", 5) == 0 ||
        strstr(input, "\"Point\"") != NULL) {
        return GEOM_POINT;
    }
    
    if (strncasecmp(input, "LINESTRING", 10) == 0 ||
        strstr(input, "\"LineString\"") != NULL) {
        return GEOM_LINESTRING;
    }
    
    if (strncasecmp(input, "POLYGON", 7) == 0 ||
        strstr(input, "\"Polygon\"") != NULL) {
        return GEOM_POLYGON;
    }
    
    return GEOM_POINT;
}

