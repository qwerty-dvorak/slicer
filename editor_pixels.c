#include "editor_pixels.h"

#include <xcb/xcb.h>

uint32_t
pack_pixel (const pixel_format_t *format, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rp
        = ((uint32_t)((r * format->red_max + 127) / 255) << format->red_shift)
          & format->red_mask;
    uint32_t gp = ((uint32_t)((g * format->green_max + 127) / 255)
                   << format->green_shift)
                  & format->green_mask;
    uint32_t bp = ((uint32_t)((b * format->blue_max + 127) / 255)
                   << format->blue_shift)
                  & format->blue_mask;
    return rp | gp | bp;
}

uint32_t
load_pixel (const pixel_format_t *format, const uint8_t *src)
{
    if (format->bytes_per_pixel == 4)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16)
                           | ((uint32_t)src[2] << 8) | (uint32_t)src[3];
                }
            return ((uint32_t)src[3] << 24) | ((uint32_t)src[2] << 16)
                   | ((uint32_t)src[1] << 8) | (uint32_t)src[0];
        }

    if (format->bytes_per_pixel == 3)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    return ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8)
                           | (uint32_t)src[2];
                }
            return ((uint32_t)src[2] << 16) | ((uint32_t)src[1] << 8)
                   | (uint32_t)src[0];
        }

    if (format->bytes_per_pixel == 2)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    return ((uint32_t)src[0] << 8) | (uint32_t)src[1];
                }
            return ((uint32_t)src[1] << 8) | (uint32_t)src[0];
        }

    return src[0];
}

void
store_pixel (const pixel_format_t *format, uint8_t *dst, uint32_t pixel)
{
    if (format->bytes_per_pixel == 4)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    dst[0] = (uint8_t)((pixel >> 24) & 0xFFU);
                    dst[1] = (uint8_t)((pixel >> 16) & 0xFFU);
                    dst[2] = (uint8_t)((pixel >> 8) & 0xFFU);
                    dst[3] = (uint8_t)(pixel & 0xFFU);
                }
            else
                {
                    dst[0] = (uint8_t)(pixel & 0xFFU);
                    dst[1] = (uint8_t)((pixel >> 8) & 0xFFU);
                    dst[2] = (uint8_t)((pixel >> 16) & 0xFFU);
                    dst[3] = (uint8_t)((pixel >> 24) & 0xFFU);
                }
            return;
        }

    if (format->bytes_per_pixel == 3)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    dst[0] = (uint8_t)((pixel >> 16) & 0xFFU);
                    dst[1] = (uint8_t)((pixel >> 8) & 0xFFU);
                    dst[2] = (uint8_t)(pixel & 0xFFU);
                }
            else
                {
                    dst[0] = (uint8_t)(pixel & 0xFFU);
                    dst[1] = (uint8_t)((pixel >> 8) & 0xFFU);
                    dst[2] = (uint8_t)((pixel >> 16) & 0xFFU);
                }
            return;
        }

    if (format->bytes_per_pixel == 2)
        {
            if (format->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
                {
                    dst[0] = (uint8_t)((pixel >> 8) & 0xFFU);
                    dst[1] = (uint8_t)(pixel & 0xFFU);
                }
            else
                {
                    dst[0] = (uint8_t)(pixel & 0xFFU);
                    dst[1] = (uint8_t)((pixel >> 8) & 0xFFU);
                }
            return;
        }

    dst[0] = (uint8_t)(pixel & 0xFFU);
}

void
unpack_pixel (
    const pixel_format_t *format,
    uint32_t pixel,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b
)
{
    uint32_t rv = (pixel & format->red_mask) >> format->red_shift;
    uint32_t gv = (pixel & format->green_mask) >> format->green_shift;
    uint32_t bv = (pixel & format->blue_mask) >> format->blue_shift;

    *r = format->red_max > 0 ? (uint8_t)((rv * 255 + format->red_max / 2)
                                         / (uint32_t)format->red_max)
                             : 0;
    *g = format->green_max > 0 ? (uint8_t)((gv * 255 + format->green_max / 2)
                                           / (uint32_t)format->green_max)
                               : 0;
    *b = format->blue_max > 0 ? (uint8_t)((bv * 255 + format->blue_max / 2)
                                          / (uint32_t)format->blue_max)
                              : 0;
}

void
blend_pixel (
    const pixel_format_t *format,
    uint8_t *dst_px,
    uint8_t src_r,
    uint8_t src_g,
    uint8_t src_b,
    uint8_t alpha
)
{
    uint8_t dst_r;
    uint8_t dst_g;
    uint8_t dst_b;
    uint8_t out_r;
    uint8_t out_g;
    uint8_t out_b;
    uint32_t packed = load_pixel (format, dst_px);

    if (alpha == 255U)
        {
            store_pixel (
                format, dst_px, pack_pixel (format, src_r, src_g, src_b)
            );
            return;
        }

    unpack_pixel (format, packed, &dst_r, &dst_g, &dst_b);
    out_r = (uint8_t)(((int)src_r * (int)alpha
                       + (int)dst_r * (255 - (int)alpha) + 127)
                      / 255);
    out_g = (uint8_t)(((int)src_g * (int)alpha
                       + (int)dst_g * (255 - (int)alpha) + 127)
                      / 255);
    out_b = (uint8_t)(((int)src_b * (int)alpha
                       + (int)dst_b * (255 - (int)alpha) + 127)
                      / 255);
    store_pixel (format, dst_px, pack_pixel (format, out_r, out_g, out_b));
}
