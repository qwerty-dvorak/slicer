#include "keybinds.h"

#define KEYCODE_MINUS 20
#define KEYCODE_EQUAL 21
#define KEYCODE_KP_SUBTRACT 82
#define KEYCODE_KP_ADD 86

#define KEYCODE_LEFT 113
#define KEYCODE_RIGHT 114
#define KEYCODE_UP 111
#define KEYCODE_DOWN 116

#define MOUSE_BUTTON_LEFT 1
#define MOUSE_WHEEL_UP 4
#define MOUSE_WHEEL_DOWN 5

#define ZOOM_STEP 1.1f
#define ZOOM_MIN 0.1f
#define ZOOM_MAX 32.0f

#define ARROW_PAN_STEP 32

static float
clamp_zoom (float zoom)
{
    if (zoom < ZOOM_MIN)
        {
            return ZOOM_MIN;
        }
    if (zoom > ZOOM_MAX)
        {
            return ZOOM_MAX;
        }
    return zoom;
}

void
keybinds_init (keybinds_state_t *state, view_params_t *view)
{
    if (!state || !view)
        {
            return;
        }

    state->mouse_pan_enabled = 0;
    state->dragging = 0;
    state->drag_last_x = 0;
    state->drag_last_y = 0;

    view->zoom = 1.0f;
    view->pan_x = 0;
    view->pan_y = 0;
}

void
keybinds_set_mouse_pan_enabled (keybinds_state_t *state, int enabled)
{
    if (!state)
        {
            return;
        }

    state->mouse_pan_enabled = enabled ? 1 : 0;
    if (!state->mouse_pan_enabled)
        {
            state->dragging = 0;
        }
}

void
keybinds_handle_event (
    keybinds_state_t *state,
    view_params_t *view,
    const xcb_generic_event_t *event,
    int *request_redraw
)
{
    uint8_t type;

    if (!state || !view || !event || !request_redraw)
        {
            return;
        }

    type = event->response_type & 0x7FU;
    switch (type)
        {
        case XCB_KEY_PRESS:
            {
                const xcb_key_press_event_t *key
                    = (const xcb_key_press_event_t *)event;
                if (key->detail == KEYCODE_EQUAL
                    || key->detail == KEYCODE_KP_ADD)
                    {
                        view->zoom = clamp_zoom (view->zoom * ZOOM_STEP);
                        *request_redraw = 1;
                    }
                else if (key->detail == KEYCODE_MINUS
                         || key->detail == KEYCODE_KP_SUBTRACT)
                    {
                        view->zoom = clamp_zoom (view->zoom / ZOOM_STEP);
                        *request_redraw = 1;
                    }
                else if (key->detail == KEYCODE_LEFT)
                    {
                        view->pan_x += ARROW_PAN_STEP;
                        *request_redraw = 1;
                    }
                else if (key->detail == KEYCODE_RIGHT)
                    {
                        view->pan_x -= ARROW_PAN_STEP;
                        *request_redraw = 1;
                    }
                else if (key->detail == KEYCODE_UP)
                    {
                        view->pan_y += ARROW_PAN_STEP;
                        *request_redraw = 1;
                    }
                else if (key->detail == KEYCODE_DOWN)
                    {
                        view->pan_y -= ARROW_PAN_STEP;
                        *request_redraw = 1;
                    }
                break;
            }
        case XCB_BUTTON_PRESS:
            {
                const xcb_button_press_event_t *btn
                    = (const xcb_button_press_event_t *)event;
                if (btn->detail == MOUSE_BUTTON_LEFT
                    && state->mouse_pan_enabled)
                    {
                        state->dragging = 1;
                        state->drag_last_x = btn->event_x;
                        state->drag_last_y = btn->event_y;
                    }
                else if (btn->detail == MOUSE_WHEEL_UP)
                    {
                        view->zoom = clamp_zoom (view->zoom * ZOOM_STEP);
                        *request_redraw = 1;
                    }
                else if (btn->detail == MOUSE_WHEEL_DOWN)
                    {
                        view->zoom = clamp_zoom (view->zoom / ZOOM_STEP);
                        *request_redraw = 1;
                    }
                break;
            }
        case XCB_BUTTON_RELEASE:
            {
                const xcb_button_release_event_t *btn
                    = (const xcb_button_release_event_t *)event;
                if (btn->detail == MOUSE_BUTTON_LEFT)
                    {
                        state->dragging = 0;
                    }
                break;
            }
        case XCB_MOTION_NOTIFY:
            {
                const xcb_motion_notify_event_t *motion
                    = (const xcb_motion_notify_event_t *)event;
                if (state->dragging && state->mouse_pan_enabled)
                    {
                        int dx = motion->event_x - state->drag_last_x;
                        int dy = motion->event_y - state->drag_last_y;

                        view->pan_x += dx;
                        view->pan_y += dy;

                        state->drag_last_x = motion->event_x;
                        state->drag_last_y = motion->event_y;
                        *request_redraw = 1;
                    }
                break;
            }
        default:
            break;
        }
}
