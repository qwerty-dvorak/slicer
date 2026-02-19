#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Key / mouse constants                                               */
/* ------------------------------------------------------------------ */

#define KEYCODE_ESC 9
#define KEYCODE_TAB 23
#define KEYCODE_D 40
#define KEYCODE_E 26
#define KEYCODE_G 42
#define KEYCODE_H 43
#define KEYCODE_M 58
#define KEYCODE_R 27
#define KEYCODE_S 39
#define KEYCODE_X 53
#define KEYCODE_LEFT 113
#define KEYCODE_RIGHT 114
#define KEYCODE_UP 111
#define KEYCODE_DOWN 116
#define KEYCODE_BACKSPACE 22
#define KEYCODE_DELETE 119

#define MOUSE_BUTTON_LEFT 1

/* ------------------------------------------------------------------ */
/* Editor capacity limits                                              */
/* ------------------------------------------------------------------ */

#define CUT_MAX_COUNT 1024
#define SECTION_MAX_COUNT 2048

/* ------------------------------------------------------------------ */
/* Basic geometry                                                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    int x;
    int y;
    int w;
    int h;
} rect_i_t;

typedef struct
{
    int draw_w;
    int draw_h;
    int off_x;
    int off_y;
} view_rect_t;

/* ------------------------------------------------------------------ */
/* Editor data types                                                   */
/* ------------------------------------------------------------------ */

typedef struct
{
    int x1;
    int y1;
    int x2;
    int y2;
} cut_t;

typedef struct
{
    int x;
    int y;
    int w;
    int h;
} section_t;

typedef enum
{
    TOOL_DRAW = 0,
    TOOL_SELECT = 1,
    TOOL_MOVE = 2
} editor_tool_t;

typedef enum
{
    DRAG_NONE = 0,
    DRAG_DRAW_NEW = 1,
    DRAG_MOVE_CUT = 2,
    DRAG_RESIZE_A = 3,
    DRAG_RESIZE_B = 4
} drag_mode_t;

typedef enum
{
    HUD_BTN_DRAW = 0,
    HUD_BTN_SELECT = 1,
    HUD_BTN_MOVE = 2,
    HUD_BTN_GRID = 3,
    HUD_BTN_COUNT = 4
} hud_button_t;

typedef struct
{
    rect_i_t bar;
    rect_i_t buttons[HUD_BTN_COUNT];
} hud_layout_t;

typedef struct
{
    int initialized;
    int hud_visible;
    editor_tool_t tool;

    cut_t cuts[CUT_MAX_COUNT];
    int cut_count;
    int selected_cut;

    section_t sections[SECTION_MAX_COUNT];
    int section_count;
    int selected_section;

    drag_mode_t drag_mode;
    int drag_last_img_x;
    int drag_last_img_y;

    int preview_active;
    int preview_x1;
    int preview_y1;
    int preview_x2;
    int preview_y2;

    int grid_cols;
    int grid_rows;
} editor_state_t;

/* ------------------------------------------------------------------ */
/* Global editor state — defined in viewer_editor.c                   */
/* ------------------------------------------------------------------ */

extern editor_state_t g_editor;

#endif /* EDITOR_TYPES_H */
