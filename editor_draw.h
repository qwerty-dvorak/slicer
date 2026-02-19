#ifndef EDITOR_DRAW_H
#define EDITOR_DRAW_H

#include <stdint.h>

#include "editor_types.h"
#include "viewer.h"

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                  */
/* All functions write directly into the raw pixel draw buffer.        */
/* ------------------------------------------------------------------ */

void fill_rect_blended (
    const viewer_t *viewer,
    uint8_t *buf,
    const rect_i_t *r,
    uint8_t cr,
    uint8_t cg,
    uint8_t cb,
    uint8_t alpha
);

void draw_line_blended (
    const viewer_t *viewer,
    uint8_t *buf,
    int x0,
    int y0,
    int x1,
    int y1,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t alpha
);

void draw_rect_outline (
    const viewer_t *viewer,
    uint8_t *buf,
    const rect_i_t *r,
    uint8_t cr,
    uint8_t cg,
    uint8_t cb,
    uint8_t alpha
);

#endif /* EDITOR_DRAW_H */
