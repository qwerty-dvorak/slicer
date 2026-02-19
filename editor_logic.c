#include "editor_logic.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_coords.h"

/* ------------------------------------------------------------------ */
/* Cut normalisation and classification                                */
/* ------------------------------------------------------------------ */

void
editor_normalize_cut (cut_t *cut)
{
    int dx = abs (cut->x2 - cut->x1);
    int dy = abs (cut->y2 - cut->y1);

    if (dx >= dy)
        {
            cut->y2 = cut->y1;
            if (cut->x1 > cut->x2)
                {
                    int t = cut->x1;
                    cut->x1 = cut->x2;
                    cut->x2 = t;
                }
        }
    else
        {
            cut->x2 = cut->x1;
            if (cut->y1 > cut->y2)
                {
                    int t = cut->y1;
                    cut->y1 = cut->y2;
                    cut->y2 = t;
                }
        }
}

int
cut_is_vertical (const cut_t *cut)
{
    return cut->x1 == cut->x2;
}

int
cut_is_horizontal (const cut_t *cut)
{
    return cut->y1 == cut->y2;
}

/* ------------------------------------------------------------------ */
/* Section helpers (internal)                                          */
/* ------------------------------------------------------------------ */

static int
cut_splits_section (const cut_t *cut, const section_t *section)
{
    if (cut_is_vertical (cut))
        {
            int x = cut->x1;
            int y0 = cut->y1 < cut->y2 ? cut->y1 : cut->y2;
            int y1 = cut->y1 > cut->y2 ? cut->y1 : cut->y2;
            int section_bottom = section->y + section->h - 1;

            if (section->w < 2)
                {
                    return 0;
                }
            if (x <= section->x || x >= section->x + section->w - 1)
                {
                    return 0;
                }
            if (y0 > section->y || y1 < section_bottom)
                {
                    return 0;
                }
            return 1;
        }

    if (cut_is_horizontal (cut))
        {
            int y = cut->y1;
            int x0 = cut->x1 < cut->x2 ? cut->x1 : cut->x2;
            int x1 = cut->x1 > cut->x2 ? cut->x1 : cut->x2;
            int section_right = section->x + section->w - 1;

            if (section->h < 2)
                {
                    return 0;
                }
            if (y <= section->y || y >= section->y + section->h - 1)
                {
                    return 0;
                }
            if (x0 > section->x || x1 < section_right)
                {
                    return 0;
                }
            return 1;
        }

    return 0;
}

static int
split_section_by_cut (
    const section_t *in,
    const cut_t *cut,
    section_t *a,
    section_t *b
)
{
    if (cut_is_vertical (cut))
        {
            int x = cut->x1;
            a->x = in->x;
            a->y = in->y;
            a->w = x - in->x;
            a->h = in->h;

            b->x = x;
            b->y = in->y;
            b->w = (in->x + in->w) - x;
            b->h = in->h;
        }
    else if (cut_is_horizontal (cut))
        {
            int y = cut->y1;
            a->x = in->x;
            a->y = in->y;
            a->w = in->w;
            a->h = y - in->y;

            b->x = in->x;
            b->y = y;
            b->w = in->w;
            b->h = (in->y + in->h) - y;
        }
    else
        {
            return 0;
        }

    return a->w > 0 && a->h > 0 && b->w > 0 && b->h > 0;
}

static int
build_sections_without_cut (
    const image_t *img,
    int skip_cut_index,
    section_t *sections_out,
    int *section_count_out
)
{
    int i;
    int j;
    int section_count = 0;

    if (!img || !sections_out || !section_count_out)
        {
            return 0;
        }
    if (img->width <= 0 || img->height <= 0)
        {
            return 0;
        }

    sections_out[section_count].x = 0;
    sections_out[section_count].y = 0;
    sections_out[section_count].w = img->width;
    sections_out[section_count].h = img->height;
    section_count = 1;

    for (i = 0; i < g_editor.cut_count; i++)
        {
            const cut_t *cut;

            if (i == skip_cut_index)
                {
                    continue;
                }

            cut = &g_editor.cuts[i];
            for (j = 0; j < section_count; j++)
                {
                    section_t s1;
                    section_t s2;

                    if (!cut_splits_section (cut, &sections_out[j]))
                        {
                            continue;
                        }
                    if (!split_section_by_cut (
                            &sections_out[j], cut, &s1, &s2
                        ))
                        {
                            continue;
                        }

                    sections_out[j] = s1;
                    if (section_count < SECTION_MAX_COUNT)
                        {
                            sections_out[section_count] = s2;
                            section_count++;
                        }
                }
        }

    *section_count_out = section_count;
    return 1;
}

static int
distance_to_range (int value, int lo, int hi)
{
    if (value < lo)
        {
            return lo - value;
        }
    if (value > hi)
        {
            return value - hi;
        }
    return 0;
}

static int
fit_cut_to_best_section (
    cut_t *cut,
    const section_t *sections,
    int section_count,
    const image_t *img
)
{
    int i;
    int best = -1;
    int best_axis = 0;
    int best_area = INT_MAX;
    long long best_score = LLONG_MAX;

    if (!cut || !sections || section_count <= 0 || !img)
        {
            return 0;
        }

    editor_normalize_cut (cut);
    if (cut_is_vertical (cut))
        {
            int target_x = clamp_int (cut->x1, 0, img->width - 1);
            int target_y = (cut->y1 + cut->y2) / 2;

            for (i = 0; i < section_count; i++)
                {
                    const section_t *s = &sections[i];
                    int min_x = s->x + 1;
                    int max_x = s->x + s->w - 2;
                    int snapped_x;
                    int dx;
                    int dy;
                    int area;
                    long long score;

                    if (s->w < 2 || s->h < 2 || min_x > max_x)
                        {
                            continue;
                        }

                    snapped_x = clamp_int (target_x, min_x, max_x);
                    dx = abs (target_x - snapped_x);
                    dy = distance_to_range (target_y, s->y, s->y + s->h - 1);
                    area = s->w * s->h;
                    score = (long long)dy * 4096LL + (long long)dx;

                    if (score < best_score
                        || (score == best_score && area < best_area))
                        {
                            best = i;
                            best_axis = snapped_x;
                            best_score = score;
                            best_area = area;
                        }
                }

            if (best < 0)
                {
                    return 0;
                }

            cut->x1 = best_axis;
            cut->x2 = best_axis;
            cut->y1 = sections[best].y;
            cut->y2 = sections[best].y + sections[best].h - 1;
        }
    else if (cut_is_horizontal (cut))
        {
            int target_x = (cut->x1 + cut->x2) / 2;
            int target_y = clamp_int (cut->y1, 0, img->height - 1);

            for (i = 0; i < section_count; i++)
                {
                    const section_t *s = &sections[i];
                    int min_y = s->y + 1;
                    int max_y = s->y + s->h - 2;
                    int snapped_y;
                    int dx;
                    int dy;
                    int area;
                    long long score;

                    if (s->w < 2 || s->h < 2 || min_y > max_y)
                        {
                            continue;
                        }

                    snapped_y = clamp_int (target_y, min_y, max_y);
                    dx = distance_to_range (target_x, s->x, s->x + s->w - 1);
                    dy = abs (target_y - snapped_y);
                    area = s->w * s->h;
                    score = (long long)dx * 4096LL + (long long)dy;

                    if (score < best_score
                        || (score == best_score && area < best_area))
                        {
                            best = i;
                            best_axis = snapped_y;
                            best_score = score;
                            best_area = area;
                        }
                }

            if (best < 0)
                {
                    return 0;
                }

            cut->y1 = best_axis;
            cut->y2 = best_axis;
            cut->x1 = sections[best].x;
            cut->x2 = sections[best].x + sections[best].w - 1;
        }
    else
        {
            return 0;
        }

    return cut->x1 != cut->x2 || cut->y1 != cut->y2;
}

/* ------------------------------------------------------------------ */
/* Section management                                                  */
/* ------------------------------------------------------------------ */

int
editor_find_section_at (int ix, int iy)
{
    int i;
    int best_idx = -1;
    int best_area = INT_MAX;

    for (i = 0; i < g_editor.section_count; i++)
        {
            const section_t *s = &g_editor.sections[i];
            int area = s->w * s->h;
            if (point_in_section (ix, iy, s) && area < best_area)
                {
                    best_area = area;
                    best_idx = i;
                }
        }
    return best_idx;
}

void
editor_recompute_sections (const image_t *img)
{
    int i;
    int j;

    g_editor.section_count = 0;
    if (img->width <= 0 || img->height <= 0)
        {
            g_editor.selected_section = -1;
            return;
        }

    g_editor.sections[g_editor.section_count].x = 0;
    g_editor.sections[g_editor.section_count].y = 0;
    g_editor.sections[g_editor.section_count].w = img->width;
    g_editor.sections[g_editor.section_count].h = img->height;
    g_editor.section_count = 1;

    for (i = 0; i < g_editor.cut_count; i++)
        {
            const cut_t *cut = &g_editor.cuts[i];

            for (j = 0; j < g_editor.section_count; j++)
                {
                    section_t s1;
                    section_t s2;

                    if (!cut_splits_section (cut, &g_editor.sections[j]))
                        {
                            continue;
                        }
                    if (!split_section_by_cut (
                            &g_editor.sections[j], cut, &s1, &s2
                        ))
                        {
                            continue;
                        }

                    g_editor.sections[j] = s1;
                    if (g_editor.section_count < SECTION_MAX_COUNT)
                        {
                            g_editor.sections[g_editor.section_count] = s2;
                            g_editor.section_count++;
                        }
                }
        }

    if (g_editor.selected_cut >= 0
        && g_editor.selected_cut < g_editor.cut_count)
        {
            const cut_t *cut = &g_editor.cuts[g_editor.selected_cut];
            int mx = (cut->x1 + cut->x2) / 2;
            int my = (cut->y1 + cut->y2) / 2;
            g_editor.selected_section = editor_find_section_at (mx, my);
        }
    if (g_editor.selected_section < 0
        || g_editor.selected_section >= g_editor.section_count)
        {
            g_editor.selected_section = g_editor.section_count > 0 ? 0 : -1;
        }
}

int
editor_apply_grid_to_selected_section (const image_t *img)
{
    int col;
    int row;
    int added_any = 0;
    section_t section;

    if (g_editor.selected_section < 0
        || g_editor.selected_section >= g_editor.section_count)
        {
            return 0;
        }
    if (g_editor.grid_cols < 2 && g_editor.grid_rows < 2)
        {
            return 0;
        }

    section = g_editor.sections[g_editor.selected_section];
    for (col = 1; col < g_editor.grid_cols; col++)
        {
            int x = section.x + (section.w * col) / g_editor.grid_cols;
            cut_t cut;
            if (x <= section.x || x >= section.x + section.w)
                {
                    continue;
                }

            cut.x1 = x;
            cut.y1 = section.y;
            cut.x2 = x;
            cut.y2 = section.y + section.h - 1;
            added_any |= editor_add_cut_raw (cut, img);
        }

    for (row = 1; row < g_editor.grid_rows; row++)
        {
            int y = section.y + (section.h * row) / g_editor.grid_rows;
            cut_t cut;
            if (y <= section.y || y >= section.y + section.h)
                {
                    continue;
                }

            cut.x1 = section.x;
            cut.y1 = y;
            cut.x2 = section.x + section.w - 1;
            cut.y2 = y;
            added_any |= editor_add_cut_raw (cut, img);
        }

    if (added_any)
        {
            editor_recompute_sections (img);
        }
    return added_any;
}

int
editor_adjust_grid_size (int dcols, int drows)
{
    int next_cols = clamp_int (g_editor.grid_cols + dcols, 1, 64);
    int next_rows = clamp_int (g_editor.grid_rows + drows, 1, 64);
    int changed = 0;

    if (next_cols != g_editor.grid_cols)
        {
            g_editor.grid_cols = next_cols;
            changed = 1;
        }
    if (next_rows != g_editor.grid_rows)
        {
            g_editor.grid_rows = next_rows;
            changed = 1;
        }
    return changed;
}

void
editor_export_sections_stdout (void)
{
    int i;

    for (i = 0; i < g_editor.section_count; i++)
        {
            const section_t *s = &g_editor.sections[i];
            printf (
                "section_%d { x: %d, y: %d, w: %d, h: %d }\n",
                i,
                s->x,
                s->y,
                s->w,
                s->h
            );
        }
    fflush (stdout);
}

/* ------------------------------------------------------------------ */
/* Cut management                                                      */
/* ------------------------------------------------------------------ */

int
editor_cut_equals (const cut_t *a, const cut_t *b)
{
    return a->x1 == b->x1 && a->y1 == b->y1 && a->x2 == b->x2
           && a->y2 == b->y2;
}

int
editor_add_cut_raw (cut_t cut, const image_t *img)
{
    int i;

    editor_normalize_cut (&cut);
    cut.x1 = clamp_int (cut.x1, 0, img->width - 1);
    cut.x2 = clamp_int (cut.x2, 0, img->width - 1);
    cut.y1 = clamp_int (cut.y1, 0, img->height - 1);
    cut.y2 = clamp_int (cut.y2, 0, img->height - 1);
    editor_normalize_cut (&cut);

    if ((!cut_is_vertical (&cut) && !cut_is_horizontal (&cut))
        || (cut.x1 == cut.x2 && cut.y1 == cut.y2))
        {
            return 0;
        }
    for (i = 0; i < g_editor.cut_count; i++)
        {
            if (editor_cut_equals (&g_editor.cuts[i], &cut))
                {
                    return 0;
                }
        }
    if (g_editor.cut_count >= CUT_MAX_COUNT)
        {
            return 0;
        }

    g_editor.cuts[g_editor.cut_count] = cut;
    g_editor.selected_cut = g_editor.cut_count;
    g_editor.cut_count++;
    return 1;
}

int
editor_add_cut (cut_t cut, const image_t *img)
{
    int added = editor_add_cut_raw (cut, img);
    if (added)
        {
            editor_recompute_sections (img);
        }
    return added;
}

int
editor_refit_cut_to_closed_region (int cut_index, const image_t *img)
{
    section_t base_sections[SECTION_MAX_COUNT];
    int base_section_count = 0;
    cut_t adjusted;
    int i;

    if (!img)
        {
            return 0;
        }
    if (cut_index < 0 || cut_index >= g_editor.cut_count)
        {
            return 0;
        }

    adjusted = g_editor.cuts[cut_index];
    adjusted.x1 = clamp_int (adjusted.x1, 0, img->width - 1);
    adjusted.x2 = clamp_int (adjusted.x2, 0, img->width - 1);
    adjusted.y1 = clamp_int (adjusted.y1, 0, img->height - 1);
    adjusted.y2 = clamp_int (adjusted.y2, 0, img->height - 1);
    editor_normalize_cut (&adjusted);

    if (!build_sections_without_cut (
            img, cut_index, base_sections, &base_section_count
        ))
        {
            return 0;
        }
    if (!fit_cut_to_best_section (
            &adjusted, base_sections, base_section_count, img
        ))
        {
            return 0;
        }

    for (i = 0; i < g_editor.cut_count; i++)
        {
            if (i == cut_index)
                {
                    continue;
                }
            if (editor_cut_equals (&g_editor.cuts[i], &adjusted))
                {
                    return 0;
                }
        }

    g_editor.cuts[cut_index] = adjusted;
    return 1;
}

void
editor_delete_selected_cut (const image_t *img)
{
    int idx = g_editor.selected_cut;
    int tail;

    if (idx < 0 || idx >= g_editor.cut_count)
        {
            return;
        }

    tail = g_editor.cut_count - idx - 1;
    if (tail > 0)
        {
            memmove (
                &g_editor.cuts[idx],
                &g_editor.cuts[idx + 1],
                (size_t)tail * sizeof (cut_t)
            );
        }
    g_editor.cut_count--;

    if (g_editor.cut_count == 0)
        {
            g_editor.selected_cut = -1;
        }
    else if (idx >= g_editor.cut_count)
        {
            g_editor.selected_cut = g_editor.cut_count - 1;
        }

    editor_recompute_sections (img);
}

void
editor_rotate_selected_cut (const image_t *img)
{
    cut_t *cut;
    cut_t original;

    if (!img || img->width <= 0 || img->height <= 0)
        {
            return;
        }
    if (g_editor.selected_cut < 0
        || g_editor.selected_cut >= g_editor.cut_count)
        {
            return;
        }

    cut = &g_editor.cuts[g_editor.selected_cut];
    original = *cut;
    if (cut_is_vertical (cut))
        {
            int cx = cut->x1;
            int cy = (cut->y1 + cut->y2) / 2;
            cut->x1 = cx - 1;
            cut->y1 = cy;
            cut->x2 = cx + 1;
            cut->y2 = cy;
        }
    else
        {
            int cx = (cut->x1 + cut->x2) / 2;
            int cy = cut->y1;
            int y0 = cy - 1;
            int y1 = cy + 1;
            cut->x1 = cx;
            cut->y1 = y0;
            cut->x2 = cx;
            cut->y2 = y1;
        }

    cut->x1 = clamp_int (cut->x1, 0, img->width - 1);
    cut->x2 = clamp_int (cut->x2, 0, img->width - 1);
    cut->y1 = clamp_int (cut->y1, 0, img->height - 1);
    cut->y2 = clamp_int (cut->y2, 0, img->height - 1);
    editor_normalize_cut (cut);

    if (!editor_refit_cut_to_closed_region (g_editor.selected_cut, img))
        {
            *cut = original;
            return;
        }

    editor_recompute_sections (img);
}

void
editor_translate_cut_clamped (cut_t *cut, int dx, int dy, const image_t *img)
{
    int min_x;
    int max_x;
    int min_y;
    int max_y;

    cut->x1 += dx;
    cut->x2 += dx;
    cut->y1 += dy;
    cut->y2 += dy;

    min_x = cut->x1 < cut->x2 ? cut->x1 : cut->x2;
    max_x = cut->x1 > cut->x2 ? cut->x1 : cut->x2;
    min_y = cut->y1 < cut->y2 ? cut->y1 : cut->y2;
    max_y = cut->y1 > cut->y2 ? cut->y1 : cut->y2;

    if (min_x < 0)
        {
            cut->x1 -= min_x;
            cut->x2 -= min_x;
        }
    if (max_x >= img->width)
        {
            int shift = max_x - (img->width - 1);
            cut->x1 -= shift;
            cut->x2 -= shift;
        }
    if (min_y < 0)
        {
            cut->y1 -= min_y;
            cut->y2 -= min_y;
        }
    if (max_y >= img->height)
        {
            int shift = max_y - (img->height - 1);
            cut->y1 -= shift;
            cut->y2 -= shift;
        }
}

/* ------------------------------------------------------------------ */
/* Hit testing                                                         */
/* ------------------------------------------------------------------ */

static int
distance_sq_to_segment_screen (int px, int py, int x1, int y1, int x2, int y2)
{
    long long vx = (long long)x2 - x1;
    long long vy = (long long)y2 - y1;
    long long wx = (long long)px - x1;
    long long wy = (long long)py - y1;
    long long c1 = vx * wx + vy * wy;
    long long c2 = vx * vx + vy * vy;

    if (c2 <= 0)
        {
            long long dx = (long long)px - x1;
            long long dy = (long long)py - y1;
            return (int)(dx * dx + dy * dy);
        }

    if (c1 <= 0)
        {
            long long dx = (long long)px - x1;
            long long dy = (long long)py - y1;
            return (int)(dx * dx + dy * dy);
        }
    if (c1 >= c2)
        {
            long long dx = (long long)px - x2;
            long long dy = (long long)py - y2;
            return (int)(dx * dx + dy * dy);
        }

    {
        long long projx_num = (long long)x1 * c2 + vx * c1;
        long long projy_num = (long long)y1 * c2 + vy * c1;
        long long dx_num = (long long)px * c2 - projx_num;
        long long dy_num = (long long)py * c2 - projy_num;
        long long d2_num = dx_num * dx_num + dy_num * dy_num;
        return (int)(d2_num / (c2 * c2));
    }
}

int
editor_find_cut_at_screen (
    const viewer_t *viewer,
    const image_t *img,
    int sx,
    int sy
)
{
    int i;
    int best = -1;
    int best_d2 = INT_MAX;
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
            return -1;
        }

    for (i = 0; i < g_editor.cut_count; i++)
        {
            const cut_t *cut = &g_editor.cuts[i];
            int x1 = image_to_screen_x (&vr, img, cut->x1);
            int y1 = image_to_screen_y (&vr, img, cut->y1);
            int x2 = image_to_screen_x (&vr, img, cut->x2);
            int y2 = image_to_screen_y (&vr, img, cut->y2);
            int d2 = distance_sq_to_segment_screen (sx, sy, x1, y1, x2, y2);

            if (d2 < best_d2)
                {
                    best_d2 = d2;
                    best = i;
                }
        }

    if (best_d2 <= 64)
        {
            return best;
        }
    return -1;
}

int
editor_endpoint_hit (
    const viewer_t *viewer,
    const image_t *img,
    const cut_t *cut,
    int sx,
    int sy
)
{
    view_rect_t vr;
    int ax;
    int ay;
    int bx;
    int by;
    int da2;
    int db2;

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
            return 0;
        }

    ax = image_to_screen_x (&vr, img, cut->x1);
    ay = image_to_screen_y (&vr, img, cut->y1);
    bx = image_to_screen_x (&vr, img, cut->x2);
    by = image_to_screen_y (&vr, img, cut->y2);
    da2 = (sx - ax) * (sx - ax) + (sy - ay) * (sy - ay);
    db2 = (sx - bx) * (sx - bx) + (sy - by) * (sy - by);

    if (da2 <= 49)
        {
            return 1;
        }
    if (db2 <= 49)
        {
            return 2;
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Editor state initialisation                                         */
/* ------------------------------------------------------------------ */

void
editor_reset_for_image (const image_t *img)
{
    memset (&g_editor, 0, sizeof (g_editor));

    g_editor.initialized = 1;
    g_editor.hud_visible = 1;
    g_editor.tool = TOOL_DRAW;
    g_editor.selected_cut = -1;
    g_editor.selected_section = 0;
    g_editor.grid_cols = 2;
    g_editor.grid_rows = 2;

    editor_recompute_sections (img);
}

/* ------------------------------------------------------------------ */
/* HUD geometry                                                        */
/* ------------------------------------------------------------------ */

void
hud_get_layout (const viewer_t *viewer, hud_layout_t *layout)
{
    int bar_x = 12;
    int bar_w = viewer->win_w - 24;
    int bar_h = 64;
    int bar_y = viewer->win_h - bar_h - 12;
    int gap = 12;
    int inner_w;
    int btn_w;
    int btn_h = 36;
    int i;

    if (bar_w < 300)
        {
            bar_w = 300;
            bar_x = (viewer->win_w - bar_w) / 2;
        }
    if (bar_y < 0)
        {
            bar_y = 0;
        }

    layout->bar.x = bar_x;
    layout->bar.y = bar_y;
    layout->bar.w = bar_w;
    layout->bar.h = bar_h;

    inner_w = bar_w - 2 * 14;
    btn_w = (inner_w - (HUD_BTN_COUNT - 1) * gap) / HUD_BTN_COUNT;
    if (btn_w < 48)
        {
            btn_w = 48;
        }
    for (i = 0; i < HUD_BTN_COUNT; i++)
        {
            layout->buttons[i].x = bar_x + 14 + i * (btn_w + gap);
            layout->buttons[i].y = bar_y + (bar_h - btn_h) / 2;
            layout->buttons[i].w = btn_w;
            layout->buttons[i].h = btn_h;
        }
}

int
hud_button_hit (const viewer_t *viewer, int x, int y)
{
    hud_layout_t layout;
    int i;

    if (!g_editor.hud_visible)
        {
            return -1;
        }

    hud_get_layout (viewer, &layout);
    if (!point_in_rect (x, y, &layout.bar))
        {
            return -1;
        }

    for (i = 0; i < HUD_BTN_COUNT; i++)
        {
            if (point_in_rect (x, y, &layout.buttons[i]))
                {
                    return i;
                }
        }
    return -1;
}
