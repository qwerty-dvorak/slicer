#ifndef PNG_DECODER_INTERNAL_H
#define PNG_DECODER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* PNG signature                                                       */
/* ------------------------------------------------------------------ */

extern const uint8_t g_png_sig[8];

/* ------------------------------------------------------------------ */
/* Chunk type four-byte codes (big-endian)                            */
/* ------------------------------------------------------------------ */

#define PNG_CHUNK_IHDR 0x49484452U /* "IHDR" */
#define PNG_CHUNK_IDAT 0x49444154U /* "IDAT" */
#define PNG_CHUNK_IEND 0x49454e44U /* "IEND" */
#define PNG_CHUNK_tRNS 0x74524e53U /* "tRNS" */

/* ------------------------------------------------------------------ */
/* Big-endian 32-bit read                                             */
/* ------------------------------------------------------------------ */

static inline uint32_t
png_read_be32 (const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ------------------------------------------------------------------ */
/* Parsed IHDR fields                                                  */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t compression;
    uint8_t filter_method;
    uint8_t interlace;
} png_ihdr_t;

/* ------------------------------------------------------------------ */
/* Parsed tRNS fields (RGB colour type only)                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    int present;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} png_trns_t;

/* ------------------------------------------------------------------ */
/* I/O helpers (png_decoder_io.c)                                     */
/* ------------------------------------------------------------------ */

int png_load_file_bytes (const char *path, uint8_t **out, size_t *out_size);
int png_append_bytes (
    uint8_t **dst,
    size_t *dst_size,
    size_t *dst_cap,
    const uint8_t *src,
    size_t src_size
);

/* ------------------------------------------------------------------ */
/* Inflate helper (png_decoder_inflate.c)                             */
/* ------------------------------------------------------------------ */

int png_inflate_idat_fast (
    uint8_t *dst,
    size_t dst_size,
    const uint8_t *idat,
    size_t idat_size
);

/* ------------------------------------------------------------------ */
/* Pixel pipeline (png_decoder_pixels.c)                              */
/* ------------------------------------------------------------------ */

int png_decode_raw_to_rgba (
    uint8_t *rgba,
    const uint8_t *raw,
    uint32_t width,
    uint32_t height,
    size_t src_channels,
    int has_trns,
    uint8_t tr,
    uint8_t tg,
    uint8_t tb
);

#endif /* PNG_DECODER_INTERNAL_H */
