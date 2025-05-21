// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "buffer.h"

typedef struct UiNode UiNode;
typedef struct UiContext UiContext;

typedef struct UiFloatSpec {
    // Specifies the origin of the container relative to the container size. [0, 1]
    f32 gravity_x;
    f32 gravity_y;
    // Specifies an offset from the origin in cells.
    CoordType offset_x;
    CoordType offset_y;
} UiFloatSpec;

typedef struct UiGridColumns {
    usize count;
    // `data` is a pointer to an array of `CoordType` values.
    // Positive values indicate an absolute width in columns.
    // Negative values indicate a fraction of the remaining width, similar to the "fr" unit in CSS.
    CoordType* widths;
} UiGridColumns;

typedef enum UiInputType {
    UI_INPUT_NONE,
    UI_INPUT_RESIZE,
    UI_INPUT_TEXT,
    UI_INPUT_KEYBOARD,
    UI_INPUT_MOUSE,
} UiInputType;

typedef enum KeyboardModifier {
    KEYBOARD_MODIFIER_NONE = 0x00000000,
    KEYBOARD_MODIFIER_CTRL = 0x01000000,
    KEYBOARD_MODIFIER_ALT = 0x02000000,
    KEYBOARD_MODIFIER_SHIFT = 0x04000000,
} KeyboardModifier;

typedef enum UiInputKeyboardKey {
    VK_NULL = 0x00,
    VK_BACK = 0x08,
    VK_TAB = 0x09,
    VK_RETURN = 0x0D,
    VK_ESCAPE = 0x1B,
    VK_SPACE = 0x20,
    VK_PRIOR = 0x21,
    VK_NEXT = 0x22,

    VK_END = 0x23,
    VK_HOME = 0x24,

    VK_LEFT = 0x25,
    VK_UP = 0x26,
    VK_RIGHT = 0x27,
    VK_DOWN = 0x28,

    VK_INSERT = 0x2D,
    VK_DELETE = 0x2E,

    VK_A = 'A',
    VK_B = 'B',
    VK_C = 'C',
    VK_D = 'D',
    VK_E = 'E',
    VK_F = 'F',
    VK_G = 'G',
    VK_H = 'H',
    VK_I = 'I',
    VK_J = 'J',
    VK_K = 'K',
    VK_L = 'L',
    VK_M = 'M',
    VK_N = 'N',
    VK_O = 'O',
    VK_P = 'P',
    VK_Q = 'Q',
    VK_R = 'R',
    VK_S = 'S',
    VK_T = 'T',
    VK_U = 'U',
    VK_V = 'V',
    VK_W = 'W',
    VK_X = 'X',
    VK_Y = 'Y',
    VK_Z = 'Z',

    VK_NUMPAD0 = 0x60,
    VK_NUMPAD1 = 0x61,
    VK_NUMPAD2 = 0x62,
    VK_NUMPAD3 = 0x63,
    VK_NUMPAD4 = 0x64,
    VK_NUMPAD5 = 0x65,
    VK_NUMPAD6 = 0x66,
    VK_NUMPAD7 = 0x67,
    VK_NUMPAD8 = 0x68,
    VK_NUMPAD9 = 0x69,
    VK_MULTIPLY = 0x6A,
    VK_ADD = 0x6B,
    VK_SEPARATOR = 0x6C,
    VK_SUBTRACT = 0x6D,
    VK_DECIMAL = 0x6E,
    VK_DIVIDE = 0x6F,

    VK_F1 = 0x70,
    VK_F2 = 0x71,
    VK_F3 = 0x72,
    VK_F4 = 0x73,
    VK_F5 = 0x74,
    VK_F6 = 0x75,
    VK_F7 = 0x76,
    VK_F8 = 0x77,
    VK_F9 = 0x78,
    VK_F10 = 0x79,
    VK_F11 = 0x7A,
    VK_F12 = 0x7B,
    VK_F13 = 0x7C,
    VK_F14 = 0x7D,
    VK_F15 = 0x7E,
    VK_F16 = 0x7F,
    VK_F17 = 0x80,
    VK_F18 = 0x81,
    VK_F19 = 0x82,
    VK_F20 = 0x83,
    VK_F21 = 0x84,
    VK_F22 = 0x85,
    VK_F23 = 0x86,
    VK_F24 = 0x87,
} UiInputKeyboardKey;

typedef struct UiInputKeyboard {
    UiInputKeyboardKey key;
    KeyboardModifier modifiers;
} UiInputKeyboard;

typedef enum MouseAction {
    MOUSE_ACTION_NONE,
    MOUSE_ACTION_RELEASE,
    MOUSE_ACTION_LEFT,
    MOUSE_ACTION_MIDDLE,
    MOUSE_ACTION_RIGHT,
    MOUSE_ACTION_SCROLL,
} MouseAction;

typedef struct UiInputMouse {
    MouseAction action;
    KeyboardModifier modifier;
    Point position;
    Point scroll;
} UiInputMouse;

typedef struct UiInput {
    UiInputType type;

    union {
        Size resize;
        s8 text;
        UiInputKeyboard keyboard;
        UiInputMouse mouse;
    };
} UiInput;

struct UiContext {
    Arena* arena;
    Arena* arena_prev;

    u32 indexed_colors[16];

    Size size;
    s8 input_text;
    UiInputKeyboard input_keyboard;
    MouseAction input_mouse_action;
    Point input_mouse_position;
    Point input_scroll_delta;
    bool input_consumed;

    u64 focused_item_id;

    UiNode* root_first;
    UiNode* root_last;
    UiNode* attr_node;
    UiNode* parent;
    usize node_count;
    bool autofocus_next;

    UiNode** node_map;
    usize node_map_shift;
    usize node_map_mask;
    bool finalized;
};

UiContext* ui_root_create();
void ui_root_setup_indexed_colors(UiContext* ctx, u32 colors[16]);
UiContext* ui_root_reset(UiContext* prev, UiInput input);
s8 ui_root_render(UiContext* ctx);

Size ui_root_get_size(UiContext* ctx);

void ui_consume_input(UiContext* ctx);
bool ui_consume_shortcut(UiContext* ctx, i32 shortcut);
MouseAction ui_input_mouse(UiContext* ctx);
s8 ui_input_text(UiContext* ctx);
UiInputKeyboard ui_input_keyboard(UiContext* ctx);

void ui_container_begin(UiContext* ctx, u64 id);
void ui_container_begin_named(UiContext* ctx, s8 id);
void ui_container_end(UiContext* ctx);

void ui_attr_float(UiContext* ctx, UiFloatSpec spec);
void ui_attr_border(UiContext* ctx);
void ui_attr_padding(UiContext* ctx, Rect padding);
void ui_attr_grid_columns(UiContext* ctx, UiGridColumns columns);
void ui_attr_background_rgba(UiContext* ctx, u32 bg);
void ui_attr_foreground_rgba(UiContext* ctx, u32 fg);
void ui_attr_background_indexed(UiContext* ctx, u32 bg);
void ui_attr_foreground_indexed(UiContext* ctx, u32 fg);

void ui_focus_next_by_default(UiContext* ctx);
bool ui_is_hovering(UiContext* ctx);
bool ui_has_focus(UiContext* ctx);
bool ui_was_clicked(UiContext* ctx);

void ui_label(UiContext* ctx, s8 text);
void ui_styled_label_begin(UiContext* ctx, s8 id);
void ui_styled_label_set_foreground_indexed(UiContext* ctx, u32 fg);
void ui_styled_label_add_text(UiContext* ctx, s8 text);
void ui_styled_label_end(UiContext* ctx);
bool ui_button(UiContext* ctx, s8 text);
bool ui_editline(UiContext* ctx, s8 name);
void ui_textarea(UiContext* ctx, TextBuffer* tb, Size intrinsic_size);

void ui_scrollarea_begin(UiContext* ctx, s8 name, Size intrinsic_size);
void ui_scrollarea_end(UiContext* ctx);

void ui_menubar_begin(UiContext* ctx);
bool ui_menubar_menu_begin(UiContext* ctx, s8 text, c8 accelerator);
bool ui_menubar_menu_item(UiContext* ctx, s8 text, c8 accelerator, i32 shortcut);
void ui_menubar_menu_end(UiContext* ctx);
void ui_menubar_end(UiContext* ctx);
