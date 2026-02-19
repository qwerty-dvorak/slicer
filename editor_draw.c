#include "editor_draw.h"
#include "editor_pixels.h"
#include <stdlib.h>

static void
plot_blended (
    const viewer_t *viewer,
    uint8_t *buf,
    int x,
    int y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t alpha
)
{
    size_t stride;
    uint8_t *dst_px;

    if (x < 0 || y < 0 || x >= viewer->win_w || y >= viewer->win_h)
        {
            return;
        }

    stride
        = (size_t)viewer->win_w * (size_t)viewer->pixel_format.bytes_per_pixel;
    dst_px = buf + (size_t)y * stride
             + (size_t)x * (size_t)viewer->pixel_format.bytes_per_pixel;
    blend_pixel (&viewer->pixel_format, dst_px, r, g, b, alpha);
}

void
fill_rect_blended (
    const viewer_t *viewer,
    uint8_t *buf,
    const rect_i_t *r,
    uint8_t cr,
    uint8_t cg,
    uint8_t cb,
    uint8_t alpha
)
{
    int x0 = r->x < 0 ? 0 : r->x;
    int y0 = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w;
    int y1 = r->y + r->h;
    int x;
    int y;

    if (x1 > viewer->win_w)
        {
            x1 = viewer->win_w;
        }
    if (y1 > viewer->win_h)
        {
            y1 = viewer->win_h;
        }

    if (x0 >= x1 || y0 >= y1)
        {
            return;
        }

    for (y = y0; y < y1; y++)
        {
            for (x = x0; x < x1; x++)
                {
                    plot_blended (viewer, buf, x, y, cr, cg, cb, alpha);
                }
        }
}

void
draw_line_blended (
    const viewer_t *viewer,
    uint8_t *buf,
    int x0,
    int y0,
    int x1,
    int y1,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t alpha
)
{
    int dx = abs (x1 - x0);
    int dy = abs (y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1)
        {
            plot_blended (viewer, buf, x0, y0, r, g, b, alpha);
            if (x0 == x1 && y0 == y1)
                {
                    break;
                }

            if (2 * err > -dy)
                {
                    err -= dy;
                    x0 += sx;
                }
            if (2 * err < dx)
                {
                    err += dx;
                    y0 += sy;
                }
        }
}

void
draw_rect_outline (
    const viewer_t *viewer,
    uint8_t *buf,
    const rect_i_t *r,
    uint8_t cr,
    uint8_t cg,
    uint8_t cb,
    uint8_t alpha
)
{
    int x0 = r->x;
    int y0 = r->y;
    int x1 = r->x + r->w - 1;
    int y1 = r->y + r->h - 1;

    if (r->w <= 0 || r->h <= 0)
        {
            return;
        }

    draw_line_blended (viewer, buf, x0, y0, x1, y0, cr, cg, cb, alpha);
    draw_line_blended (viewer, buf, x1, y0, x1, y1, cr, cg, cb, alpha);
    draw_line_blended (viewer, buf, x1, y1, x0, y1, cr, cg, cb, alpha);
    draw_line_blended (viewer, buf, x0, y1, x0, y0, cr, cg, cb, alpha);
}
