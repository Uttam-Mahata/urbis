/**
 * @file compression.h
 * @brief Page compression for reduced I/O bandwidth
 * 
 * Provides transparent compression for disk pages using LZ4 (fast)
 * or ZSTD (better ratio) algorithms.
 */

#ifndef URBIS_COMPRESSION_H
#define URBIS_COMPRESSION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define COMPRESS_MAGIC 0x5A495043  /* "ZIPC" */
#define COMPRESS_VERSION 1

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Compression algorithm types
 */
typedef enum {
    COMPRESS_NONE = 0,      /**< No compression */
    COMPRESS_LZ4 = 1,       /**< LZ4 - fast, moderate ratio */
    COMPRESS_ZSTD = 2       /**< ZSTD - slower, better ratio */
} CompressionType;

/**
 * @brief Compression level (1-12, higher = better ratio, slower)
 */
typedef enum {
    COMPRESS_LEVEL_FAST = 1,
    COMPRESS_LEVEL_DEFAULT = 6,
    COMPRESS_LEVEL_BEST = 12
} CompressionLevel;

/**
 * @brief Header prepended to compressed data
 */
typedef struct {
    uint32_t magic;             /**< Magic number for validation */
    uint32_t version;           /**< Format version */
    uint32_t original_size;     /**< Uncompressed size */
    uint32_t compressed_size;   /**< Compressed size (excluding header) */
    uint8_t compression_type;   /**< Algorithm used */
    uint8_t level;              /**< Compression level used */
    uint8_t reserved[2];        /**< Reserved for future use */
    uint32_t checksum;          /**< CRC32 of original data */
} CompressedHeader;

/**
 * @brief Compression configuration
 */
typedef struct {
    CompressionType type;       /**< Algorithm to use */
    CompressionLevel level;     /**< Compression level */
    size_t min_size;            /**< Minimum size to compress (skip small data) */
    double min_ratio;           /**< Minimum compression ratio to keep (e.g., 0.9) */
} CompressionConfig;

/**
 * @brief Compression statistics
 */
typedef struct {
    uint64_t total_original;    /**< Total uncompressed bytes */
    uint64_t total_compressed;  /**< Total compressed bytes */
    uint64_t compress_calls;    /**< Number of compress operations */
    uint64_t decompress_calls;  /**< Number of decompress operations */
    uint64_t skipped_small;     /**< Skipped due to min_size */
    uint64_t skipped_ratio;     /**< Skipped due to poor ratio */
    double avg_ratio;           /**< Average compression ratio */
    double compress_time_ms;    /**< Total compression time */
    double decompress_time_ms;  /**< Total decompression time */
} CompressionStats;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    COMPRESS_OK = 0,
    COMPRESS_ERR_NULL_PTR = -1,
    COMPRESS_ERR_ALLOC = -2,
    COMPRESS_ERR_INVALID = -3,
    COMPRESS_ERR_CORRUPT = -4,
    COMPRESS_ERR_OVERFLOW = -5,
    COMPRESS_ERR_UNSUPPORTED = -6
} CompressionError;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Get default compression configuration
 */
CompressionConfig compression_default_config(void);

/**
 * @brief Create a high-speed configuration (LZ4, fast)
 */
CompressionConfig compression_fast_config(void);

/**
 * @brief Create a high-ratio configuration (ZSTD, best)
 */
CompressionConfig compression_best_config(void);

/* ============================================================================
 * Compression Operations
 * ============================================================================ */

/**
 * @brief Compress data
 * @param input Input data to compress
 * @param input_size Size of input data
 * @param output Output buffer (allocated by caller)
 * @param output_capacity Capacity of output buffer
 * @param output_size Actual size written (including header)
 * @param config Compression configuration
 * @return COMPRESS_OK on success
 * 
 * Output buffer should be at least compress_bound(input_size) + sizeof(CompressedHeader)
 */
int compress_data(const uint8_t *input, size_t input_size,
                  uint8_t *output, size_t output_capacity, size_t *output_size,
                  const CompressionConfig *config);

/**
 * @brief Decompress data
 * @param input Compressed data (with header)
 * @param input_size Size of compressed data
 * @param output Output buffer (allocated by caller)
 * @param output_capacity Capacity of output buffer
 * @param output_size Actual decompressed size
 * @return COMPRESS_OK on success
 */
int decompress_data(const uint8_t *input, size_t input_size,
                    uint8_t *output, size_t output_capacity, size_t *output_size);

/**
 * @brief Get maximum compressed size for given input size
 */
size_t compress_bound(size_t input_size, CompressionType type);

/**
 * @brief Check if data is compressed (has valid header)
 */
bool is_compressed(const uint8_t *data, size_t size);

/**
 * @brief Get original size from compressed data header
 * @return Original size, or 0 if not valid compressed data
 */
size_t compressed_original_size(const uint8_t *data, size_t size);

/**
 * @brief Validate compressed data integrity
 */
bool compressed_validate(const uint8_t *data, size_t size);

/* ============================================================================
 * In-place Operations (for page buffers)
 * ============================================================================ */

/**
 * @brief Compress data in place (reallocates if needed)
 * @param data Pointer to data buffer (may be reallocated)
 * @param size Current data size
 * @param capacity Current buffer capacity
 * @param new_size Output: new data size after compression
 * @param config Compression configuration
 * @return COMPRESS_OK on success
 */
int compress_inplace(uint8_t **data, size_t size, size_t *capacity,
                     size_t *new_size, const CompressionConfig *config);

/**
 * @brief Decompress data in place (reallocates if needed)
 * @param data Pointer to data buffer (may be reallocated)
 * @param size Current compressed data size
 * @param capacity Current buffer capacity
 * @param new_size Output: decompressed size
 * @return COMPRESS_OK on success
 */
int decompress_inplace(uint8_t **data, size_t size, size_t *capacity,
                       size_t *new_size);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Get global compression statistics
 */
void compression_get_stats(CompressionStats *stats);

/**
 * @brief Reset compression statistics
 */
void compression_reset_stats(void);

/* ============================================================================
 * Utility
 * ============================================================================ */

/**
 * @brief Calculate CRC32 checksum
 */
uint32_t crc32_compute(const uint8_t *data, size_t size);

/**
 * @brief Get compression type name
 */
const char* compression_type_name(CompressionType type);

#ifdef __cplusplus
}
#endif

#endif /* URBIS_COMPRESSION_H */

