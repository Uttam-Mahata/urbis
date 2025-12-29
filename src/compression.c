/**
 * @file compression.c
 * @brief Page compression implementation using LZ4
 * 
 * Provides fast compression using LZ4 for spatial data pages.
 * Uses a simple LZ4-compatible implementation for portability.
 */

#include "compression.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Global Statistics
 * ============================================================================ */

static CompressionStats g_stats = {0};

/* ============================================================================
 * CRC32 Implementation
 * ============================================================================ */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t crc32_compute(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * Simple LZ4-style Compression (built-in, no external dependency)
 * ============================================================================ */

#define LZ4_MIN_MATCH 4
#define LZ4_MAX_OFFSET 65535
#define LZ4_HASH_LOG 12
#define LZ4_HASH_SIZE (1 << LZ4_HASH_LOG)

static inline uint32_t lz4_hash(uint32_t v) {
    return (v * 2654435761U) >> (32 - LZ4_HASH_LOG);
}

static inline uint32_t read32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/**
 * @brief Simple LZ4-style compression
 */
static size_t lz4_compress_simple(const uint8_t *src, size_t src_size,
                                   uint8_t *dst, size_t dst_capacity) {
    if (src_size < LZ4_MIN_MATCH) {
        /* Too small to compress - copy as literals */
        if (dst_capacity < src_size + 1) return 0;
        dst[0] = (uint8_t)((src_size << 4) & 0xF0);
        memcpy(dst + 1, src, src_size);
        return src_size + 1;
    }
    
    uint16_t hash_table[LZ4_HASH_SIZE];
    memset(hash_table, 0xFF, sizeof(hash_table));
    
    const uint8_t *src_end = src + src_size;
    const uint8_t *match_limit = src_end - LZ4_MIN_MATCH;
    const uint8_t *ip = src;
    const uint8_t *anchor = src;
    uint8_t *op = dst;
    uint8_t *op_end = dst + dst_capacity;
    
    while (ip < match_limit) {
        /* Find a match */
        uint32_t h = lz4_hash(read32(ip));
        const uint8_t *ref = src + hash_table[h];
        hash_table[h] = (uint16_t)(ip - src);
        
        if (ref >= src && ref < ip && ip - ref <= LZ4_MAX_OFFSET &&
            read32(ref) == read32(ip)) {
            /* Found a match */
            size_t literal_len = ip - anchor;
            
            /* Extend match forward */
            const uint8_t *match_start = ip;
            ip += LZ4_MIN_MATCH;
            ref += LZ4_MIN_MATCH;
            while (ip < src_end && *ip == *ref) {
                ip++;
                ref++;
            }
            size_t match_len = ip - match_start;
            size_t offset = match_start - (ref - match_len);
            
            /* Check output space */
            size_t needed = 1 + (literal_len >= 15 ? 1 + (literal_len - 15) / 255 + 1 : 0) +
                           literal_len + 2 + (match_len >= 19 ? 1 + (match_len - 19) / 255 + 1 : 0);
            if (op + needed > op_end) return 0;
            
            /* Write token */
            uint8_t *token = op++;
            *token = (uint8_t)(((literal_len >= 15 ? 15 : literal_len) << 4) |
                               (match_len - 4 >= 15 ? 15 : match_len - 4));
            
            /* Write literal length extension */
            if (literal_len >= 15) {
                size_t remaining = literal_len - 15;
                while (remaining >= 255) {
                    *op++ = 255;
                    remaining -= 255;
                }
                *op++ = (uint8_t)remaining;
            }
            
            /* Write literals */
            memcpy(op, anchor, literal_len);
            op += literal_len;
            
            /* Write offset */
            *op++ = (uint8_t)(offset & 0xFF);
            *op++ = (uint8_t)((offset >> 8) & 0xFF);
            
            /* Write match length extension */
            if (match_len - 4 >= 15) {
                size_t remaining = match_len - 4 - 15;
                while (remaining >= 255) {
                    *op++ = 255;
                    remaining -= 255;
                }
                *op++ = (uint8_t)remaining;
            }
            
            anchor = ip;
        } else {
            ip++;
        }
    }
    
    /* Write remaining literals */
    size_t literal_len = src_end - anchor;
    if (literal_len > 0) {
        size_t needed = 1 + (literal_len >= 15 ? 1 + (literal_len - 15) / 255 + 1 : 0) + literal_len;
        if (op + needed > op_end) return 0;
        
        uint8_t *token = op++;
        *token = (uint8_t)((literal_len >= 15 ? 15 : literal_len) << 4);
        
        if (literal_len >= 15) {
            size_t remaining = literal_len - 15;
            while (remaining >= 255) {
                *op++ = 255;
                remaining -= 255;
            }
            *op++ = (uint8_t)remaining;
        }
        
        memcpy(op, anchor, literal_len);
        op += literal_len;
    }
    
    return op - dst;
}

/**
 * @brief Simple LZ4-style decompression
 */
static size_t lz4_decompress_simple(const uint8_t *src, size_t src_size,
                                     uint8_t *dst, size_t dst_capacity) {
    const uint8_t *ip = src;
    const uint8_t *ip_end = src + src_size;
    uint8_t *op = dst;
    uint8_t *op_end = dst + dst_capacity;
    
    while (ip < ip_end) {
        uint8_t token = *ip++;
        size_t literal_len = (token >> 4) & 0x0F;
        
        /* Read literal length extension */
        if (literal_len == 15) {
            while (ip < ip_end && *ip == 255) {
                literal_len += 255;
                ip++;
            }
            if (ip < ip_end) {
                literal_len += *ip++;
            }
        }
        
        /* Copy literals */
        if (literal_len > 0) {
            if (op + literal_len > op_end || ip + literal_len > ip_end) return 0;
            memcpy(op, ip, literal_len);
            ip += literal_len;
            op += literal_len;
        }
        
        /* Check if this is the last sequence (no match) */
        if (ip >= ip_end) break;
        
        /* Read offset */
        if (ip + 2 > ip_end) return 0;
        size_t offset = ip[0] | (ip[1] << 8);
        ip += 2;
        
        if (offset == 0 || op - dst < (ptrdiff_t)offset) return 0;
        
        /* Read match length */
        size_t match_len = (token & 0x0F) + LZ4_MIN_MATCH;
        if ((token & 0x0F) == 15) {
            while (ip < ip_end && *ip == 255) {
                match_len += 255;
                ip++;
            }
            if (ip < ip_end) {
                match_len += *ip++;
            }
        }
        
        /* Copy match */
        if (op + match_len > op_end) return 0;
        const uint8_t *ref = op - offset;
        
        /* Handle overlapping copy */
        for (size_t i = 0; i < match_len; i++) {
            *op++ = ref[i];
        }
    }
    
    return op - dst;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

CompressionConfig compression_default_config(void) {
    CompressionConfig config = {
        .type = COMPRESS_LZ4,
        .level = COMPRESS_LEVEL_DEFAULT,
        .min_size = 64,
        .min_ratio = 0.95
    };
    return config;
}

CompressionConfig compression_fast_config(void) {
    CompressionConfig config = {
        .type = COMPRESS_LZ4,
        .level = COMPRESS_LEVEL_FAST,
        .min_size = 128,
        .min_ratio = 0.98
    };
    return config;
}

CompressionConfig compression_best_config(void) {
    CompressionConfig config = {
        .type = COMPRESS_LZ4,  /* Still use LZ4 for built-in implementation */
        .level = COMPRESS_LEVEL_BEST,
        .min_size = 32,
        .min_ratio = 0.9
    };
    return config;
}

/* ============================================================================
 * Compression Operations
 * ============================================================================ */

size_t compress_bound(size_t input_size, CompressionType type) {
    (void)type;  /* Currently only LZ4 */
    /* Worst case: no compression + length bytes */
    return input_size + (input_size / 255) + 16 + sizeof(CompressedHeader);
}

int compress_data(const uint8_t *input, size_t input_size,
                  uint8_t *output, size_t output_capacity, size_t *output_size,
                  const CompressionConfig *config) {
    if (!input || !output || !output_size) return COMPRESS_ERR_NULL_PTR;
    
    CompressionConfig cfg = config ? *config : compression_default_config();
    
    /* Skip compression for small data */
    if (input_size < cfg.min_size) {
        g_stats.skipped_small++;
        *output_size = 0;
        return COMPRESS_OK;
    }
    
    /* Check output capacity */
    size_t bound = compress_bound(input_size, cfg.type);
    if (output_capacity < bound) return COMPRESS_ERR_OVERFLOW;
    
    /* Prepare header */
    CompressedHeader header = {
        .magic = COMPRESS_MAGIC,
        .version = COMPRESS_VERSION,
        .original_size = (uint32_t)input_size,
        .compression_type = (uint8_t)cfg.type,
        .level = (uint8_t)cfg.level,
        .checksum = crc32_compute(input, input_size)
    };
    
    /* Compress data after header */
    uint8_t *compressed_data = output + sizeof(CompressedHeader);
    size_t compressed_capacity = output_capacity - sizeof(CompressedHeader);
    size_t compressed_size = 0;
    
    switch (cfg.type) {
        case COMPRESS_LZ4:
        case COMPRESS_ZSTD:  /* Fallback to LZ4 */
            compressed_size = lz4_compress_simple(input, input_size,
                                                   compressed_data, compressed_capacity);
            break;
        case COMPRESS_NONE:
        default:
            if (compressed_capacity < input_size) return COMPRESS_ERR_OVERFLOW;
            memcpy(compressed_data, input, input_size);
            compressed_size = input_size;
            break;
    }
    
    if (compressed_size == 0) return COMPRESS_ERR_OVERFLOW;
    
    /* Check if compression is worthwhile */
    double ratio = (double)compressed_size / (double)input_size;
    if (ratio > cfg.min_ratio) {
        g_stats.skipped_ratio++;
        *output_size = 0;
        return COMPRESS_OK;
    }
    
    /* Finalize header */
    header.compressed_size = (uint32_t)compressed_size;
    memcpy(output, &header, sizeof(CompressedHeader));
    
    *output_size = sizeof(CompressedHeader) + compressed_size;
    
    /* Update stats */
    g_stats.total_original += input_size;
    g_stats.total_compressed += *output_size;
    g_stats.compress_calls++;
    if (g_stats.compress_calls > 0) {
        g_stats.avg_ratio = (double)g_stats.total_compressed / 
                            (double)g_stats.total_original;
    }
    
    return COMPRESS_OK;
}

int decompress_data(const uint8_t *input, size_t input_size,
                    uint8_t *output, size_t output_capacity, size_t *output_size) {
    if (!input || !output || !output_size) return COMPRESS_ERR_NULL_PTR;
    
    /* Validate header */
    if (input_size < sizeof(CompressedHeader)) return COMPRESS_ERR_INVALID;
    
    CompressedHeader header;
    memcpy(&header, input, sizeof(CompressedHeader));
    
    if (header.magic != COMPRESS_MAGIC) return COMPRESS_ERR_INVALID;
    if (header.version != COMPRESS_VERSION) return COMPRESS_ERR_UNSUPPORTED;
    if (output_capacity < header.original_size) return COMPRESS_ERR_OVERFLOW;
    
    /* Decompress */
    const uint8_t *compressed_data = input + sizeof(CompressedHeader);
    size_t compressed_size = header.compressed_size;
    
    size_t decompressed_size = 0;
    
    switch (header.compression_type) {
        case COMPRESS_LZ4:
        case COMPRESS_ZSTD:
            decompressed_size = lz4_decompress_simple(compressed_data, compressed_size,
                                                       output, output_capacity);
            break;
        case COMPRESS_NONE:
        default:
            if (compressed_size > output_capacity) return COMPRESS_ERR_OVERFLOW;
            memcpy(output, compressed_data, compressed_size);
            decompressed_size = compressed_size;
            break;
    }
    
    if (decompressed_size == 0 || decompressed_size != header.original_size) {
        return COMPRESS_ERR_CORRUPT;
    }
    
    /* Verify checksum */
    uint32_t computed_crc = crc32_compute(output, decompressed_size);
    if (computed_crc != header.checksum) {
        return COMPRESS_ERR_CORRUPT;
    }
    
    *output_size = decompressed_size;
    
    /* Update stats */
    g_stats.decompress_calls++;
    
    return COMPRESS_OK;
}

bool is_compressed(const uint8_t *data, size_t size) {
    if (!data || size < sizeof(CompressedHeader)) return false;
    
    CompressedHeader header;
    memcpy(&header, data, sizeof(CompressedHeader));
    
    return header.magic == COMPRESS_MAGIC && header.version == COMPRESS_VERSION;
}

size_t compressed_original_size(const uint8_t *data, size_t size) {
    if (!is_compressed(data, size)) return 0;
    
    CompressedHeader header;
    memcpy(&header, data, sizeof(CompressedHeader));
    
    return header.original_size;
}

bool compressed_validate(const uint8_t *data, size_t size) {
    if (!is_compressed(data, size)) return false;
    
    CompressedHeader header;
    memcpy(&header, data, sizeof(CompressedHeader));
    
    /* Basic validation */
    if (sizeof(CompressedHeader) + header.compressed_size > size) return false;
    if (header.compression_type > COMPRESS_ZSTD) return false;
    
    return true;
}

/* ============================================================================
 * In-place Operations
 * ============================================================================ */

int compress_inplace(uint8_t **data, size_t size, size_t *capacity,
                     size_t *new_size, const CompressionConfig *config) {
    if (!data || !*data || !capacity || !new_size) return COMPRESS_ERR_NULL_PTR;
    
    /* Allocate output buffer */
    size_t bound = compress_bound(size, config ? config->type : COMPRESS_LZ4);
    uint8_t *output = malloc(bound);
    if (!output) return COMPRESS_ERR_ALLOC;
    
    size_t output_size;
    int err = compress_data(*data, size, output, bound, &output_size, config);
    
    if (err != COMPRESS_OK || output_size == 0) {
        free(output);
        *new_size = size;  /* Keep uncompressed */
        return err;
    }
    
    /* Replace data with compressed version */
    if (*capacity < output_size) {
        uint8_t *new_data = realloc(*data, output_size);
        if (!new_data) {
            free(output);
            return COMPRESS_ERR_ALLOC;
        }
        *data = new_data;
        *capacity = output_size;
    }
    
    memcpy(*data, output, output_size);
    *new_size = output_size;
    free(output);
    
    return COMPRESS_OK;
}

int decompress_inplace(uint8_t **data, size_t size, size_t *capacity,
                       size_t *new_size) {
    if (!data || !*data || !capacity || !new_size) return COMPRESS_ERR_NULL_PTR;
    
    if (!is_compressed(*data, size)) {
        *new_size = size;
        return COMPRESS_OK;
    }
    
    size_t original_size = compressed_original_size(*data, size);
    if (original_size == 0) return COMPRESS_ERR_CORRUPT;
    
    /* Allocate output buffer */
    uint8_t *output = malloc(original_size);
    if (!output) return COMPRESS_ERR_ALLOC;
    
    size_t output_size;
    int err = decompress_data(*data, size, output, original_size, &output_size);
    
    if (err != COMPRESS_OK) {
        free(output);
        return err;
    }
    
    /* Replace data with decompressed version */
    if (*capacity < output_size) {
        uint8_t *new_data = realloc(*data, output_size);
        if (!new_data) {
            free(output);
            return COMPRESS_ERR_ALLOC;
        }
        *data = new_data;
        *capacity = output_size;
    }
    
    memcpy(*data, output, output_size);
    *new_size = output_size;
    free(output);
    
    return COMPRESS_OK;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void compression_get_stats(CompressionStats *stats) {
    if (!stats) return;
    *stats = g_stats;
}

void compression_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

/* ============================================================================
 * Utility
 * ============================================================================ */

const char* compression_type_name(CompressionType type) {
    switch (type) {
        case COMPRESS_NONE: return "none";
        case COMPRESS_LZ4: return "lz4";
        case COMPRESS_ZSTD: return "zstd";
        default: return "unknown";
    }
}

