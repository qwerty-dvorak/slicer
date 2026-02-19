#ifndef EDITOR_PIXELS_H
#define EDITOR_PIXELS_H

#include <stdint.h>

#include "renderer.h"

/* ------------------------------------------------------------------ */
/* Pixel format packing / unpacking                                    */
/* ------------------------------------------------------------------ */

uint32_t
pack_pixel (const pixel_format_t *format, uint8_t r, uint8_t g, uint8_t b);
uint32_t load_pixel (const pixel_format_t *format, const uint8_t *src);
void store_pixel (const pixel_format_t *format, uint8_t *dst, uint32_t pixel);
void unpack_pixel (
    const pixel_format_t *format,
    uint32_t pixel,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b
);

/* ------------------------------------------------------------------ */
/* Alpha blending                                                      */
/* ------------------------------------------------------------------ */

void blend_pixel (
    const pixel_format_t *format,
    uint8_t *dst_px,
    uint8_t src_r,
    uint8_t src_g,
    uint8_t src_b,
    uint8_t alpha
);

#endif /* EDITOR_PIXELS_H */
