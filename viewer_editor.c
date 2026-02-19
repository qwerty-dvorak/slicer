#include "viewer_editor.h"

#include "editor_events.h"
#include "editor_logic.h"
#include "editor_render.h"
#include "editor_types.h"

/* ------------------------------------------------------------------ */
/* Global editor state — shared across all editor_*.c translation      */
/* units via the extern declaration in editor_types.h                  */
/* ------------------------------------------------------------------ */

editor_state_t g_editor = { 0 };

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void
viewer_editor_reset_for_image (const image_t *img)
{
    editor_reset_for_image (img);
}

int
viewer_editor_handle_event (
    viewer_t *viewer,
    const image_t *img,
    const xcb_generic_event_t *event,
    int *request_redraw
)
{
    return editor_handle_event (viewer, img, event, request_redraw);
}

void
viewer_editor_draw_overlay (
    const viewer_t *viewer,
    const image_t *img,
    uint8_t *draw_buf
)
{
    editor_draw_sections (viewer, img, draw_buf);
    editor_draw_cuts (viewer, img, draw_buf);
    editor_draw_hud (viewer, draw_buf);
}

void
viewer_editor_draw_overlay_text (viewer_t *viewer)
{
    editor_draw_hud_text (viewer);
}
