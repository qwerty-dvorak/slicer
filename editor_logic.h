#ifndef EDITOR_LOGIC_H
#define EDITOR_LOGIC_H

#include "editor_types.h"
#include "image.h"
#include "viewer.h"

/* ------------------------------------------------------------------ */
/* Cut management                                                      */
/* ------------------------------------------------------------------ */

void editor_normalize_cut (cut_t *cut);
int cut_is_vertical (const cut_t *cut);
int cut_is_horizontal (const cut_t *cut);

int editor_cut_equals (const cut_t *a, const cut_t *b);
int editor_add_cut_raw (cut_t cut, const image_t *img);
int editor_add_cut (cut_t cut, const image_t *img);
void editor_delete_selected_cut (const image_t *img);
void editor_rotate_selected_cut (const image_t *img);
void
editor_translate_cut_clamped (cut_t *cut, int dx, int dy, const image_t *img);
int editor_refit_cut_to_closed_region (int cut_index, const image_t *img);

/* ------------------------------------------------------------------ */
/* Section management                                                  */
/* ------------------------------------------------------------------ */

int editor_find_section_at (int ix, int iy);
void editor_recompute_sections (const image_t *img);
int editor_apply_grid_to_selected_section (const image_t *img);
int editor_adjust_grid_size (int dcols, int drows);
void editor_export_sections_stdout (void);

/* ------------------------------------------------------------------ */
/* Hit testing                                                         */
/* ------------------------------------------------------------------ */

int editor_find_cut_at_screen (
    const viewer_t *viewer,
    const image_t *img,
    int sx,
    int sy
);

int editor_endpoint_hit (
    const viewer_t *viewer,
    const image_t *img,
    const cut_t *cut,
    int sx,
    int sy
);

/* ------------------------------------------------------------------ */
/* Editor state initialisation                                         */
/* ------------------------------------------------------------------ */

void editor_reset_for_image (const image_t *img);

/* ------------------------------------------------------------------ */
/* HUD geometry                                                        */
/* ------------------------------------------------------------------ */

void hud_get_layout (const viewer_t *viewer, hud_layout_t *layout);
int hud_button_hit (const viewer_t *viewer, int x, int y);

#endif /* EDITOR_LOGIC_H */
