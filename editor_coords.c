#include "editor_coords.h"

int
clamp_int (int v, int lo, int hi)
{
    if (v < lo)
        {
            return lo;
        }
    if (v > hi)
        {
            return hi;
        }
    return v;
}

int
point_in_rect (int x, int y, const rect_i_t *r)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int
point_in_section (int x, int y, const section_t *s)
{
    return x >= s->x && y >= s->y && x < s->x + s->w && y < s->y + s->h;
}

void
compute_view_rect (
    int img_w,
    int img_h,
    int win_w,
    int win_h,
    const view_params_t *view,
    view_rect_t *out
)
{
    double fit_scale;
    double zoom;
    double scale;
    long long scaled_w;
    long long scaled_h;
    int pan_x = 0;
    int pan_y = 0;

    out->draw_w = 0;
    out->draw_h = 0;
    out->off_x = 0;
    out->off_y = 0;

    if (img_w <= 0 || img_h <= 0 || win_w <= 0 || win_h <= 0)
        {
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

    out->draw_w = (int)scaled_w;
    out->draw_h = (int)scaled_h;
    out->off_x = (win_w - out->draw_w) / 2 + pan_x;
    out->off_y = (win_h - out->draw_h) / 2 + pan_y;
}

int
screen_to_image (
    const viewer_t *viewer,
    const image_t *img,
    int sx,
    int sy,
    int *ix,
    int *iy,
    int *inside
)
{
    view_rect_t vr;
    long long lx;
    long long ly;

    compute_view_rect (
        img->width,
        img->height,
        viewer->win_w,
        viewer->win_h,
        &viewer->view,
        &vr
    );
    if (vr.draw_w <= 0 || vr.draw_h <= 0)
        {
            return 0;
        }

    lx = (long long)sx - (long long)vr.off_x;
    ly = (long long)sy - (long long)vr.off_y;

    if (inside)
        {
            *inside = lx >= 0 && ly >= 0 && lx < vr.draw_w && ly < vr.draw_h;
        }

    if (ix)
        {
            long long v = lx * (long long)img->width;
            int mapped = (int)(v / (long long)vr.draw_w);
            *ix = clamp_int (mapped, 0, img->width - 1);
        }
    if (iy)
        {
            long long v = ly * (long long)img->height;
            int mapped = (int)(v / (long long)vr.draw_h);
            *iy = clamp_int (mapped, 0, img->height - 1);
        }

    return 1;
}

int
image_to_screen_x (const view_rect_t *vr, const image_t *img, int ix)
{
    return vr->off_x + (ix * vr->draw_w) / img->width;
}

int
image_to_screen_y (const view_rect_t *vr, const image_t *img, int iy)
{
    return vr->off_y + (iy * vr->draw_h) / img->height;
}

int
image_edge_to_screen_x (const view_rect_t *vr, const image_t *img, int ix_edge)
{
    return vr->off_x + (ix_edge * vr->draw_w) / img->width;
}

int
image_edge_to_screen_y (const view_rect_t *vr, const image_t *img, int iy_edge)
{
    return vr->off_y + (iy_edge * vr->draw_h) / img->height;
}
