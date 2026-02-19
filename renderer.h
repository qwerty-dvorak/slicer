#ifndef RENDERER_H
#define RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

typedef enum
{
    BG_MODE_CHECKERED = 0,
    BG_MODE_SOLID = 1
} bg_mode_t;

typedef struct
{
    bg_mode_t mode;
    uint8_t solid_r;
    uint8_t solid_g;
    uint8_t solid_b;
} bg_config_t;

typedef struct
{
    uint8_t root_depth;
    int bytes_per_pixel;
    int image_byte_order;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    int red_shift;
    int green_shift;
    int blue_shift;
    int red_max;
    int green_max;
    int blue_max;
} pixel_format_t;

int renderer_ensure_buffer (
    uint8_t **buffer,
    size_t *buffer_size,
    int width,
    int height,
    int bytes_per_pixel
);

void renderer_draw_image (
    const pixel_format_t *format,
    const image_t *img,
    int win_w,
    int win_h,
    uint8_t *dst,
    const bg_config_t *bg
);

#endif
