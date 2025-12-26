/**
 * @file parser.h
 * @brief GeoJSON and WKT parser for loading spatial data
 * 
 * Provides parsing functionality for standard GIS data formats,
 * including GeoJSON (RFC 7946) and WKT (Well-Known Text).
 */

#ifndef URBIS_PARSER_H
#define URBIS_PARSER_H

#include "geometry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define PARSER_MAX_DEPTH 64            /**< Maximum JSON nesting depth */
#define PARSER_MAX_STRING 4096         /**< Maximum string length */
#define PARSER_BUFFER_SIZE 65536       /**< Read buffer size */

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Parser error codes
 */
typedef enum {
    PARSE_OK = 0,
    PARSE_ERR_NULL_PTR = -1,
    PARSE_ERR_ALLOC = -2,
    PARSE_ERR_IO = -3,
    PARSE_ERR_SYNTAX = -4,
    PARSE_ERR_INVALID_GEOM = -5,
    PARSE_ERR_UNSUPPORTED = -6,
    PARSE_ERR_OVERFLOW = -7
} ParseError;

/**
 * @brief JSON token types
 */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_END
} JsonTokenType;

/**
 * @brief JSON value structure
 */
typedef struct JsonValue {
    JsonTokenType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct {
            struct JsonValue *items;
            size_t count;
        } array;
        struct {
            char **keys;
            struct JsonValue *values;
            size_t count;
        } object;
    } data;
} JsonValue;

/**
 * @brief Parser state
 */
typedef struct {
    const char *input;                 /**< Input string */
    size_t pos;                        /**< Current position */
    size_t len;                        /**< Input length */
    int line;                          /**< Current line number */
    int column;                        /**< Current column */
    char error_msg[256];               /**< Error message */
} ParserState;

/**
 * @brief Parsed feature with geometry and properties
 */
typedef struct {
    SpatialObject object;              /**< Spatial object */
    JsonValue properties;              /**< Feature properties */
    char *id_str;                      /**< Optional string ID */
} ParsedFeature;

/**
 * @brief Collection of parsed features
 */
typedef struct {
    ParsedFeature *features;           /**< Array of features */
    size_t count;                      /**< Number of features */
    size_t capacity;                   /**< Array capacity */
    MBR bounds;                        /**< Overall bounds */
} FeatureCollection;

/**
 * @brief Parser callbacks for streaming parsing
 */
typedef struct {
    void *user_data;
    int (*on_feature)(void *user_data, const ParsedFeature *feature);
    void (*on_error)(void *user_data, const char *message, int line, int col);
    void (*on_progress)(void *user_data, size_t bytes_read, size_t total_bytes);
} ParserCallbacks;

/* ============================================================================
 * GeoJSON Parsing
 * ============================================================================ */

/**
 * @brief Parse a GeoJSON string into a feature collection
 */
int geojson_parse_string(const char *json, FeatureCollection *result);

/**
 * @brief Parse a GeoJSON file into a feature collection
 */
int geojson_parse_file(const char *path, FeatureCollection *result);

/**
 * @brief Parse GeoJSON with streaming callbacks
 */
int geojson_parse_stream(FILE *file, const ParserCallbacks *callbacks);

/**
 * @brief Parse a single GeoJSON geometry
 */
int geojson_parse_geometry(const char *json, SpatialObject *obj);

/**
 * @brief Export spatial object to GeoJSON string
 * @param obj Spatial object to export
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of characters written, or negative on error
 */
int geojson_export(const SpatialObject *obj, char *buffer, size_t buffer_size);

/**
 * @brief Export feature collection to GeoJSON string
 */
int geojson_export_collection(const FeatureCollection *fc, 
                               char *buffer, size_t buffer_size);

/* ============================================================================
 * WKT Parsing
 * ============================================================================ */

/**
 * @brief Parse a WKT string into a spatial object
 */
int wkt_parse(const char *wkt, SpatialObject *obj);

/**
 * @brief Export spatial object to WKT string
 */
int wkt_export(const SpatialObject *obj, char *buffer, size_t buffer_size);

/* ============================================================================
 * Feature Collection Operations
 * ============================================================================ */

/**
 * @brief Initialize a feature collection
 */
int feature_collection_init(FeatureCollection *fc, size_t capacity);

/**
 * @brief Free feature collection resources
 */
void feature_collection_free(FeatureCollection *fc);

/**
 * @brief Add a feature to the collection
 */
int feature_collection_add(FeatureCollection *fc, const ParsedFeature *feature);

/**
 * @brief Get feature by index
 */
ParsedFeature* feature_collection_get(FeatureCollection *fc, size_t index);

/**
 * @brief Clear feature collection
 */
void feature_collection_clear(FeatureCollection *fc);

/* ============================================================================
 * JSON Value Operations
 * ============================================================================ */

/**
 * @brief Free JSON value resources
 */
void json_value_free(JsonValue *value);

/**
 * @brief Get JSON object property by key
 */
JsonValue* json_object_get(const JsonValue *obj, const char *key);

/**
 * @brief Get JSON array element by index
 */
JsonValue* json_array_get(const JsonValue *arr, size_t index);

/**
 * @brief Check if JSON value is null
 */
bool json_is_null(const JsonValue *value);

/**
 * @brief Get boolean value
 */
bool json_get_bool(const JsonValue *value, bool default_val);

/**
 * @brief Get number value
 */
double json_get_number(const JsonValue *value, double default_val);

/**
 * @brief Get string value
 */
const char* json_get_string(const JsonValue *value, const char *default_val);

/* ============================================================================
 * Parser Utilities
 * ============================================================================ */

/**
 * @brief Get error message from last parse operation
 */
const char* parser_get_error(const ParserState *state);

/**
 * @brief Validate GeoJSON string
 */
bool geojson_validate(const char *json);

/**
 * @brief Validate WKT string
 */
bool wkt_validate(const char *wkt);

/**
 * @brief Detect geometry type from GeoJSON or WKT string
 */
GeomType parser_detect_type(const char *input);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_PARSER_H */

