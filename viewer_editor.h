#ifndef VIEWER_EDITOR_H
#define VIEWER_EDITOR_H

#include <stdint.h>

#include <xcb/xcb.h>

#include "image.h"
#include "viewer.h"

void viewer_editor_reset_for_image (const image_t *img);

int viewer_editor_handle_event (
    viewer_t *viewer,
    const image_t *img,
    const xcb_generic_event_t *event,
    int *request_redraw
);

void viewer_editor_draw_overlay (
    const viewer_t *viewer,
    const image_t *img,
    uint8_t *draw_buf
);

void viewer_editor_draw_overlay_text (viewer_t *viewer);

#endif
