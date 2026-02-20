#include "editor_render.h"

#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>

#include "editor_coords.h"
#include "editor_draw.h"
#include "editor_logic.h"
#include "editor_pixels.h"
#include "editor_types.h"

static void
viewer_draw_text (
    viewer_t *viewer,
    int x,
    int y,
    const char *text,
    uint8_t r,
    uint8_t g,
    uint8_t b
)
{
    uint32_t fg = pack_pixel (&viewer->pixel_format, r, g, b);
    uint32_t values[1];
    size_t len = strlen (text);

    if (len > 255U)
        {
            len = 255U;
        }

    values[0] = fg;
    xcb_change_gc (viewer->conn, viewer->gc, XCB_GC_FOREGROUND, values);
    xcb_image_text_8 (
        viewer->conn,
        (uint8_t)len,
        viewer->window,
        viewer->gc,
        (int16_t)x,
        (int16_t)y,
        text
    );
}

void
editor_draw_sections (const viewer_t *viewer, const image_t *img, uint8_t *buf)
{
    int i;
    view_rect_t vr;

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
            return;
        }

    for (i = 0; i < g_editor.section_count; i++)
        {
            const section_t *s = &g_editor.sections[i];
            rect_i_t sr;
            uint8_t r = 70;
            uint8_t g = 140;
            uint8_t b = 150;
            uint8_t a = 95;

            sr.x = image_edge_to_screen_x (&vr, img, s->x);
            sr.y = image_edge_to_screen_y (&vr, img, s->y);
            sr.w = image_edge_to_screen_x (&vr, img, s->x + s->w) - sr.x;
            sr.h = image_edge_to_screen_y (&vr, img, s->y + s->h) - sr.y;
            if (sr.w <= 0 || sr.h <= 0)
                {
                    continue;
                }

            if (i == g_editor.selected_section)
                {
                    r = 45;
                    g = 230;
                    b = 230;
                    a = 185;
                    draw_rect_outline (viewer, buf, &sr, r, g, b, a);

                    sr.x += 1;
                    sr.y += 1;
                    sr.w -= 2;
                    sr.h -= 2;
                    if (sr.w > 0 && sr.h > 0)
                        {
                            draw_rect_outline (viewer, buf, &sr, r, g, b, 130);
                        }
                }
            else
                {
                    draw_rect_outline (viewer, buf, &sr, r, g, b, a);
                }
        }
}

void
editor_draw_cuts (const viewer_t *viewer, const image_t *img, uint8_t *buf)
{
    int i;
    view_rect_t vr;

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
            return;
        }

    for (i = 0; i < g_editor.cut_count; i++)
        {
            const cut_t *cut = &g_editor.cuts[i];
            int x1 = image_to_screen_x (&vr, img, cut->x1);
            int y1 = image_to_screen_y (&vr, img, cut->y1);
            int x2 = image_to_screen_x (&vr, img, cut->x2);
            int y2 = image_to_screen_y (&vr, img, cut->y2);
            uint8_t r = i == g_editor.selected_cut ? 255 : 242;
            uint8_t g = i == g_editor.selected_cut ? 220 : 80;
            uint8_t b = i == g_editor.selected_cut ? 70 : 60;
            uint8_t a = i == g_editor.selected_cut ? 255 : 215;

            draw_line_blended (viewer, buf, x1, y1, x2, y2, r, g, b, a);

            if (i == g_editor.selected_cut)
                {
                    rect_i_t h1 = { x1 - 3, y1 - 3, 7, 7 };
                    rect_i_t h2 = { x2, y2, 7, 7 };
                    fill_rect_blended (viewer, buf, &h1, 255, 255, 200, 240);
                    fill_rect_blended (viewer, buf, &h2, 255, 255, 200, 240);
                    draw_rect_outline (viewer, buf, &h1, 50, 20, 20, 255);
                    draw_rect_outline (viewer, buf, &h2, 50, 20, 20, 255);
                }
        }

    if (g_editor.preview_active)
        {
            int x1 = image_to_screen_x (&vr, img, g_editor.preview_x1);
            int y1 = image_to_screen_y (&vr, img, g_editor.preview_y1);
            int x2 = image_to_screen_x (&vr, img, g_editor.preview_x2);
            int y2 = image_to_screen_y (&vr, img, g_editor.preview_y2);
            draw_line_blended (
                viewer, buf, x1, y1, x2, y2, 110, 255, 130, 255
            );
        }
}

void
editor_draw_hud (const viewer_t *viewer, uint8_t *buf)
{
    hud_layout_t layout;
    rect_i_t button;

    if (!g_editor.hud_visible)
        {
            return;
        }

    hud_get_layout (viewer, &layout);
    fill_rect_blended (viewer, buf, &layout.bar, 15, 22, 30, 150);
    draw_rect_outline (viewer, buf, &layout.bar, 110, 130, 155, 180);

    button = layout.buttons[HUD_BTN_DRAW];
    fill_rect_blended (
        viewer,
        buf,
        &button,
        g_editor.tool == TOOL_DRAW ? 52 : 34,
        g_editor.tool == TOOL_DRAW ? 144 : 62,
        g_editor.tool == TOOL_DRAW ? 95 : 72,
        205
    );
    draw_rect_outline (viewer, buf, &button, 170, 220, 180, 240);

    button = layout.buttons[HUD_BTN_SELECT];
    fill_rect_blended (
        viewer,
        buf,
        &button,
        g_editor.tool == TOOL_SELECT ? 48 : 34,
        g_editor.tool == TOOL_SELECT ? 98 : 62,
        g_editor.tool == TOOL_SELECT ? 165 : 88,
        205
    );
    draw_rect_outline (viewer, buf, &button, 175, 190, 240, 240);

    button = layout.buttons[HUD_BTN_MOVE];
    fill_rect_blended (
        viewer,
        buf,
        &button,
        g_editor.tool == TOOL_MOVE ? 42 : 34,
        g_editor.tool == TOOL_MOVE ? 126 : 62,
        g_editor.tool == TOOL_MOVE ? 132 : 88,
        205
    );
    draw_rect_outline (viewer, buf, &button, 160, 230, 232, 240);

    button = layout.buttons[HUD_BTN_GRID];
    fill_rect_blended (viewer, buf, &button, 120, 90, 34, 210);
    draw_rect_outline (viewer, buf, &button, 245, 210, 120, 245);
}

void
editor_draw_hud_text (viewer_t *viewer)
{
    hud_layout_t layout;
    char summary[192];
    char grid_label[48];

    if (!g_editor.hud_visible)
        {
            viewer_draw_text (
                viewer, 12, 22, "HUD hidden (H or TAB)", 245, 245, 245
            );
            return;
        }

    hud_get_layout (viewer, &layout);
    viewer_draw_text (
        viewer,
        layout.buttons[HUD_BTN_DRAW].x + 12,
        layout.buttons[HUD_BTN_DRAW].y + 24,
        "Draw Cut (D)",
        240,
        250,
        240
    );
    viewer_draw_text (
        viewer,
        layout.buttons[HUD_BTN_SELECT].x + 12,
        layout.buttons[HUD_BTN_SELECT].y + 24,
        "Select (S)",
        240,
        240,
        250
    );
    viewer_draw_text (
        viewer,
        layout.buttons[HUD_BTN_MOVE].x + 12,
        layout.buttons[HUD_BTN_MOVE].y + 24,
        "Move/Pan (M)",
        230,
        248,
        248
    );
    snprintf (
        grid_label,
        sizeof (grid_label),
        "Grid %dx%d (G)",
        g_editor.grid_cols,
        g_editor.grid_rows
    );
    viewer_draw_text (
        viewer,
        layout.buttons[HUD_BTN_GRID].x + 12,
        layout.buttons[HUD_BTN_GRID].y + 24,
        grid_label,
        250,
        245,
        220
    );

    snprintf (
        summary,
        sizeof (summary),
        "Cuts:%d  Sections:%d  Ctrl+Arrows grid size  R rotate  E export  X "
        "delete",
        g_editor.cut_count,
        g_editor.section_count
    );
    viewer_draw_text (
        viewer, layout.bar.x + 12, layout.bar.y - 8, summary, 220, 230, 245
    );
}
