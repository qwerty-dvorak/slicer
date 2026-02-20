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
/* BSP / 2D k-d tree (internal)                                        */
/* ------------------------------------------------------------------ */

#define BSP_NODE_MAX_COUNT (CUT_MAX_COUNT * 2 + 1)

typedef struct
{
    section_t bounds;
    int parent;
    int child_a;
    int child_b;
    int cut_index;
    int split_vertical;
    int split_value;
} bsp_node_t;

typedef struct
{
    bsp_node_t nodes[BSP_NODE_MAX_COUNT];
    int node_count;
    int root;
} bsp_tree_t;

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
cut_has_axis (const cut_t *cut)
{
    return cut_is_vertical (cut) || cut_is_horizontal (cut);
}

static int
cut_is_point (const cut_t *cut)
{
    return cut->x1 == cut->x2 && cut->y1 == cut->y2;
}

static int
cut_span_for_axis (const cut_t *cut)
{
    if (cut_is_vertical (cut))
        {
            return abs (cut->y2 - cut->y1) + 1;
        }
    if (cut_is_horizontal (cut))
        {
            return abs (cut->x2 - cut->x1) + 1;
        }
    return 0;
}

static long long
bsp_resize_direction_penalty (
    editor_refit_mode_t mode,
    int candidate_span,
    int reference_span
)
{
    if (reference_span <= 0 || mode == EDITOR_REFIT_DEFAULT)
        {
            return 0;
        }

    if (mode == EDITOR_REFIT_PREFER_PARENT)
        {
            if (candidate_span <= reference_span)
                {
                    return (long long)(reference_span - candidate_span + 1)
                           * 1000000LL;
                }
            return 0;
        }

    if (mode == EDITOR_REFIT_PREFER_CHILD)
        {
            if (candidate_span >= reference_span)
                {
                    return (long long)(candidate_span - reference_span + 1)
                           * 1000000LL;
                }
            return 0;
        }

    return 0;
}

static int
bsp_score_better (
    editor_refit_mode_t mode,
    long long score,
    long long best_score,
    int span,
    int best_span,
    int area,
    int best_area
)
{
    if (score < best_score)
        {
            return 1;
        }
    if (score > best_score)
        {
            return 0;
        }

    if (mode == EDITOR_REFIT_PREFER_PARENT)
        {
            if (span > best_span)
                {
                    return 1;
                }
            if (span < best_span)
                {
                    return 0;
                }
        }
    else if (mode == EDITOR_REFIT_PREFER_CHILD)
        {
            if (span < best_span)
                {
                    return 1;
                }
            if (span > best_span)
                {
                    return 0;
                }
        }

    return area < best_area;
}

static int
normalize_cut_for_image (cut_t *cut, const image_t *img)
{
    if (!cut || !img || img->width <= 0 || img->height <= 0)
        {
            return 0;
        }

    editor_normalize_cut (cut);
    cut->x1 = clamp_int (cut->x1, 0, img->width - 1);
    cut->x2 = clamp_int (cut->x2, 0, img->width - 1);
    cut->y1 = clamp_int (cut->y1, 0, img->height - 1);
    cut->y2 = clamp_int (cut->y2, 0, img->height - 1);
    editor_normalize_cut (cut);

    if (!cut_has_axis (cut) || cut_is_point (cut))
        {
            return 0;
        }

    return 1;
}

static int
bsp_node_is_leaf (const bsp_node_t *node)
{
    return node->child_a < 0 && node->child_b < 0;
}

static void
bsp_tree_reset (bsp_tree_t *tree)
{
    tree->node_count = 0;
    tree->root = -1;
}

static int
bsp_tree_alloc_leaf (bsp_tree_t *tree, const section_t *bounds, int parent)
{
    bsp_node_t *node;
    int idx;

    if (tree->node_count >= BSP_NODE_MAX_COUNT)
        {
            return -1;
        }

    idx = tree->node_count++;
    node = &tree->nodes[idx];
    node->bounds = *bounds;
    node->parent = parent;
    node->child_a = -1;
    node->child_b = -1;
    node->cut_index = -1;
    node->split_vertical = 0;
    node->split_value = 0;
    return idx;
}

static int
bsp_tree_init_root (bsp_tree_t *tree, const image_t *img)
{
    section_t root;
    int idx;

    if (!tree || !img || img->width <= 0 || img->height <= 0)
        {
            return 0;
        }

    root.x = 0;
    root.y = 0;
    root.w = img->width;
    root.h = img->height;

    idx = bsp_tree_alloc_leaf (tree, &root, -1);
    if (idx < 0)
        {
            return 0;
        }

    tree->root = idx;
    return 1;
}

static int
bsp_choose_leaf_for_cut (
    const bsp_tree_t *tree,
    const cut_t *cut,
    editor_refit_mode_t mode,
    int reference_span,
    int *leaf_out,
    int *split_value_out
)
{
    int i;
    int best_leaf = -1;
    int best_split = 0;
    int best_span = 0;
    int best_area = INT_MAX;
    long long best_score = LLONG_MAX;
    int desired_span;

    if (!tree || !cut || tree->root < 0)
        {
            return 0;
        }

    desired_span = cut_span_for_axis (cut);
    if (cut_is_vertical (cut))
        {
            int target_x = cut->x1;
            int target_y = (cut->y1 + cut->y2) / 2;

            for (i = 0; i < tree->node_count; i++)
                {
                    const bsp_node_t *node = &tree->nodes[i];
                    const section_t *s;
                    int min_x;
                    int max_x;
                    int snapped_x;
                    int dx;
                    int dy;
                    int span;
                    int span_delta;
                    int area;
                    long long dir_penalty;
                    long long score;

                    if (!bsp_node_is_leaf (node))
                        {
                            continue;
                        }

                    s = &node->bounds;
                    if (s->w < 2 || s->h < 1)
                        {
                            continue;
                        }

                    min_x = s->x + 1;
                    max_x = s->x + s->w - 1;
                    if (min_x > max_x)
                        {
                            continue;
                        }

                    snapped_x = clamp_int (target_x, min_x, max_x);
                    dx = abs (target_x - snapped_x);
                    dy = distance_to_range (target_y, s->y, s->y + s->h - 1);
                    span = s->h;
                    span_delta = abs (span - desired_span);
                    area = s->w * s->h;
                    dir_penalty = bsp_resize_direction_penalty (
                        mode, span, reference_span
                    );
                    score = dir_penalty + (long long)span_delta * 128LL
                            + (long long)dy * 4096LL + (long long)dx;

                    if (best_leaf < 0
                        || bsp_score_better (
                            mode,
                            score,
                            best_score,
                            span,
                            best_span,
                            area,
                            best_area
                        ))
                        {
                            best_leaf = i;
                            best_split = snapped_x;
                            best_span = span;
                            best_area = area;
                            best_score = score;
                        }
                }
        }
    else if (cut_is_horizontal (cut))
        {
            int target_x = (cut->x1 + cut->x2) / 2;
            int target_y = cut->y1;

            for (i = 0; i < tree->node_count; i++)
                {
                    const bsp_node_t *node = &tree->nodes[i];
                    const section_t *s;
                    int min_y;
                    int max_y;
                    int snapped_y;
                    int dx;
                    int dy;
                    int span;
                    int span_delta;
                    int area;
                    long long dir_penalty;
                    long long score;

                    if (!bsp_node_is_leaf (node))
                        {
                            continue;
                        }

                    s = &node->bounds;
                    if (s->h < 2 || s->w < 1)
                        {
                            continue;
                        }

                    min_y = s->y + 1;
                    max_y = s->y + s->h - 1;
                    if (min_y > max_y)
                        {
                            continue;
                        }

                    snapped_y = clamp_int (target_y, min_y, max_y);
                    dx = distance_to_range (target_x, s->x, s->x + s->w - 1);
                    dy = abs (target_y - snapped_y);
                    span = s->w;
                    span_delta = abs (span - desired_span);
                    area = s->w * s->h;
                    dir_penalty = bsp_resize_direction_penalty (
                        mode, span, reference_span
                    );
                    score = dir_penalty + (long long)span_delta * 128LL
                            + (long long)dx * 4096LL + (long long)dy;

                    if (best_leaf < 0
                        || bsp_score_better (
                            mode,
                            score,
                            best_score,
                            span,
                            best_span,
                            area,
                            best_area
                        ))
                        {
                            best_leaf = i;
                            best_split = snapped_y;
                            best_span = span;
                            best_area = area;
                            best_score = score;
                        }
                }
        }
    else
        {
            return 0;
        }

    if (best_leaf < 0)
        {
            return 0;
        }

    *leaf_out = best_leaf;
    *split_value_out = best_split;
    return 1;
}

static int
bsp_split_leaf (
    bsp_tree_t *tree,
    int leaf_index,
    int cut_index,
    int split_vertical,
    int split_value,
    cut_t *snapped_cut_out
)
{
    bsp_node_t *leaf;
    section_t a;
    section_t b;
    int child_a;
    int child_b;

    if (!tree || leaf_index < 0 || leaf_index >= tree->node_count)
        {
            return 0;
        }

    leaf = &tree->nodes[leaf_index];
    if (!bsp_node_is_leaf (leaf))
        {
            return 0;
        }

    if (split_vertical)
        {
            const section_t *s = &leaf->bounds;
            if (s->w < 2)
                {
                    return 0;
                }
            if (split_value <= s->x || split_value >= s->x + s->w)
                {
                    return 0;
                }

            a.x = s->x;
            a.y = s->y;
            a.w = split_value - s->x;
            a.h = s->h;

            b.x = split_value;
            b.y = s->y;
            b.w = (s->x + s->w) - split_value;
            b.h = s->h;

            if (snapped_cut_out)
                {
                    snapped_cut_out->x1 = split_value;
                    snapped_cut_out->y1 = s->y;
                    snapped_cut_out->x2 = split_value;
                    snapped_cut_out->y2 = s->y + s->h - 1;
                }
        }
    else
        {
            const section_t *s = &leaf->bounds;
            if (s->h < 2)
                {
                    return 0;
                }
            if (split_value <= s->y || split_value >= s->y + s->h)
                {
                    return 0;
                }

            a.x = s->x;
            a.y = s->y;
            a.w = s->w;
            a.h = split_value - s->y;

            b.x = s->x;
            b.y = split_value;
            b.w = s->w;
            b.h = (s->y + s->h) - split_value;

            if (snapped_cut_out)
                {
                    snapped_cut_out->x1 = s->x;
                    snapped_cut_out->y1 = split_value;
                    snapped_cut_out->x2 = s->x + s->w - 1;
                    snapped_cut_out->y2 = split_value;
                }
        }

    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        {
            return 0;
        }

    child_a = bsp_tree_alloc_leaf (tree, &a, leaf_index);
    if (child_a < 0)
        {
            return 0;
        }

    child_b = bsp_tree_alloc_leaf (tree, &b, leaf_index);
    if (child_b < 0)
        {
            tree->node_count--;
            return 0;
        }

    leaf->child_a = child_a;
    leaf->child_b = child_b;
    leaf->cut_index = cut_index;
    leaf->split_vertical = split_vertical;
    leaf->split_value = split_value;
    return 1;
}

static int
bsp_insert_cut (
    bsp_tree_t *tree,
    int cut_index,
    cut_t *cut,
    editor_refit_mode_t mode,
    int reference_span
)
{
    int leaf_index;
    int split_value;
    cut_t snapped;

    if (!tree || !cut || tree->root < 0)
        {
            return 0;
        }
    if (!cut_has_axis (cut) || cut_is_point (cut))
        {
            return 0;
        }
    if (!bsp_choose_leaf_for_cut (
            tree, cut, mode, reference_span, &leaf_index, &split_value
        ))
        {
            return 0;
        }
    if (!bsp_split_leaf (
            tree,
            leaf_index,
            cut_index,
            cut_is_vertical (cut),
            split_value,
            &snapped
        ))
        {
            return 0;
        }

    *cut = snapped;
    return 1;
}

static int
bsp_build_tree_from_current_cuts (
    const image_t *img,
    int skip_cut_index,
    bsp_tree_t *tree
)
{
    int i;
    int next_index = 0;

    if (!tree)
        {
            return 0;
        }

    bsp_tree_reset (tree);
    if (!bsp_tree_init_root (tree, img))
        {
            return 0;
        }

    for (i = 0; i < g_editor.cut_count; i++)
        {
            cut_t cut;
            if (i == skip_cut_index)
                {
                    continue;
                }

            cut = g_editor.cuts[i];
            if (!normalize_cut_for_image (&cut, img))
                {
                    continue;
                }

            /* Cuts are interpreted as k-d split nodes in sequence.
               Invalid splits are skipped during temporary builds. */
            if (bsp_insert_cut (
                    tree,
                    next_index,
                    &cut,
                    EDITOR_REFIT_DEFAULT,
                    0
                ))
                {
                    next_index++;
                }
        }

    return 1;
}

static void
bsp_emit_sections_to_editor (const bsp_tree_t *tree)
{
    int i;

    g_editor.section_count = 0;
    if (!tree || tree->root < 0)
        {
            return;
        }

    for (i = 0; i < tree->node_count; i++)
        {
            const bsp_node_t *node = &tree->nodes[i];
            if (!bsp_node_is_leaf (node))
                {
                    continue;
                }
            if (g_editor.section_count >= SECTION_MAX_COUNT)
                {
                    break;
                }

            g_editor.sections[g_editor.section_count] = node->bounds;
            g_editor.section_count++;
        }
}

static int
bsp_rebuild_editor_cuts_and_tree (const image_t *img, bsp_tree_t *tree)
{
    cut_t old_cuts[CUT_MAX_COUNT];
    int old_cut_count = g_editor.cut_count;
    int old_selected_cut = g_editor.selected_cut;
    int selected_new = -1;
    int i;
    int write = 0;

    if (!tree || !img || img->width <= 0 || img->height <= 0)
        {
            return 0;
        }

    if (old_cut_count > CUT_MAX_COUNT)
        {
            old_cut_count = CUT_MAX_COUNT;
        }
    if (old_cut_count > 0)
        {
            memcpy (
                old_cuts, g_editor.cuts, (size_t)old_cut_count * sizeof (cut_t)
            );
        }

    bsp_tree_reset (tree);
    if (!bsp_tree_init_root (tree, img))
        {
            return 0;
        }

    for (i = 0; i < old_cut_count; i++)
        {
            cut_t cut = old_cuts[i];

            if (!normalize_cut_for_image (&cut, img))
                {
                    continue;
                }
            if (!bsp_insert_cut (
                    tree, write, &cut, EDITOR_REFIT_DEFAULT, 0
                ))
                {
                    continue;
                }

            g_editor.cuts[write] = cut;
            if (i == old_selected_cut)
                {
                    selected_new = write;
                }
            write++;
        }

    g_editor.cut_count = write;
    if (g_editor.cut_count == 0)
        {
            g_editor.selected_cut = -1;
        }
    else if (selected_new >= 0)
        {
            g_editor.selected_cut = selected_new;
        }
    else if (old_selected_cut >= g_editor.cut_count)
        {
            g_editor.selected_cut = g_editor.cut_count - 1;
        }
    else if (old_selected_cut < 0)
        {
            g_editor.selected_cut = -1;
        }
    else
        {
            g_editor.selected_cut = old_selected_cut;
        }

    return 1;
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
    bsp_tree_t tree;

    g_editor.section_count = 0;
    if (!img || img->width <= 0 || img->height <= 0)
        {
            g_editor.selected_section = -1;
            return;
        }

    if (!bsp_rebuild_editor_cuts_and_tree (img, &tree))
        {
            g_editor.selected_section = -1;
            return;
        }

    bsp_emit_sections_to_editor (&tree);

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
    bsp_tree_t tree;
    int i;

    if (!normalize_cut_for_image (&cut, img))
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

    if (!bsp_build_tree_from_current_cuts (img, -1, &tree))
        {
            return 0;
        }
    if (!bsp_insert_cut (
            &tree, g_editor.cut_count, &cut, EDITOR_REFIT_DEFAULT, 0
        ))
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
    return editor_refit_cut_to_closed_region_with_mode (
        cut_index, img, EDITOR_REFIT_DEFAULT, 0
    );
}

int
editor_refit_cut_to_closed_region_with_mode (
    int cut_index,
    const image_t *img,
    editor_refit_mode_t mode,
    int reference_span
)
{
    bsp_tree_t tree;
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
    if (!normalize_cut_for_image (&adjusted, img))
        {
            return 0;
        }

    if (!bsp_build_tree_from_current_cuts (img, cut_index, &tree))
        {
            return 0;
        }

    /* Re-insertion into the tree performs the "slide split value"
       behavior. Resize uses mode/reference_span to prefer
       parent-promotion or child-demotion in the BSP hierarchy. */
    if (!bsp_insert_cut (&tree, cut_index, &adjusted, mode, reference_span))
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

    /* Deletion is modeled by rebuilding the k-d tree without the node;
       surviving descendants are promoted into larger leaves. */
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
