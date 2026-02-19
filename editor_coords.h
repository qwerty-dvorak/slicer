#ifndef EDITOR_COORDS_H
#define EDITOR_COORDS_H

#include "editor_types.h"
#include "image.h"
#include "viewer.h"

/* ------------------------------------------------------------------ */
/* General math helpers                                                */
/* ------------------------------------------------------------------ */

int clamp_int (int v, int lo, int hi);
int point_in_rect (int x, int y, const rect_i_t *r);
int point_in_section (int x, int y, const section_t *s);

/* ------------------------------------------------------------------ */
/* View rectangle                                                      */
/* ------------------------------------------------------------------ */

void compute_view_rect (
    int img_w,
    int img_h,
    int win_w,
    int win_h,
    const view_params_t *view,
    view_rect_t *out
);

/* ------------------------------------------------------------------ */
/* Coordinate conversion                                               */
/* ------------------------------------------------------------------ */

int screen_to_image (
    const viewer_t *viewer,
    const image_t *img,
    int sx,
    int sy,
    int *ix,
    int *iy,
    int *inside
);

int image_to_screen_x (const view_rect_t *vr, const image_t *img, int ix);
int image_to_screen_y (const view_rect_t *vr, const image_t *img, int iy);
int image_edge_to_screen_x (
    const view_rect_t *vr,
    const image_t *img,
    int ix_edge
);
int image_edge_to_screen_y (
    const view_rect_t *vr,
    const image_t *img,
    int iy_edge
);

#endif /* EDITOR_COORDS_H */
