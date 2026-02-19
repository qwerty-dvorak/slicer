#ifndef EDITOR_EVENTS_H
#define EDITOR_EVENTS_H

#include <xcb/xcb.h>

#include "image.h"
#include "viewer.h"

/* ------------------------------------------------------------------ */
/* XCB event dispatch                                                  */
/* ------------------------------------------------------------------ */

int editor_handle_event (
    viewer_t *viewer,
    const image_t *img,
    const xcb_generic_event_t *event,
    int *request_redraw
);

#endif /* EDITOR_EVENTS_H */
