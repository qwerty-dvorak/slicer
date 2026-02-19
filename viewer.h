#ifndef VIEWER_H
#define VIEWER_H

#include <stddef.h>
#include <stdint.h>

#include <xcb/xcb.h>

#include "image.h"
#include "renderer.h"

typedef struct
{
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_gcontext_t gc;

    pixel_format_t pixel_format;

    int win_w;
    int win_h;
    uint8_t *draw_buf;
    size_t draw_buf_size;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
} viewer_t;

int viewer_init (viewer_t *viewer, int initial_w, int initial_h);
int viewer_run (viewer_t *viewer, const image_t *img, const bg_config_t *bg);
void viewer_cleanup (viewer_t *viewer);

#endif
