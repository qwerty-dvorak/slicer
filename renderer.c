#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <xcb/xcb.h>

#include "renderer.h"

static uint32_t
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

static void
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

static void
sample_checkered (int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = *g = *b
        = ((((x >> 4) + (y >> 4)) & 1) != 0) ? (uint8_t)120U : (uint8_t)200U;
}

static void
sample_background (
    const bg_config_t *bg,
    int x,
    int y,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b
)
{
    if (bg->mode == BG_MODE_SOLID)
        {
            *r = bg->solid_r;
            *g = bg->solid_g;
            *b = bg->solid_b;
            return;
        }

    sample_checkered (x, y, r, g, b);
}

static void
compute_view_rect (
    int img_w,
    int img_h,
    int win_w,
    int win_h,
    const view_params_t *view,
    int *out_w,
    int *out_h,
    int *out_x,
    int *out_y
)
{
    double fit_scale;
    double zoom;
    double scale;
    long long scaled_w;
    long long scaled_h;
    int pan_x = 0;
    int pan_y = 0;

    if (img_w <= 0 || img_h <= 0 || win_w <= 0 || win_h <= 0)
        {
            *out_w = 0;
            *out_h = 0;
            *out_x = 0;
            *out_y = 0;
            return;
        }

    fit_scale = (double)win_w / (double)img_w;
    if ((double)win_h / (double)img_h < fit_scale)
        {
            fit_scale = (double)win_h / (double)img_h;
        }
    zoom = (view && view->zoom > 0.0f) ? (double)view->zoom : 1.0;
    scale = fit_scale * zoom;

    scaled_w = (long long)((double)img_w * scale + 0.5);
    scaled_h = (long long)((double)img_h * scale + 0.5);

    if (view)
        {
            pan_x = view->pan_x;
            pan_y = view->pan_y;
        }

    if (scaled_w < 1)
        {
            scaled_w = 1;
        }
    if (scaled_h < 1)
        {
            scaled_h = 1;
        }

    *out_w = (int)scaled_w;
    *out_h = (int)scaled_h;
    *out_x = (win_w - *out_w) / 2 + pan_x;
    *out_y = (win_h - *out_h) / 2 + pan_y;
}

static void
fill_background (
    const pixel_format_t *format,
    int win_w,
    int win_h,
    uint8_t *dst,
    const bg_config_t *bg
)
{
    int x;
    int y;
    size_t stride = (size_t)win_w * (size_t)format->bytes_per_pixel;

    for (y = 0; y < win_h; y++)
        {
            uint8_t *row = dst + (size_t)y * stride;
            for (x = 0; x < win_w; x++)
                {
                    uint8_t br;
                    uint8_t bgc;
                    uint8_t bb;
                    uint32_t pixel;

                    sample_background (bg, x, y, &br, &bgc, &bb);
                    pixel = pack_pixel (format, br, bgc, bb);
                    store_pixel (
                        format,
                        row + (size_t)x * (size_t)format->bytes_per_pixel,
                        pixel
                    );
                }
        }
}

int
renderer_ensure_buffer (
    uint8_t **buffer,
    size_t *buffer_size,
    int width,
    int height,
    int bytes_per_pixel
)
{
    size_t pixel_count;
    size_t need;
    uint8_t *new_buffer;

    if (width <= 0 || height <= 0 || bytes_per_pixel <= 0)
        {
            return 0;
        }
    if ((size_t)width > SIZE_MAX / (size_t)height)
        {
            return 0;
        }
    pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / (size_t)bytes_per_pixel)
        {
            return 0;
        }

    need = pixel_count * (size_t)bytes_per_pixel;
    if (need <= *buffer_size)
        {
            return 1;
        }

    new_buffer = (uint8_t *)realloc (*buffer, need);
    if (!new_buffer)
        {
            return 0;
        }
    *buffer = new_buffer;
    *buffer_size = need;
    return 1;
}

void
renderer_draw_image (
    const pixel_format_t *format,
    const image_t *img,
    int win_w,
    int win_h,
    uint8_t *dst,
    const bg_config_t *bg,
    const view_params_t *view
)
{
    int draw_w;
    int draw_h;
    int offset_x;
    int offset_y;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    int x;
    int y;
    size_t stride;

    if (!format || !img || !img->rgba || !dst || !bg || win_w <= 0
        || win_h <= 0)
        {
            return;
        }

    fill_background (format, win_w, win_h, dst, bg);
    compute_view_rect (
        img->width,
        img->height,
        win_w,
        win_h,
        view,
        &draw_w,
        &draw_h,
        &offset_x,
        &offset_y
    );
    if (draw_w <= 0 || draw_h <= 0)
        {
            return;
        }

    start_x = offset_x > 0 ? offset_x : 0;
    start_y = offset_y > 0 ? offset_y : 0;
    end_x = offset_x + draw_w;
    end_y = offset_y + draw_h;
    if (end_x > win_w)
        {
            end_x = win_w;
        }
    if (end_y > win_h)
        {
            end_y = win_h;
        }
    if (start_x >= end_x || start_y >= end_y)
        {
            return;
        }

    stride = (size_t)win_w * (size_t)format->bytes_per_pixel;
    for (y = start_y; y < end_y; y++)
        {
            int src_y = ((y - offset_y) * img->height) / draw_h;
            uint8_t *row = dst + (size_t)y * stride
                           + (size_t)start_x
                                 * (size_t)format->bytes_per_pixel;

            for (x = start_x; x < end_x; x++)
                {
                    int src_x = ((x - offset_x) * img->width) / draw_w;
                    size_t src_idx
                        = ((size_t)src_y * (size_t)img->width + (size_t)src_x)
                          * 4U;
                    uint8_t r = img->rgba[src_idx + 0U];
                    uint8_t g = img->rgba[src_idx + 1U];
                    uint8_t b = img->rgba[src_idx + 2U];
                    uint8_t a = img->rgba[src_idx + 3U];
                    uint32_t pixel;

                    if (!img->has_alpha || a == 255U)
                        {
                            pixel = pack_pixel (format, r, g, b);
                        }
                    else
                        {
                            uint8_t br;
                            uint8_t bgc;
                            uint8_t bb;
                            uint8_t out_r;
                            uint8_t out_g;
                            uint8_t out_b;

                            sample_background (bg, x, y, &br, &bgc, &bb);
                            out_r
                                = (uint8_t)(((int)r * (int)a
                                             + (int)br * (255 - (int)a) + 127)
                                            / 255);
                            out_g
                                = (uint8_t)(((int)g * (int)a
                                             + (int)bgc * (255 - (int)a) + 127)
                                            / 255);
                            out_b
                                = (uint8_t)(((int)b * (int)a
                                             + (int)bb * (255 - (int)a) + 127)
                                            / 255);
                            pixel = pack_pixel (format, out_r, out_g, out_b);
                        }

                    store_pixel (
                        format,
                        row + (size_t)(x - start_x)
                                  * (size_t)format->bytes_per_pixel,
                        pixel
                    );
                }
        }
}
