#ifndef KEYBINDS_H
#define KEYBINDS_H

#include <xcb/xcb.h>

#include "renderer.h"

typedef struct
{
    int mouse_pan_enabled;
    int dragging;
    int drag_last_x;
    int drag_last_y;
} keybinds_state_t;

void keybinds_init (keybinds_state_t *state, view_params_t *view);
void keybinds_set_mouse_pan_enabled (keybinds_state_t *state, int enabled);

void keybinds_handle_event (
    keybinds_state_t *state,
    view_params_t *view,
    const xcb_generic_event_t *event,
    int *request_redraw
);

#endif
