#ifndef EDITOR_RENDER_H
#define EDITOR_RENDER_H

#include <stdint.h>

#include "image.h"
#include "viewer.h"

/* ------------------------------------------------------------------ */
/* Overlay rendering (written into the raw pixel draw buffer)          */
/* ------------------------------------------------------------------ */

void editor_draw_sections (
    const viewer_t *viewer,
    const image_t *img,
    uint8_t *buf
);

void
editor_draw_cuts (const viewer_t *viewer, const image_t *img, uint8_t *buf);

void editor_draw_hud (const viewer_t *viewer, uint8_t *buf);

/* ------------------------------------------------------------------ */
/* Text rendering (uses XCB text drawing; mutates viewer gc)           */
/* ------------------------------------------------------------------ */

void editor_draw_hud_text (viewer_t *viewer);

#endif /* EDITOR_RENDER_H */
