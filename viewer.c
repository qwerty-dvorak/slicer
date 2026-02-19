#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>

#include "viewer.h"
#include "viewer_editor.h"

static int
shift_from_mask (uint32_t mask)
{
    int shift = 0;

    while ((mask & 1U) == 0U && shift < 32)
        {
            mask >>= 1U;
            shift++;
        }
    return shift;
}

static int
max_from_mask (uint32_t mask, int shift)
{
    return (int)(mask >> shift);
}

static const xcb_visualtype_t *
find_root_visual (xcb_screen_t *screen)
{
    xcb_depth_iterator_t depth_it
        = xcb_screen_allowed_depths_iterator (screen);
    for (; depth_it.rem; xcb_depth_next (&depth_it))
        {
            xcb_visualtype_iterator_t vis_it
                = xcb_depth_visuals_iterator (depth_it.data);
            for (; vis_it.rem; xcb_visualtype_next (&vis_it))
                {
                    if (vis_it.data->visual_id == screen->root_visual)
                        {
                            return vis_it.data;
                        }
                }
        }
    return NULL;
}

static int
bits_per_pixel_for_depth (const xcb_setup_t *setup, uint8_t depth)
{
    xcb_format_iterator_t fmt_it = xcb_setup_pixmap_formats_iterator (setup);
    for (; fmt_it.rem; xcb_format_next (&fmt_it))
        {
            if (fmt_it.data->depth == depth)
                {
                    return fmt_it.data->bits_per_pixel;
                }
        }
    return 0;
}

static xcb_atom_t
atom_intern (xcb_connection_t *conn, const char *name)
{
    xcb_intern_atom_cookie_t cookie
        = xcb_intern_atom (conn, 0, (uint16_t)strlen (name), name);
    xcb_intern_atom_reply_t *reply
        = xcb_intern_atom_reply (conn, cookie, NULL);
    xcb_atom_t atom = XCB_ATOM_NONE;

    if (reply)
        {
            atom = reply->atom;
            free (reply);
        }

    return atom;
}

static void
viewer_redraw (viewer_t *viewer, const image_t *img, const bg_config_t *bg)
{
    int stride;

    if (viewer->win_w <= 0 || viewer->win_h <= 0)
        {
            return;
        }
    if (!renderer_ensure_buffer (
            &viewer->draw_buf,
            &viewer->draw_buf_size,
            viewer->win_w,
            viewer->win_h,
            viewer->pixel_format.bytes_per_pixel
        ))
        {
            fprintf (stderr, "out of memory allocating draw buffer\n");
            return;
        }

    stride = viewer->win_w * viewer->pixel_format.bytes_per_pixel;
    renderer_draw_image (
        &viewer->pixel_format,
        img,
        viewer->win_w,
        viewer->win_h,
        viewer->draw_buf,
        bg,
        &viewer->view
    );

    viewer_editor_draw_overlay (viewer, img, viewer->draw_buf);
    xcb_put_image (
        viewer->conn,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        viewer->window,
        viewer->gc,
        (uint16_t)viewer->win_w,
        (uint16_t)viewer->win_h,
        0,
        0,
        0,
        viewer->pixel_format.root_depth,
        (uint32_t)((size_t)stride * (size_t)viewer->win_h),
        viewer->draw_buf
    );

    viewer_editor_draw_overlay_text (viewer);
    xcb_flush (viewer->conn);
}

int
viewer_init (viewer_t *viewer, int initial_w, int initial_h)
{
    const xcb_setup_t *setup;
    xcb_screen_iterator_t screen_it;
    const xcb_visualtype_t *visual;
    xcb_event_mask_t event_mask
        = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
          | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS
          | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
    uint32_t win_values[2];
    uint8_t root_depth;
    int bits_per_pixel;

    viewer->conn = xcb_connect (NULL, NULL);
    if (xcb_connection_has_error (viewer->conn))
        {
            fprintf (stderr, "failed to connect to X server\n");
            viewer_cleanup (viewer);
            return 0;
        }

    setup = xcb_get_setup (viewer->conn);
    screen_it = xcb_setup_roots_iterator (setup);
    if (!screen_it.rem)
        {
            fprintf (stderr, "no X screen\n");
            viewer_cleanup (viewer);
            return 0;
        }
    viewer->screen = screen_it.data;

    root_depth = viewer->screen->root_depth;
    bits_per_pixel = bits_per_pixel_for_depth (setup, root_depth);
    if (bits_per_pixel == 0 || (bits_per_pixel % 8) != 0)
        {
            fprintf (
                stderr,
                "unsupported root depth/bpp: depth=%u bpp=%d\n",
                root_depth,
                bits_per_pixel
            );
            viewer_cleanup (viewer);
            return 0;
        }

    visual = find_root_visual (viewer->screen);
    if (!visual)
        {
            fprintf (stderr, "failed to find root visual\n");
            viewer_cleanup (viewer);
            return 0;
        }

    viewer->pixel_format.root_depth = root_depth;
    viewer->pixel_format.bytes_per_pixel = bits_per_pixel / 8;
    viewer->pixel_format.image_byte_order = setup->image_byte_order;
    viewer->pixel_format.red_mask = visual->red_mask;
    viewer->pixel_format.green_mask = visual->green_mask;
    viewer->pixel_format.blue_mask = visual->blue_mask;
    viewer->pixel_format.red_shift
        = shift_from_mask (viewer->pixel_format.red_mask);
    viewer->pixel_format.green_shift
        = shift_from_mask (viewer->pixel_format.green_mask);
    viewer->pixel_format.blue_shift
        = shift_from_mask (viewer->pixel_format.blue_mask);
    viewer->pixel_format.red_max = max_from_mask (
        viewer->pixel_format.red_mask, viewer->pixel_format.red_shift
    );
    viewer->pixel_format.green_max = max_from_mask (
        viewer->pixel_format.green_mask, viewer->pixel_format.green_shift
    );
    viewer->pixel_format.blue_max = max_from_mask (
        viewer->pixel_format.blue_mask, viewer->pixel_format.blue_shift
    );

    viewer->win_w = initial_w > 0 ? initial_w : 1;
    viewer->win_h = initial_h > 0 ? initial_h : 1;
    viewer->window = xcb_generate_id (viewer->conn);
    win_values[0] = viewer->screen->black_pixel;
    win_values[1] = event_mask;
    xcb_create_window (
        viewer->conn,
        root_depth,
        viewer->window,
        viewer->screen->root,
        0,
        0,
        (uint16_t)viewer->win_w,
        (uint16_t)viewer->win_h,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        viewer->screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        win_values
    );

    viewer->wm_protocols = atom_intern (viewer->conn, "WM_PROTOCOLS");
    viewer->wm_delete_window = atom_intern (viewer->conn, "WM_DELETE_WINDOW");
    if (viewer->wm_protocols != XCB_ATOM_NONE
        && viewer->wm_delete_window != XCB_ATOM_NONE)
        {
            xcb_change_property (
                viewer->conn,
                XCB_PROP_MODE_REPLACE,
                viewer->window,
                viewer->wm_protocols,
                XCB_ATOM_ATOM,
                32,
                1,
                &viewer->wm_delete_window
            );
        }

    viewer->gc = xcb_generate_id (viewer->conn);
    xcb_create_gc (viewer->conn, viewer->gc, viewer->window, 0, NULL);
    xcb_map_window (viewer->conn, viewer->window);
    xcb_flush (viewer->conn);
    keybinds_init (&viewer->keybinds, &viewer->view);

    return 1;
}

int
viewer_run (viewer_t *viewer, const image_t *img, const bg_config_t *bg)
{
    int should_exit = 0;
    xcb_generic_event_t *pending = NULL;

    viewer_editor_reset_for_image (img);
    viewer_redraw (viewer, img, bg);
    while (!should_exit)
        {
            xcb_generic_event_t *event;
            xcb_generic_event_t *next;
            uint8_t type;
            int request_redraw = 0;

            if (pending)
                {
                    event = pending;
                    pending = NULL;
                }
            else
                {
                    event = xcb_wait_for_event (viewer->conn);
                }

            if (!event)
                {
                    return 0;
                }

            type = event->response_type & 0x7FU;

            if (type == XCB_MOTION_NOTIFY)
                {
                    while ((next = xcb_poll_for_event (viewer->conn)) != NULL)
                        {
                            if ((next->response_type & 0x7FU)
                                == XCB_MOTION_NOTIFY)
                                {
                                    free (event);
                                    event = next;
                                }
                            else
                                {
                                    pending = next;
                                    break;
                                }
                        }
                }

            switch (type)
                {
                case XCB_EXPOSE:
                    viewer_redraw (viewer, img, bg);
                    break;
                case XCB_CONFIGURE_NOTIFY:
                    {
                        xcb_configure_notify_event_t *cfg
                            = (xcb_configure_notify_event_t *)event;
                        if ((int)cfg->width != viewer->win_w
                            || (int)cfg->height != viewer->win_h)
                            {
                                viewer->win_w = cfg->width;
                                viewer->win_h = cfg->height;
                                viewer_redraw (viewer, img, bg);
                            }
                        break;
                    }
                case XCB_CLIENT_MESSAGE:
                    {
                        xcb_client_message_event_t *msg
                            = (xcb_client_message_event_t *)event;
                        if (msg->data.data32[0] == viewer->wm_delete_window)
                            {
                                should_exit = 1;
                            }
                        break;
                    }
                case XCB_KEY_PRESS:
                case XCB_BUTTON_PRESS:
                case XCB_BUTTON_RELEASE:
                case XCB_MOTION_NOTIFY:
                    viewer_editor_handle_event (
                        viewer, img, event, &request_redraw
                    );
                    break;
                default:
                    break;
                }
            if (request_redraw)
                {
                    viewer_redraw (viewer, img, bg);
                }
            free (event);
        }

    return 1;
}

void
viewer_cleanup (viewer_t *viewer)
{
    free (viewer->draw_buf);
    viewer->draw_buf = NULL;
    viewer->draw_buf_size = 0;

    if (viewer->conn && viewer->gc)
        {
            xcb_free_gc (viewer->conn, viewer->gc);
            viewer->gc = 0;
        }
    if (viewer->conn && viewer->window)
        {
            xcb_destroy_window (viewer->conn, viewer->window);
            viewer->window = 0;
        }
    if (viewer->conn)
        {
            xcb_disconnect (viewer->conn);
            viewer->conn = NULL;
        }
    viewer->screen = NULL;
}
