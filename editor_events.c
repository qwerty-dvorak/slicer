#include "editor_events.h"

#include "editor_coords.h"
#include "editor_logic.h"
#include "editor_types.h"
#include "keybinds.h"
#include "viewer.h"
#include <stdlib.h>

#define MOUSE_BUTTON_LEFT 1

static void
editor_set_tool (viewer_t *viewer, editor_tool_t tool)
{
    g_editor.tool = tool;
    g_editor.drag_mode = DRAG_NONE;
    g_editor.preview_active = 0;
    keybinds_set_mouse_pan_enabled (&viewer->keybinds, tool == TOOL_MOVE);
}

static int
editor_handle_key_press (
    viewer_t *viewer,
    const image_t *img,
    const xcb_key_press_event_t *key,
    int *request_redraw
)
{
    if ((key->state & XCB_MOD_MASK_CONTROL) != 0U)
        {
            if (key->detail == KEYCODE_LEFT)
                {
                    if (editor_adjust_grid_size (-1, 0))
                        {
                            *request_redraw = 1;
                        }
                    return 1;
                }
            if (key->detail == KEYCODE_RIGHT)
                {
                    if (editor_adjust_grid_size (1, 0))
                        {
                            *request_redraw = 1;
                        }
                    return 1;
                }
            if (key->detail == KEYCODE_UP)
                {
                    if (editor_adjust_grid_size (0, 1))
                        {
                            *request_redraw = 1;
                        }
                    return 1;
                }
            if (key->detail == KEYCODE_DOWN)
                {
                    if (editor_adjust_grid_size (0, -1))
                        {
                            *request_redraw = 1;
                        }
                    return 1;
                }
        }

    switch (key->detail)
        {
        case KEYCODE_H:
        case KEYCODE_TAB:
            g_editor.hud_visible = !g_editor.hud_visible;
            *request_redraw = 1;
            return 1;
        case KEYCODE_D:
            editor_set_tool (viewer, TOOL_DRAW);
            *request_redraw = 1;
            return 1;
        case KEYCODE_S:
            editor_set_tool (viewer, TOOL_SELECT);
            *request_redraw = 1;
            return 1;
        case KEYCODE_M:
            editor_set_tool (viewer, TOOL_MOVE);
            *request_redraw = 1;
            return 1;
        case KEYCODE_G:
            if (editor_apply_grid_to_selected_section (img))
                {
                    *request_redraw = 1;
                }
            return 1;
        case KEYCODE_E:
            editor_export_sections_stdout ();
            return 1;
        case KEYCODE_R:
            editor_rotate_selected_cut (img);
            *request_redraw = 1;
            return 1;
        case KEYCODE_X:
        case KEYCODE_BACKSPACE:
        case KEYCODE_DELETE:
            editor_delete_selected_cut (img);
            *request_redraw = 1;
            return 1;
        case KEYCODE_ESC:
            g_editor.drag_mode = DRAG_NONE;
            g_editor.preview_active = 0;
            *request_redraw = 1;
            return 1;
        default:
            break;
        }
    return 0;
}

static int
editor_handle_button_press (
    viewer_t *viewer,
    const image_t *img,
    const xcb_button_press_event_t *btn,
    int *request_redraw
)
{
    int hud_btn;
    int ix = 0;
    int iy = 0;
    int inside = 0;

    if (btn->detail != MOUSE_BUTTON_LEFT)
        {
            return 0;
        }

    hud_btn = hud_button_hit (viewer, btn->event_x, btn->event_y);
    if (hud_btn >= 0)
        {
            if (hud_btn == HUD_BTN_DRAW)
                {
                    editor_set_tool (viewer, TOOL_DRAW);
                }
            else if (hud_btn == HUD_BTN_SELECT)
                {
                    editor_set_tool (viewer, TOOL_SELECT);
                }
            else if (hud_btn == HUD_BTN_MOVE)
                {
                    editor_set_tool (viewer, TOOL_MOVE);
                }
            else if (hud_btn == HUD_BTN_GRID)
                {
                    editor_apply_grid_to_selected_section (img);
                }

            *request_redraw = 1;
            return 1;
        }

    if (!screen_to_image (
            viewer, img, btn->event_x, btn->event_y, &ix, &iy, &inside
        ))
        {
            return 0;
        }

    if (!inside)
        {
            return 0;
        }

    g_editor.selected_section = editor_find_section_at (ix, iy);
    if (g_editor.selected_section < 0 && g_editor.section_count > 0)
        {
            g_editor.selected_section = 0;
        }

    if (g_editor.tool == TOOL_DRAW)
        {
            g_editor.drag_mode = DRAG_DRAW_NEW;
            g_editor.preview_active = 1;
            g_editor.preview_x1 = ix;
            g_editor.preview_y1 = iy;
            g_editor.preview_x2 = ix;
            g_editor.preview_y2 = iy;
            *request_redraw = 1;
            return 1;
        }

    if (g_editor.tool == TOOL_SELECT)
        {
            int cut_idx = editor_find_cut_at_screen (
                viewer, img, btn->event_x, btn->event_y
            );
            if (cut_idx >= 0)
                {
                    int endpoint_hit;
                    g_editor.selected_cut = cut_idx;
                    endpoint_hit = editor_endpoint_hit (
                        viewer,
                        img,
                        &g_editor.cuts[cut_idx],
                        btn->event_x,
                        btn->event_y
                    );

                    if (endpoint_hit == 1)
                        {
                            g_editor.drag_mode = DRAG_RESIZE_A;
                        }
                    else if (endpoint_hit == 2)
                        {
                            g_editor.drag_mode = DRAG_RESIZE_B;
                        }
                    else
                        {
                            g_editor.drag_mode = DRAG_MOVE_CUT;
                            g_editor.drag_last_img_x = ix;
                            g_editor.drag_last_img_y = iy;
                        }
                }
            else
                {
                    g_editor.selected_cut = -1;
                    g_editor.drag_mode = DRAG_NONE;
                }

            *request_redraw = 1;
            return 1;
        }

    return 0;
}

static int
editor_handle_motion (
    const viewer_t *viewer,
    const image_t *img,
    const xcb_motion_notify_event_t *motion,
    int *request_redraw
)
{
    int ix = 0;
    int iy = 0;

    if (g_editor.drag_mode == DRAG_NONE)
        {
            return 0;
        }
    if (!screen_to_image (
            viewer, img, motion->event_x, motion->event_y, &ix, &iy, NULL
        ))
        {
            return 0;
        }

    if (g_editor.drag_mode == DRAG_DRAW_NEW)
        {
            g_editor.preview_x2 = ix;
            g_editor.preview_y2 = iy;
            g_editor.preview_active = 1;
            *request_redraw = 1;
            return 1;
        }

    if (g_editor.selected_cut < 0
        || g_editor.selected_cut >= g_editor.cut_count)
        {
            g_editor.drag_mode = DRAG_NONE;
            return 0;
        }

    if (g_editor.drag_mode == DRAG_MOVE_CUT)
        {
            int dx = ix - g_editor.drag_last_img_x;
            int dy = iy - g_editor.drag_last_img_y;
            cut_t *cut = &g_editor.cuts[g_editor.selected_cut];

            if (dx != 0 || dy != 0)
                {
                    cut_t original = *cut;

                    editor_translate_cut_clamped (cut, dx, dy, img);
                    if (editor_refit_cut_to_closed_region (
                            g_editor.selected_cut, img
                        ))
                        {
                            editor_recompute_sections (img);
                            *request_redraw = 1;
                        }
                    else
                        {
                            *cut = original;
                        }

                    g_editor.drag_last_img_x = ix;
                    g_editor.drag_last_img_y = iy;
                }
            return 1;
        }

    if (g_editor.drag_mode == DRAG_RESIZE_A
        || g_editor.drag_mode == DRAG_RESIZE_B)
        {
            cut_t *cut = &g_editor.cuts[g_editor.selected_cut];
            cut_t original = *cut;

            if (cut_is_vertical (cut))
                {
                    if (g_editor.drag_mode == DRAG_RESIZE_A)
                        {
                            cut->y1 = clamp_int (iy, 0, img->height - 1);
                        }
                    else
                        {
                            cut->y2 = clamp_int (iy, 0, img->height - 1);
                        }
                }
            else
                {
                    if (g_editor.drag_mode == DRAG_RESIZE_A)
                        {
                            cut->x1 = clamp_int (ix, 0, img->width - 1);
                        }
                    else
                        {
                            cut->x2 = clamp_int (ix, 0, img->width - 1);
                        }
                }

            editor_normalize_cut (cut);
            if (editor_refit_cut_to_closed_region (g_editor.selected_cut, img))
                {
                    editor_recompute_sections (img);
                    *request_redraw = 1;
                }
            else
                {
                    *cut = original;
                }
            return 1;
        }

    return 0;
}

static int
editor_handle_button_release (
    const viewer_t *viewer,
    const image_t *img,
    const xcb_button_release_event_t *btn,
    int *request_redraw
)
{
    int consumed = 0;

    if (btn->detail != MOUSE_BUTTON_LEFT)
        {
            return 0;
        }

    if (g_editor.drag_mode == DRAG_DRAW_NEW && g_editor.preview_active)
        {
            int dx = abs (g_editor.preview_x2 - g_editor.preview_x1);
            int dy = abs (g_editor.preview_y2 - g_editor.preview_y1);
            int sec_idx = editor_find_section_at (
                g_editor.preview_x1, g_editor.preview_y1
            );

            if (sec_idx >= 0 && sec_idx < g_editor.section_count
                && (dx > 0 || dy > 0))
                {
                    section_t s = g_editor.sections[sec_idx];
                    cut_t cut;

                    if (dy > dx)
                        {
                            int x = clamp_int (
                                g_editor.preview_x2, s.x + 1, s.x + s.w - 1
                            );
                            if (x > s.x && x < s.x + s.w)
                                {
                                    cut.x1 = x;
                                    cut.y1 = s.y;
                                    cut.x2 = x;
                                    cut.y2 = s.y + s.h - 1;
                                    editor_add_cut (cut, img);
                                }
                        }
                    else
                        {
                            int y = clamp_int (
                                g_editor.preview_y2, s.y + 1, s.y + s.h - 1
                            );
                            if (y > s.y && y < s.y + s.h)
                                {
                                    cut.x1 = s.x;
                                    cut.y1 = y;
                                    cut.x2 = s.x + s.w - 1;
                                    cut.y2 = y;
                                    editor_add_cut (cut, img);
                                }
                        }
                }

            g_editor.preview_active = 0;
            g_editor.drag_mode = DRAG_NONE;
            consumed = 1;
            *request_redraw = 1;
        }
    else if (g_editor.drag_mode == DRAG_MOVE_CUT
             || g_editor.drag_mode == DRAG_RESIZE_A
             || g_editor.drag_mode == DRAG_RESIZE_B)
        {
            g_editor.drag_mode = DRAG_NONE;
            consumed = 1;
            *request_redraw = 1;
        }

    (void)viewer;
    return consumed;
}

int
editor_handle_event (
    viewer_t *viewer,
    const image_t *img,
    const xcb_generic_event_t *event,
    int *request_redraw
)
{
    uint8_t type = event->response_type & 0x7FU;
    int consumed = 0;

    switch (type)
        {
        case XCB_KEY_PRESS:
            consumed = editor_handle_key_press (
                viewer,
                img,
                (const xcb_key_press_event_t *)event,
                request_redraw
            );
            break;
        case XCB_BUTTON_PRESS:
            consumed = editor_handle_button_press (
                viewer,
                img,
                (const xcb_button_press_event_t *)event,
                request_redraw
            );
            break;
        case XCB_BUTTON_RELEASE:
            consumed = editor_handle_button_release (
                viewer,
                img,
                (const xcb_button_release_event_t *)event,
                request_redraw
            );
            break;
        case XCB_MOTION_NOTIFY:
            consumed = editor_handle_motion (
                viewer,
                img,
                (const xcb_motion_notify_event_t *)event,
                request_redraw
            );
            break;
        default:
            break;
        }

    /* Fall through to viewer-level keybinds (zoom/pan) for any event
       the editor did not fully consume. */
    if (!consumed)
        keybinds_handle_event (
            &viewer->keybinds, &viewer->view, event, request_redraw
        );

    return consumed;
}
