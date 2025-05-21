// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "tui.h"

#include "icu.h"
#include "os.h"
#include "ucd.h"

#include <icu.h>
#include <math.h>

#pragma comment(lib, "icuuc.lib")

typedef enum UiNodeType {
    UI_NODE_CONTAINER,
    UI_NODE_TEXT,
    UI_NODE_TEXTAREA,
    UI_NODE_SCROLLAREA,
} UiContentType;

typedef struct CoordList {
    CoordType* beg;
    usize len;
    usize cap;
} CoordList;

typedef struct Attributes {
    UiFloatSpec float_spec;
    Rect padding;
    CoordList grid_columns;
    u32 bg;
    u32 fg;
    bool floating;
    bool bordered;
} Attributes;

typedef struct StyledTextChunk {
    struct StyledTextChunk* next;
    s8 text;
    u32 fg;
} StyledTextChunk;

typedef struct StyledTextList {
    StyledTextChunk* first;
    StyledTextChunk* last;
} StyledTextList;

struct UiNode {
    UiNode* stack_parent;

    u64 id;
    UiNode* parent;
    UiNode* child_first;
    UiNode* child_last;
    UiNode* sibling_prev;
    UiNode* sibling_next;

    Attributes attributes;

    UiContentType content_type;

    union {
        StyledTextList text;
        TextBuffer* textarea;
        Point scrollarea; // scroll offset
    } content;

    Size intrinsic_size;
    bool intrinsic_size_set;
    Rect outer;         // in screen-space, calculated during layout
    Rect inner;         // in screen-space, calculated during layout
    Rect outer_clipped; // in screen-space, calculated during layout, restricted to the viewport
    Rect inner_clipped; // in screen-space, calculated during layout, restricted to the viewport
};

typedef struct RenderContext {
    Arena* arena;
    Rect bounds;
    u64 focused_item_id;

    s8* lines;
    u32* bg_bitmap;
    u32* fg_bitmap;

    Point cursor;
    bool cursor_overtype;
} RenderContext;

static void ui_node_append_child(UiNode* parent, UiNode* child)
{
    // The child node is supposed to not be part of any tree.
    assert(child->sibling_prev == NULL && child->sibling_next == NULL);

    child->parent = parent;
    child->sibling_prev = parent->child_last;

    if (parent->child_last) {
        parent->child_last->sibling_next = child;
    }
    if (!parent->child_first) {
        parent->child_first = child;
    }
    parent->child_last = child;
}

static void ui_node_remove(UiNode* child)
{
    UiNode* parent = child->parent;
    assert(parent != NULL);

    if (child->sibling_prev) {
        child->sibling_prev->sibling_next = child->sibling_next;
    }
    if (child->sibling_next) {
        child->sibling_next->sibling_prev = child->sibling_prev;
    }
    if (parent->child_first == child) {
        parent->child_first = child->sibling_next;
    }
    if (parent->child_last == child) {
        parent->child_last = child->sibling_prev;
    }

    child->parent = NULL;
    child->sibling_prev = NULL;
    child->sibling_next = NULL;
}

static Rect outer_to_inner(UiNode* node, Rect outer)
{
    const CoordType l = node->attributes.bordered;
    const CoordType t = node->attributes.bordered;
    const CoordType r = node->attributes.bordered | (node->content_type == UI_NODE_SCROLLAREA);
    const CoordType b = node->attributes.bordered;
    outer.left += node->attributes.padding.left + l;
    outer.top += node->attributes.padding.top + t;
    outer.right -= node->attributes.padding.right + r;
    outer.bottom -= node->attributes.padding.bottom + b;
    return outer;
}

static Size intrinsic_to_outer(UiNode* node)
{
    const CoordType l = node->attributes.bordered;
    const CoordType t = node->attributes.bordered;
    const CoordType r = node->attributes.bordered | (node->content_type == UI_NODE_SCROLLAREA);
    const CoordType b = node->attributes.bordered;
    Size size = node->intrinsic_size;
    size.width += node->attributes.padding.left + node->attributes.padding.right + l + r;
    size.height += node->attributes.padding.top + node->attributes.padding.bottom + t + b;
    return size;
}

UiContext* ui_root_create()
{
    Arena* arena = arena_create(64 * 1024 * 1024);
    Arena* arena_prev = arena_create(64 * 1024 * 1024);
    UiContext* ctx = arena_zalloc(arena, UiContext);
    ctx->arena = arena;
    ctx->arena_prev = arena_prev;
    ctx->input_mouse_position = (Point){-1, -1};
    ctx->focused_item_id = 0x0123456789abcdef;
    return ctx;
}

void ui_root_setup_indexed_colors(UiContext* ctx, u32 colors[16])
{
    memcpy(ctx->indexed_colors, colors, sizeof(ctx->indexed_colors));
}

static CoordType ui_replace_text(Arena* arena, s8* line, CoordType x1, CoordType x2, s8 text)
{
    UcdMeasurement res_new = ucd_measure_forward(text, 0, (Point){x1, 0}, x2, -1, NULL);
    UcdMeasurement res_old_beg = ucd_measure_forward(*line, 0, (Point){0, 0}, x1, -1, NULL);
    UcdMeasurement res_old_end = ucd_measure_forward(*line, res_old_beg.offset, (Point){x1, 0}, res_new.pos.x, -1, NULL);

    s8 str_new = s8_slice(text, 0, res_new.offset);

    if (x1 > res_old_beg.pos.x || res_new.pos.x > res_old_end.pos.x) {
        assert(false); // TODO: test this code
        s8 s = {};
        // Pad the line until we reach `x`.
        if (x1 > res_old_beg.pos.x) {
            s8_append_repeat(arena, &s, ' ', x1 - res_old_beg.pos.x);
        }
        s8_append(arena, &s, str_new);
        // If we had to stop short when finding the end, because the end offset
        // is on top of a wide glyph we also have to pad that with a whitespace.
        if (res_new.pos.x > res_old_end.pos.x) {
            s8_append_repeat(arena, &s, ' ', res_new.pos.x - res_old_end.pos.x);
        }
        str_new = s;
    }

    s8_replace(arena, line, res_old_beg.offset, res_old_end.offset, str_new);
    return res_new.pos.x;
}

static void ui_compute_intrinsic_size(UiNode* node)
{
    if (node->intrinsic_size_set) {
        return;
    }

    CoordList columns = node->attributes.grid_columns;
    CoordType default_width = -1;
    if (columns.len == 0) {
        columns.beg = &default_width;
        columns.len = 1;
        columns.cap = 1;
    }

    Size row_size = {};
    Size total_size = {};
    usize column = 0;

    for (UiNode* child = node->child_first; child; child = child->sibling_next) {
        ui_compute_intrinsic_size(child);

        Size size = intrinsic_to_outer(child);
        size.width = max(size.width, columns.beg[column]);

        row_size.width += size.width;
        row_size.height = max(row_size.height, size.height);

        column += 1;
        if (column >= columns.len) {
            total_size.width = max(total_size.width, row_size.width);
            total_size.height += row_size.height;
            row_size = (Size){};
            column = 0;
        }
    }

    total_size.width = max(total_size.width, row_size.width);
    total_size.height += row_size.height;

    node->intrinsic_size = total_size;
    node->intrinsic_size_set = true;
}

static void ui_layout_children(UiContext* ctx, UiNode* node, Rect clip)
{
    if (!node->child_first) {
        return;
    }

    if (rect_is_empty(node->inner)) {
        return;
    }

    if (node->content_type == UI_NODE_SCROLLAREA) {
        UiNode* child = node->child_first;

        // scrollarea size
        CoordType sx = node->inner.right - node->inner.left;
        CoordType sy = node->inner.bottom - node->inner.top;
        // content size
        CoordType cx = max(child->intrinsic_size.width, sx);
        CoordType cy = max(child->intrinsic_size.height, sy);
        // offset
        CoordType ox = clamp(node->content.scrollarea.x, 0, cx - sx);
        CoordType oy = clamp(node->content.scrollarea.y, 0, cy - sy);

        child->outer = node->inner;
        child->outer.left = node->inner.left - ox;
        child->outer.top = node->inner.top - oy;
        child->outer.right = child->outer.left + cx;
        child->outer.bottom = child->outer.top + cy;
        child->inner = outer_to_inner(child, child->outer);
        child->outer_clipped = rect_intersect(child->outer, node->inner_clipped);
        child->inner_clipped = rect_intersect(child->inner, node->inner_clipped);

        node->content.scrollarea.x = ox;
        node->content.scrollarea.y = oy;
        return;
    }

    CoordList columns = node->attributes.grid_columns;
    CoordType default_width = -1;
    if (columns.len == 0) {
        columns.beg = &default_width;
        columns.len = 1;
        columns.cap = 1;
    }

    // TODO: We can skip this for nodes without a grid layout.
    CoordType* intrinsic_column_width = arena_zallocn(ctx->arena, CoordType, columns.len);
    UiNode* child;
    usize column;

    for (child = node->child_first, column = 0; child; child = child->sibling_next) {
        Size size = intrinsic_to_outer(child);
        intrinsic_column_width[column] = max(intrinsic_column_width[column], size.width);

        column += 1;
        if (column >= columns.len) {
            column = 0;
        }
    }

    {
        CoordType total_abs_widths = 0;
        CoordType total_fr_widths = 0;

        for (usize i = 0; i < columns.len; i++) {
            total_abs_widths += max(columns.beg[i], 0);
            total_fr_widths += min(columns.beg[i], 0);
        }

        f64 fr_scale = 0;
        if (total_fr_widths < 0) {
            CoordType remaining = (node->inner.right - node->inner.left) - total_abs_widths;
            remaining = max(remaining, 0);
            // `unit` will be negative and invert the `grid_widths` each to a positive value.
            fr_scale = (f64)remaining / (f64)total_fr_widths;
        }

        for (usize i = 0; i < columns.len; i++) {
            CoordType width = columns.beg[i];
            if (width <= 0) {
                CoordType adjusted = intrinsic_column_width[i];
                if (width < 0) {
                    CoordType fr = (CoordType)((f64)width * fr_scale + 0.5);
                    adjusted = max(adjusted, fr);
                }
                columns.beg[i] = adjusted;
            }
        }
    }

    CoordType x = node->inner.left;
    CoordType y = node->inner.top;
    CoordType row_height = 0;

    for (child = node->child_first, column = 0; child; child = child->sibling_next) {
        Size size = intrinsic_to_outer(child);
        size.width = columns.beg[column];

        child->outer.left = x;
        child->outer.top = y;
        child->outer.right = x + size.width;
        child->outer.bottom = y + size.height;
        child->outer = rect_intersect(child->outer, node->inner);
        child->inner = outer_to_inner(child, child->outer);
        child->outer_clipped = rect_intersect(child->outer, clip);
        child->inner_clipped = rect_intersect(child->inner, clip);

        x += size.width;
        row_height = max(row_height, size.height);
        column += 1;

        if (column >= columns.len) {
            x = node->inner.left;
            y += row_height;
            row_height = 0;
            column = 0;
        }
    }

    for (child = node->child_first; child; child = child->sibling_next) {
        ui_layout_children(ctx, child, clip);
    }
}

static void ui_root_finalize(UiContext* ctx)
{
    if (ctx->finalized) {
        return;
    }

    {
        u32 width = os_bit_width_u64(4 * ctx->node_count);
        u32 shift = 64 - width;
        usize slots = (usize)1 << width;
        usize mask = slots - 1;
        UiNode** node_map = arena_zallocn(ctx->arena, UiNode*, slots);

        ctx->node_map = node_map;
        ctx->node_map_shift = shift;
        ctx->node_map_mask = mask;

        for (UiNode* root = ctx->root_first; root; root = root->sibling_next) {
            for (UiNode* node = root;;) {
                for (usize slot = node->id >> shift;; slot = (slot + 1) & mask) {
                    if (!node_map[slot]) {
                        node_map[slot] = node;
                        break;
                    }
                }

                if (node->child_first) {
                    node = node->child_first;
                } else {
                    while (!node->sibling_next) {
                        node = node->parent;
                        if (node == root) {
                            goto next_root;
                        }
                    }
                    node = node->sibling_next;
                }
            }
        next_root:;
        }
    }

    for (UiNode* child = ctx->root_first; child; child = child->sibling_next) {
        ui_compute_intrinsic_size(child);
    }

    if (ctx->root_first) {
        UiNode* root = ctx->root_first;

        root->outer.left = 0;
        root->outer.top = 0;
        root->outer.right = ctx->size.width;
        root->outer.bottom = ctx->size.height;
        root->inner = outer_to_inner(root, root->outer);
        root->outer_clipped = root->outer;
        root->inner_clipped = root->inner;

        ui_layout_children(ctx, root, root->outer);

        UiNode* child = root;
        while ((child = child->sibling_next)) {
            CoordType x = child->parent->outer.left;
            CoordType y = child->parent->outer.top;
            Size size = intrinsic_to_outer(child);

            x += child->attributes.float_spec.offset_x;
            y += child->attributes.float_spec.offset_y;
            x -= (CoordType)(child->attributes.float_spec.gravity_x * (f32)size.width + 0.5f);
            y -= (CoordType)(child->attributes.float_spec.gravity_y * (f32)size.height + 0.5f);

            child->outer.left = x;
            child->outer.top = y;
            child->outer.right = x + size.width;
            child->outer.bottom = y + size.height;
            child->inner = outer_to_inner(child, child->outer);
            child->outer_clipped = rect_intersect(child->outer, root->inner_clipped);
            child->inner_clipped = rect_intersect(child->outer, root->inner_clipped);

            ui_layout_children(ctx, child, child->outer);
        }
    }

    ctx->finalized = true;
}

UiContext* ui_root_reset(UiContext* prev, UiInput input)
{
    ui_root_finalize(prev);

    Arena* arena = prev->arena_prev;
    arena_reset(arena);

    UiContext* ctx = arena_zalloc(arena, UiContext);
    UiNode* root_node = arena_zalloc(arena, UiNode);

    ctx->arena = arena;
    ctx->arena_prev = prev->arena;

    memcpy(ctx->indexed_colors, prev->indexed_colors, sizeof(ctx->indexed_colors));

    ctx->size = prev->size;
    ctx->input_mouse_position = prev->input_mouse_position;

    ctx->focused_item_id = prev->focused_item_id;

    ctx->root_first = root_node;
    ctx->root_last = root_node;
    ctx->attr_node = root_node;
    ctx->parent = root_node;
    ctx->node_count = 1;

    ctx->node_map = prev->node_map;
    ctx->node_map_shift = prev->node_map_shift;
    ctx->node_map_mask = prev->node_map_mask;

    root_node->id = 0x0123456789abcdef;
    root_node->attributes.bg = ctx->indexed_colors[0];
    root_node->attributes.fg = ctx->indexed_colors[15];

    switch (input.type) {
    case UI_INPUT_NONE:
        ctx->input_mouse_action = prev->input_mouse_action;
        ctx->input_consumed = true;
        break;
    case UI_INPUT_RESIZE:
        assert(input.resize.width > 0 && input.resize.height > 0);
        assert(input.resize.width < 32768 && input.resize.height < 32768);
        ctx->size = input.resize;
        break;
    case UI_INPUT_TEXT:
        ctx->input_text = input.text;
        break;
    case UI_INPUT_KEYBOARD:
        ctx->input_keyboard = input.keyboard;
        break;
    case UI_INPUT_MOUSE: {
        const Point pos = input.mouse.position;
        UiNode* best = NULL;

        for (UiNode* root = prev->root_first; root; root = root->sibling_next) {
            for (UiNode* node = root;;) {
                if (rect_contains(node->inner_clipped, pos)) {
                    best = node;
                }

                if (node->child_first) {
                    node = node->child_first;
                } else {
                    while (!node->sibling_next) {
                        node = node->parent;
                        if (node == root) {
                            goto next;
                        }
                    }
                    node = node->sibling_next;
                }
            }
        next:;
        }

        MouseAction mouse_action = input.mouse.action;
        if (prev->input_mouse_action >= MOUSE_ACTION_LEFT && prev->input_mouse_action <= MOUSE_ACTION_RIGHT && mouse_action == MOUSE_ACTION_NONE) {
            mouse_action = MOUSE_ACTION_RELEASE;
        }

        ctx->input_mouse_action = mouse_action;
        ctx->input_mouse_position = pos;
        ctx->input_scroll_delta = input.mouse.scroll;

        if (best) {
            if (mouse_action == MOUSE_ACTION_LEFT) {
                ctx->focused_item_id = best->id;
            }
        }
        break;
    }
    }

    return ctx;
}

// Performs gamma-correct alpha blending.
attribute_forceinline float srgb_to_linear(u32 c)
{
    const f32 fc = c / 255.0f;
    return (fc <= 0.04045f) ? (fc / 12.92f) : powf((fc + 0.055f) / 1.055f, 2.4f);
}

attribute_forceinline u32 linear_to_srgb(float c)
{
    return (u32)((c <= 0.0031308f) ? (c * 12.92f * 255.0f) : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f) * 255.0f);
}

u32 alpha_blend(u32 dst, u32 src)
{
    float src_r = srgb_to_linear(src & 0xff);
    float src_g = srgb_to_linear((src >> 8) & 0xff);
    float src_b = srgb_to_linear((src >> 16) & 0xff);
    float src_a = (src >> 24) / 255.0f;

    float dst_r = srgb_to_linear(dst & 0xff);
    float dst_g = srgb_to_linear((dst >> 8) & 0xff);
    float dst_b = srgb_to_linear((dst >> 16) & 0xff);
    float dst_a = (dst >> 24) / 255.0f;

    float out_a = src_a + dst_a * (1.0f - src_a);
    float out_r = (src_r * src_a + dst_r * dst_a * (1.0f - src_a)) / out_a;
    float out_g = (src_g * src_a + dst_g * dst_a * (1.0f - src_a)) / out_a;
    float out_b = (src_b * src_a + dst_b * dst_a * (1.0f - src_a)) / out_a;

    return ((u32)(out_a * 255.0f) << 24) | (linear_to_srgb(out_b) << 16) | (linear_to_srgb(out_g) << 8) | linear_to_srgb(out_r);
}

void alpha_blend_rect(u32* dst, u32 src, Rect rect, CoordType stride)
{
    if ((src & 0xff000000) == 0xff000000) {
        for (CoordType y = rect.top; y < rect.bottom; y++) {
            CoordType x = y * stride + rect.left;
            CoordType end = y * stride + rect.right;
            for (; x < end; x++) {
                dst[x] = src;
            }
        }
    } else if ((src & 0xff000000) != 0x00000000) {
        // TODO: Cache the alpha blend calculation by accumulating runs of the same color.
        for (CoordType y = rect.top; y < rect.bottom; y++) {
            CoordType x = y * stride + rect.left;
            CoordType end = y * stride + rect.right;
            for (; x < end; x++) {
                dst[x] = alpha_blend(dst[x], src);
            }
        }
    }
}

// TODO: pass clip rect from parent for scrollarea
static void ui_render_node(RenderContext* ctx, UiNode* node)
{
    Rect outer = node->outer;
    Rect outer_clamped = node->outer_clipped;
    if (rect_is_empty(outer_clamped)) {
        return;
    }

    if (node->attributes.bordered) {
        // ┌────┐
        {
            s8 fill = {};
            s8_append(ctx->arena, &fill, S("┌"));
            s8_append_repeat_string(ctx->arena, &fill, S("─"), outer_clamped.right - outer_clamped.left - 2);
            s8_append(ctx->arena, &fill, S("┐"));
            ui_replace_text(ctx->arena, &ctx->lines[outer_clamped.top], outer_clamped.left, outer_clamped.right, fill);
        }

        // │    │
        {
            s8 fill = {};
            s8_append(ctx->arena, &fill, S("│"));
            s8_append_repeat(ctx->arena, &fill, ' ', outer_clamped.right - outer_clamped.left - 2);
            s8_append(ctx->arena, &fill, S("│"));

            for (CoordType y = outer_clamped.top + 1; y < outer_clamped.bottom - 1; y++) {
                ui_replace_text(ctx->arena, &ctx->lines[y], outer_clamped.left, outer_clamped.right, fill);
            }
        }

        // └────┘
        {
            s8 fill = {};
            s8_append(ctx->arena, &fill, S("└"));
            s8_append_repeat_string(ctx->arena, &fill, S("─"), outer_clamped.right - outer_clamped.left - 2);
            s8_append(ctx->arena, &fill, S("┘"));
            ui_replace_text(ctx->arena, &ctx->lines[outer_clamped.bottom - 1], outer_clamped.left, outer_clamped.right, fill);
        }
    } else if (node->attributes.floating) {
        s8 fill = {};
        s8_append_repeat(ctx->arena, &fill, ' ', outer_clamped.right - outer_clamped.left);

        for (CoordType y = outer_clamped.top; y < outer_clamped.bottom; y++) {
            ui_replace_text(ctx->arena, &ctx->lines[y], outer_clamped.left, outer_clamped.right, fill);
        }
    }

    if (node->content_type == UI_NODE_SCROLLAREA) {
        CoordType outer_height = outer_clamped.bottom - outer_clamped.top;
        CoordType inner_height = node->child_first->intrinsic_size.height;
        CoordType scroll_offset = min(node->inner.top - node->child_first->outer.top, inner_height);
        CoordType track_height = max((CoordType)((f64)outer_height / (f64)inner_height * (f64)outer_height + 0.5), 1);

        CoordType track_bottom = (CoordType)((f64)(scroll_offset + outer_height) / (f64)inner_height * (f64)outer_height + 0.5);
        track_bottom = max(track_bottom, track_height);
        track_bottom = min(track_bottom, outer_height);

        CoordType track_top = min(track_top, track_bottom - track_height);

        track_top += outer_clamped.top;
        track_bottom += outer_clamped.top;

        for (CoordType y = outer_clamped.top; y < outer_clamped.bottom; y++) {
            s8 text = y >= track_top && y < track_bottom ? S("█") : S("░");
            ui_replace_text(ctx->arena, &ctx->lines[y], outer_clamped.right - 1, outer_clamped.right, text);
        }
    }

    if (node->attributes.bg & 0xff000000) {
        alpha_blend_rect(ctx->bg_bitmap, node->attributes.bg, outer_clamped, ctx->bounds.right);
    }

    if (node->attributes.fg & 0xff000000) {
        alpha_blend_rect(ctx->fg_bitmap, node->attributes.fg, outer_clamped, ctx->bounds.right);
    }

    Rect inner = node->inner;
    Rect inner_clamped = node->inner_clipped;
    if (rect_is_empty(inner_clamped)) {
        return;
    }

    switch (node->content_type) {
    case UI_NODE_TEXT: {
        if (!rect_is_empty(inner_clamped)) {
            CoordType bitmap_offset = inner_clamped.top * ctx->bounds.right;
            CoordType origin_x = inner.left;

            for (StyledTextChunk* chunk = node->content.text.first; chunk; chunk = chunk->next) {
                s8 text = chunk->text;

                // If the text is clipped on the left side, we need to skip parts of
                // the text (or entire iterations) until it's inside the clip bounds.
                if (origin_x < inner_clamped.left) {
                    UcdMeasurement m = ucd_measure_forward(text, 0, (Point){origin_x, 0}, inner_clamped.left, COORD_TYPE_MAX, NULL);
                    text = s8_slice(text, m.offset, USIZE_MAX);
                    if (origin_x < inner_clamped.left) {
                        continue;
                    }
                }

                CoordType end_x = ui_replace_text(ctx->arena, &ctx->lines[inner_clamped.top], origin_x, inner_clamped.right, text);

                u32 fg = chunk->fg;
                if (fg) {
                    CoordType x = bitmap_offset + origin_x;
                    CoordType end = bitmap_offset + end_x;
                    for (; x < end; x++) {
                        ctx->fg_bitmap[x] = fg;
                    }
                }

                origin_x = end_x;
            }
            break;
        }
    case UI_NODE_TEXTAREA: {
        TextBuffer* tb = node->content.textarea;
        CoordType gutter_width = (CoordType)u64_log10(tb->stats.lines) + 2;
        CoordType width = inner_clamped.right - inner_clamped.left - gutter_width;
        CoordType scroll_x = outer_clamped.left - outer.left;
        CoordType scroll_y = outer_clamped.top - outer.top;
        CoordType offset_y = -outer.top;
        TextBufferCursor cursor_backup = tb->cursor;
        s8 gutter = {};

#if 1
        gutter_width = 0;
        width = inner_clamped.right - inner_clamped.left;
        for (CoordType y = inner_clamped.top; y < inner_clamped.bottom; ++y) {
            usize off_beg = text_buffer_cursor_move_to_visual(tb, (Point){scroll_x, offset_y + y});
            usize off_end = text_buffer_cursor_move_to_visual(tb, (Point){scroll_x + width, offset_y + y});

            s8 line = {};
            s8_reserve(ctx->arena, &line, off_end - off_beg);
            line.len = text_buffer_extract(tb, off_beg, off_end, line.beg);

            ui_replace_text(ctx->arena, &ctx->lines[y], inner_clamped.left, inner_clamped.right, line);
        }
#else
        CoordType tb_x = 0;
        CoordType tb_y = scroll_y;
        CoordType y = inner_clamped.top;
        CoordType line_number_last = -1;
        while (y < inner_clamped.bottom) {
            usize off_beg = text_buffer_cursor_move_to_visual(tb, (Point){tb_x, tb_y});
            usize off_end = text_buffer_cursor_move_to_visual(tb, (Point){tb_x + width, tb_y});
            usize line_number = tb_y + 1;
            CoordType line_width = u64_log10(line_number) + 2;

            gutter.len = 0;
            if (line_number == line_number_last) {
                s8_append_repeat(ctx->arena, &gutter, ' ', gutter_width);
            } else {
                s8_append_repeat(ctx->arena, &gutter, ' ', gutter_width - line_width);
                s8_append_u64(ctx->arena, &gutter, line_number);
            }
            ui_replace_text(ctx->arena, &ctx->lines[y], inner_clamped.left, inner_clamped.right, gutter);

            s8 line = {};
            s8_reserve(ctx->arena, &line, off_end - off_beg);
            line.len = text_buffer_extract(tb, off_beg, off_end, line.beg);

            UcdMeasurement line_break;
            ucd_measure_forward(line, 0, (Point){tb_x, tb_y}, width, -1, &line_break);
            if (line_break.offset == line.len) {
                tb_y += 1;
                tb_x = 0;
            } else {
                line = s8_slice(line, 0, line_break.offset);
                tb_x = line_break.pos.x;
                tb_y = line_break.pos.y;
            }

            ui_replace_text(ctx->arena, &ctx->lines[y], inner_clamped.left + gutter_width, inner_clamped.right, line);

            y += 1;
            line_number_last = line_number;
        }

        Rect gutter_rect = inner_clamped;
        gutter_rect.right = gutter_rect.left + gutter_width;
        alpha_blend_rect(ctx->fg_bitmap, 0xdf000000, gutter_rect, ctx->bounds.right);
#endif

        if (tb->selection.state >= TEXT_BUFFER_SELECTION_ACTIVE) {
            Point beg = {
                .x = tb->selection.beg.x - scroll_x + gutter_width,
                .y = tb->selection.beg.y - offset_y,
            };
            Point end = {
                .x = tb->selection.end.x - scroll_x + gutter_width,
                .y = tb->selection.end.y - offset_y,
            };

            // `beg` is just the position where the drag started. It may be past `end`.
            // We need to swap them if necessary.
            if (beg.y > end.y || (beg.y == end.y && beg.x > end.x)) {
                Point tmp = beg;
                beg = end;
                end = tmp;
            }

            // Both may be outside the inner_clamped viewport.
            // The line where the selection started/ended may only be partially filled with fg/bg.
            // All other rows are fully filled in.

            if (beg.y < inner_clamped.top) {
                beg.y = inner_clamped.top;
            }

            if (end.y >= inner_clamped.bottom) {
                end.y = inner_clamped.bottom - 1;
            }

            for (CoordType y = beg.y; y <= end.y; y++) {
                CoordType x1 = y == beg.y ? beg.x : inner_clamped.left;
                CoordType x2 = y == end.y ? end.x : inner_clamped.right;
                for (CoordType x = x1; x < x2; x++) {
                    ctx->bg_bitmap[y * ctx->bounds.right + x] = 15;
                    ctx->fg_bitmap[y * ctx->bounds.right + x] = 0;
                }
            }
        }

        if (node->id == ctx->focused_item_id) {
            Point cursor = {
                .x = cursor_backup.logical_pos.x - scroll_x + gutter_width,
                .y = cursor_backup.logical_pos.y - offset_y,
            };
            if (rect_contains(inner_clamped, cursor)) {
                ctx->cursor = cursor;
                ctx->cursor_overtype = tb->overtype;
            }
        }

        tb->cursor = cursor_backup;
        break;
    }
    default:
        break;
    }
    }

    for (UiNode* child = node->child_first; child; child = child->sibling_next) {
        ui_render_node(ctx, child);
    }
}

s8 ui_root_render(UiContext* ctx)
{
    ui_root_finalize(ctx);

    s8 result = {};
    s8_append(ctx->arena, &result, S("\x1b[H"));

    s8* lines = arena_mallocn(ctx->arena, s8, ctx->size.height);
    for (CoordType y = 0; y < ctx->size.height; y++) {
        usize len = ctx->size.width;
        usize cap = len * 2;
        c8* s = arena_mallocn(ctx->arena, c8, cap);
        memset(s, ' ', len);
        lines[y] = (s8){s, len, cap};
    }

    usize area = ctx->size.width * ctx->size.height;
    u32* bg_bitmap = arena_mallocn(ctx->arena, u32, area);
    u32* fg_bitmap = arena_mallocn(ctx->arena, u32, area);
    for (usize i = 0; i < area; i++) {
        bg_bitmap[i] = 0;
    }
    for (usize i = 0; i < area; i++) {
        fg_bitmap[i] = 15;
    }

    RenderContext rctx = {
        .arena = ctx->arena,
        .bounds = (Rect){0, 0, ctx->size.width, ctx->size.height},
        .focused_item_id = ctx->focused_item_id,

        .lines = lines,
        .bg_bitmap = bg_bitmap,
        .fg_bitmap = fg_bitmap,

        .cursor = (Point){-1, -1},
    };
    for (UiNode* child = ctx->root_first; child; child = child->sibling_next) {
        ui_render_node(&rctx, child);
    }

    u32 last_bg = bg_bitmap[0];
    u32 last_fg = fg_bitmap[0];
    // Invert the colors to force a color change on the first cell.
    last_bg ^= 1;
    last_fg ^= 1;

    for (CoordType y = 0; y < ctx->size.height; y++) {
        if (y != 0) {
            s8_append(ctx->arena, &result, S("\r\n"));
        }

        usize last_flush_offset = 0;
        CoordType last_flush_column = 0;

        for (CoordType x = 0; x < ctx->size.width; x++) {
            u32 bg = bg_bitmap[y * ctx->size.width + x];
            u32 fg = fg_bitmap[y * ctx->size.width + x];
            if (bg == last_bg && fg == last_fg) {
                continue;
            }

            if (x) {
                UcdMeasurement m = ucd_measure_forward(lines[y], last_flush_offset, (Point){last_flush_column, 0}, x, -1, NULL);
                s8_append(ctx->arena, &result, s8_slice(lines[y], last_flush_offset, m.offset));
                last_flush_offset = m.offset;
                last_flush_column = x;
            }

            if (last_bg != bg) {
                last_bg = bg;
                if (bg < 8) {
                    s8_append_fmt(ctx->arena, &result, "\x1b[", 40 + bg, "m");
                } else if (bg < 16) {
                    s8_append_fmt(ctx->arena, &result, "\x1b[", 100 + bg - 8, "m");
                } else {
                    s8_append_fmt(ctx->arena, &result, "\x1b[48;2;", (bg >> 0) & 0xff, ";", (bg >> 8) & 0xff, ";", (bg >> 16) & 0xff, "m");
                }
            }

            if (last_fg != fg) {
                last_fg = fg;
                if (fg < 8) {
                    s8_append_fmt(ctx->arena, &result, "\x1b[", 30 + fg, "m");
                } else if (fg < 16) {
                    s8_append_fmt(ctx->arena, &result, "\x1b[", 90 + fg - 8, "m");
                } else {
                    s8_append_fmt(ctx->arena, &result, "\x1b[38;2;", (fg >> 0) & 0xff, ";", (fg >> 8) & 0xff, ";", (fg >> 16) & 0xff, "m");
                }
            }
        }

        s8_append(ctx->arena, &result, s8_slice(lines[y], last_flush_offset, USIZE_MAX));
    }

    if (rctx.cursor.x >= 0 && rctx.cursor.y >= 0) {
        // CUP to the cursor position.
        // DECSCUSR to set the cursor style.
        // DECTCEM to show the cursor.
        s8_append_fmt(ctx->arena, &result, "\x1b[", rctx.cursor.y + 1, ";", rctx.cursor.x + 1, "H\x1b[", rctx.cursor_overtype ? 1 : 5, " q\x1b[?25h");
    } else {
        // DECTCEM to hide the cursor.
        s8_append(ctx->arena, &result, S("\x1b[?25l"));
    }

#if 0
    {
        result = (s8){};

        for (UiNode* root = ctx->root_first; root; root = root->sibling_next) {
            UiNode* node = root;
            usize depth = 0;

            for (;;) {
                s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                s8_append_fmt(ctx->arena, &result, "- id: ", node->id, "\r\n");

                s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                s8_append_fmt(ctx->arena, &result, "  intrinsic:    {", node->intrinsic_size.width, ", ", node->intrinsic_size.height, "}\r\n");

                s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                s8_append_fmt(ctx->arena, &result, "  outer:        {", node->outer.left, ", ", node->outer.top, ", ", node->outer.right, ", ", node->outer.bottom, "}\r\n");

                s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                s8_append_fmt(ctx->arena, &result, "  inner:        {", node->inner.left, ", ", node->inner.top, ", ", node->inner.right, ", ", node->inner.bottom, "}\r\n");

                if (node->attributes.bordered) {
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append(ctx->arena, &result, S("  bordered:     true\r\n"));
                }

                if (node->attributes.grid_columns.len != 0) {
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append(ctx->arena, &result, S("  grid_columns: ["));
                    for (usize i = 0; i < node->attributes.grid_columns.len; i++) {
                        s8_append_u64(ctx->arena, &result, node->attributes.grid_columns.beg[i]);
                        if (i + 1 < node->attributes.grid_columns.len) {
                            s8_append(ctx->arena, &result, S(", "));
                        }
                    }
                    s8_append(ctx->arena, &result, S("]\r\n"));
                }

                if (node->attributes.bg_set) {
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append_fmt(ctx->arena, &result, "  bg:           ", node->attributes.bg, "\r\n");
                }

                if (node->attributes.fg_set) {
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append_fmt(ctx->arena, &result, "  fg:           ", node->attributes.fg, "\r\n");
                }

                if (ctx->focused_item_id == node->id) {
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append(ctx->arena, &result, S("  focused:      true\r\n"));
                }

                switch (node->content_type) {
                case UI_NODE_TEXT:
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append_fmt(ctx->arena, &result, "  text:         \"", node->content.text, "\"\r\n");
                    break;
                case UI_NODE_TEXTAREA:
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append_fmt(ctx->arena, &result, "  textarea:     ", (uintptr_t)node->content.textarea, "\r\n");
                    break;
                case UI_NODE_SCROLLAREA:
                    s8_append_repeat(ctx->arena, &result, ' ', depth * 2);
                    s8_append(ctx->arena, &result, S("  scrollable:   true\r\n"));
                    break;
                default:
                    break;
                }

                if (node->child_first) {
                    node = node->child_first;
                    depth += 1;
                } else {
                    while (!node->sibling_next) {
                        node = node->parent;
                        if (node == root) {
                            goto next;
                        }
                        depth -= 1;
                    }
                    node = node->sibling_next;
                }
            }

        next:;
        }
    }
#endif

    return result;
}

Size ui_root_get_size(UiContext* ctx)
{
    return ctx->size;
}

void ui_container_begin(UiContext* ctx, u64 id)
{
    UiNode* parent = ctx->parent;

    UiNode* node = arena_zalloc(ctx->arena, UiNode);
    node->stack_parent = parent;
    node->id = id;
    node->parent = parent;
    ui_node_append_child(parent, node);

    ctx->attr_node = node;
    ctx->parent = node;
    ctx->node_count += 1;

    if (ctx->autofocus_next) {
        ctx->autofocus_next = false;
        if (ctx->focused_item_id == parent->id) {
            ctx->focused_item_id = id;
        }
    }
}

void ui_container_begin_named(UiContext* ctx, s8 id)
{
    ui_container_begin(ctx, hash_s8(ctx->parent->id, id));
}

void ui_container_end(UiContext* ctx)
{
    ctx->attr_node = ctx->parent;
    ctx->parent = ctx->parent->stack_parent;
    ctx->autofocus_next = false;
}

void ui_attr_float(UiContext* ctx, UiFloatSpec spec)
{
    UiNode* node = ctx->attr_node;

    // Remove the node from the UI tree and insert it into the floater list.
    UiNode* parent = node->parent;
    ui_node_remove(node);
    node->parent = parent;
    node->sibling_prev = ctx->root_last;
    ctx->root_last->sibling_next = node;
    ctx->root_last = node;

    spec.gravity_x = clamp(spec.gravity_x, 0, 1);
    spec.gravity_y = clamp(spec.gravity_y, 0, 1);
    node->attributes.float_spec = spec;
    node->attributes.floating = true;
}

void ui_attr_border(UiContext* ctx)
{
    ctx->attr_node->attributes.bordered = true;
}

void ui_attr_padding(UiContext* ctx, Rect padding)
{
    padding.right = max(padding.right, 0);
    padding.bottom = max(padding.bottom, 0);
    padding.left = max(padding.left, 0);
    padding.top = max(padding.top, 0);
    ctx->attr_node->attributes.padding = padding;
}

void ui_attr_grid_columns(UiContext* ctx, UiGridColumns columns)
{
    Attributes* attrs = &ctx->attr_node->attributes;
    attrs->grid_columns.beg = arena_mallocn(ctx->arena, CoordType, columns.count);
    attrs->grid_columns.len = columns.count;
    attrs->grid_columns.cap = columns.count;
    memcpy(attrs->grid_columns.beg, columns.widths, columns.count * sizeof(CoordType));
}

void ui_attr_background_rgba(UiContext* ctx, u32 bg)
{
    Attributes* attrs = &ctx->attr_node->attributes;
    attrs->bg = bg;
}

void ui_attr_foreground_rgba(UiContext* ctx, u32 fg)
{
    Attributes* attrs = &ctx->attr_node->attributes;
    attrs->fg = fg;
}

void ui_attr_background_indexed(UiContext* ctx, u32 bg)
{
    Attributes* attrs = &ctx->attr_node->attributes;
    attrs->bg = ctx->indexed_colors[bg & 15];
}

void ui_attr_foreground_indexed(UiContext* ctx, u32 fg)
{
    Attributes* attrs = &ctx->attr_node->attributes;
    attrs->fg = ctx->indexed_colors[fg & 16];
}

static UiNode* get_prev_node(UiContext* ctx, u64 id)
{
    UiNode** node_map = ctx->node_map;
    usize shift = ctx->node_map_shift;
    usize mask = ctx->node_map_mask;

    for (usize slot = (id >> shift) & mask;; slot = (slot + 1) & mask) {
        UiNode* node = node_map[slot];
        if (!node) {
            return NULL;
        }
        if (node->id == id) {
            return node;
        }
    }
}

void ui_focus_next_by_default(UiContext* ctx)
{
    ctx->autofocus_next = true;
}

void ui_consume_input(UiContext* ctx)
{
    assert(!ctx->input_consumed);
    ctx->input_consumed = true;
}

bool ui_consume_shortcut(UiContext* ctx, i32 shortcut)
{
    if (!ctx->input_consumed && ctx->input_keyboard.key == (shortcut & 0xff) && ctx->input_keyboard.modifiers == (shortcut & (i32)0xff000000)) {
        ui_consume_input(ctx);
        return true;
    }
    return false;
}

MouseAction ui_input_mouse(UiContext* ctx)
{
    return ctx->input_consumed ? MOUSE_ACTION_NONE : ctx->input_mouse_action;
}

s8 ui_input_text(UiContext* ctx)
{
    return ctx->input_consumed ? (s8){} : ctx->input_text;
}

UiInputKeyboard ui_input_keyboard(UiContext* ctx)
{
    return ctx->input_consumed ? (UiInputKeyboard){} : ctx->input_keyboard;
}

bool ui_is_hovering(UiContext* ctx)
{
    UiNode* node = get_prev_node(ctx, ctx->attr_node->id);
    return node && rect_contains(node->outer, ctx->input_mouse_position);
}

bool ui_has_focus(UiContext* ctx)
{
    return ctx->attr_node->id == ctx->focused_item_id;
}

bool ui_was_clicked(UiContext* ctx)
{
    return ui_has_focus(ctx) && ui_input_mouse(ctx) == MOUSE_ACTION_RELEASE;
}

void ui_label(UiContext* ctx, s8 text)
{
    ui_styled_label_begin(ctx, text);
    ui_styled_label_add_text(ctx, text);
    ui_styled_label_end(ctx);
}

void ui_styled_label_begin(UiContext* ctx, s8 id)
{
    ui_container_begin_named(ctx, id);

    StyledTextChunk* chunk = arena_zalloc(ctx->arena, StyledTextChunk);
    UiNode* node = ctx->attr_node;
    node->content_type = UI_NODE_TEXT;
    node->content.text.first = chunk;
    node->content.text.last = chunk;
}

void ui_styled_label_set_foreground_indexed(UiContext* ctx, u32 fg)
{
    StyledTextChunk* last = ctx->attr_node->content.text.last;
    if (last->text.len == 0) {
        last->fg = fg;
    } else if (last->fg != fg) {
        StyledTextChunk* chunk = arena_zalloc(ctx->arena, StyledTextChunk);
        chunk->fg = fg;
        last->next = chunk;
        ctx->attr_node->content.text.last = chunk;
    }
}

void ui_styled_label_add_text(UiContext* ctx, s8 text)
{
    UiNode* node = ctx->attr_node;
    s8_append(ctx->arena, &node->content.text.last->text, text);
}

void ui_styled_label_end(UiContext* ctx)
{
    UiNode* node = ctx->attr_node;
    StyledTextChunk* chunk = node->content.text.first;
    UcdMeasurement m = {};

    for (; chunk; chunk = chunk->next) {
        m = ucd_measure_forward(chunk->text, 0, m.pos, COORD_TYPE_MAX, -1, NULL);
    }

    node->intrinsic_size.width = m.pos.x;
    node->intrinsic_size.height = 1;
    node->intrinsic_size_set = true;

    ui_container_end(ctx);
}

bool ui_button(UiContext* ctx, s8 text)
{
    ui_label(ctx, text);
    ui_attr_background_rgba(ctx, 0xa0ffffff);
    ui_attr_foreground_rgba(ctx, 0xff000000);
    if (ui_has_focus(ctx)) {
        ui_attr_background_rgba(ctx, 0xa0000000);
        ui_attr_foreground_rgba(ctx, 0xffffffff);
    }
    return ui_was_clicked(ctx);
}

bool ui_editline(UiContext* ctx, s8 name)
{
    ui_container_begin_named(ctx, name);
    ui_container_end(ctx);
    return false;
}

static void ui_textarea_handle_input(UiContext* ctx, TextBuffer* tb)
{
    UiNode* outer = ctx->attr_node;
    UiNode* inner = ctx->parent;
    UiNode* outer_prev = get_prev_node(ctx, outer->id);
    UiNode* inner_prev = get_prev_node(ctx, inner->id);

    if (!outer_prev || !inner_prev || ctx->input_consumed) {
        return;
    }

    if (ctx->input_text.len) {
        text_buffer_write(tb, ctx->input_text);
        ui_consume_input(ctx);
        return;
    }

    if (ctx->input_mouse_action) {
        switch (ctx->input_mouse_action) {
        case MOUSE_ACTION_LEFT: {
            if (!rect_contains(outer_prev->inner, ctx->input_mouse_position)) {
                return;
            }
            CoordType x = ctx->input_mouse_position.x - inner_prev->outer.left;
            CoordType y = ctx->input_mouse_position.y - inner_prev->outer.top;
            text_buffer_selection_update(tb, (Point){x, y});
            break;
        }
        case MOUSE_ACTION_RELEASE: {
            if (!rect_contains(outer_prev->inner, ctx->input_mouse_position)) {
                return;
            }
            if (!text_buffer_selection_end(tb)) {
                CoordType x = ctx->input_mouse_position.x - inner_prev->outer.left;
                CoordType y = ctx->input_mouse_position.y - inner_prev->outer.top;
                text_buffer_cursor_move_to_visual(tb, (Point){x, y});
            }
            break;
        }
        default:
            return;
        }

        ui_consume_input(ctx);
        return;
    }

    if (ctx->input_keyboard.key) {
        CoordType width = outer_prev->inner.right - outer_prev->inner.left;
        CoordType height = outer_prev->inner.bottom - outer_prev->inner.top;
        bool make_cursor_visible = true;

        switch (ctx->input_keyboard.key) {
        case VK_BACK:
            text_buffer_delete(tb, -1);
            break;
        case VK_TAB:
            text_buffer_write(tb, S("    "));
            break;
        case VK_RETURN:
            text_buffer_write(tb, S("\n"));
            break;
        case VK_PRIOR:
            text_buffer_cursor_move_to_visual(tb, (Point){tb->cursor.logical_pos.x, tb->cursor.logical_pos.y - height});
            outer->content.scrollarea.y -= height;
            break;
        case VK_NEXT:
            text_buffer_cursor_move_to_visual(tb, (Point){tb->cursor.logical_pos.x, tb->cursor.logical_pos.y + height});
            outer->content.scrollarea.y += height;
            break;
        case VK_END:
            text_buffer_cursor_move_to_visual(tb, (Point){COORD_TYPE_SAFE_MAX, tb->cursor.logical_pos.y});
            break;
        case VK_HOME:
            text_buffer_cursor_move_to_visual(tb, (Point){0, tb->cursor.logical_pos.y});
            break;
        case VK_LEFT:
            text_buffer_cursor_move_delta(tb, -1);
            break;
        case VK_UP:
            if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_NONE) {
                text_buffer_cursor_move_to_visual(tb, (Point){tb->cursor.logical_pos.x, tb->cursor.logical_pos.y - 1});
            } else if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_CTRL) {
                outer->content.scrollarea.y -= 1;
                make_cursor_visible = false;
            } else if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_SHIFT) {
                // TODO: Selection
            } else if (ctx->input_keyboard.modifiers == (KEYBOARD_MODIFIER_CTRL | KEYBOARD_MODIFIER_ALT)) {
                // TODO: Add cursor above
            }
            break;
        case VK_RIGHT:
            text_buffer_cursor_move_delta(tb, 1);
            break;
        case VK_DOWN:
            if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_NONE) {
                text_buffer_cursor_move_to_visual(tb, (Point){tb->cursor.logical_pos.x, tb->cursor.logical_pos.y + 1});
            } else if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_CTRL) {
                outer->content.scrollarea.y += 1;
                make_cursor_visible = false;
            } else if (ctx->input_keyboard.modifiers == KEYBOARD_MODIFIER_SHIFT) {
                // TODO: Selection
            } else if (ctx->input_keyboard.modifiers == (KEYBOARD_MODIFIER_CTRL | KEYBOARD_MODIFIER_ALT)) {
                // TODO: Add cursor below
            }
            break;
        case VK_INSERT:
            tb->overtype = !tb->overtype;
            break;
        case VK_DELETE:
            text_buffer_delete(tb, 1);
            break;
        case VK_Y:
            if ((ctx->input_keyboard.modifiers & KEYBOARD_MODIFIER_CTRL) == 0) {
                return;
            }
            text_buffer_redo(tb);
            break;
        case VK_Z:
            if ((ctx->input_keyboard.modifiers & KEYBOARD_MODIFIER_CTRL) == 0) {
                return;
            }
            text_buffer_undo(tb);
            break;
        default:
            return;
        }

        if (make_cursor_visible) {
            CoordType scroll_x = outer->content.scrollarea.x;
            CoordType scroll_y = outer->content.scrollarea.y;
            CoordType cursor_x = tb->cursor.logical_pos.x;
            CoordType cursor_y = tb->cursor.logical_pos.y;

            scroll_x = min(scroll_x, cursor_x);
            scroll_x = max(scroll_x, cursor_x - width + 1);

            scroll_y = min(scroll_y, cursor_y);
            scroll_y = max(scroll_y, cursor_y - height + 1);

            outer->content.scrollarea.x = scroll_x;
            outer->content.scrollarea.y = scroll_y;

            inner->intrinsic_size.width = max(inner->intrinsic_size.width, scroll_x + width);
        }

        ui_consume_input(ctx);
    }
}

void ui_textarea(UiContext* ctx, TextBuffer* tb, Size intrinsic_size)
{
    ui_scrollarea_begin(ctx, (s8){.beg = (u8*)&ctx->parent->id, .len = sizeof(ctx->parent->id), .cap = sizeof(ctx->parent->id)}, intrinsic_size);

    // Reuse the inner node for our textarea.
    UiNode* inner = ctx->parent;
    inner->content_type = UI_NODE_TEXTAREA;
    inner->content.textarea = tb;
    inner->intrinsic_size.height = tb->stats.lines;
    inner->intrinsic_size_set = true;

    UiNode* inner_prev = get_prev_node(ctx, inner->id);
    if (inner_prev) {
        text_buffer_reflow(tb, inner_prev->inner.right - inner_prev->inner.left);
    }

    // Can't use ui_has_focus() here because that checks the ctx->attr_node. If we didn't abuse
    // the inner node for our textarea it would've been updated by our ui_container_begin() call.
    if (inner->id == ctx->focused_item_id) {
        ui_textarea_handle_input(ctx, tb);
    }

    ui_scrollarea_end(ctx);
}

void ui_scrollarea_begin(UiContext* ctx, s8 name, Size intrinsic_size)
{
    ui_container_begin_named(ctx, name);

    UiNode* outer = ctx->attr_node;
    u64 id = outer->id;

    outer->content_type = UI_NODE_SCROLLAREA;

    UiNode* outer_prev = get_prev_node(ctx, id);
    if (outer_prev) {
        outer->content.scrollarea = outer_prev->content.scrollarea;
    }

    if (intrinsic_size.width > 0 || intrinsic_size.height > 0) {
        outer->intrinsic_size = intrinsic_size;
        outer->intrinsic_size_set = true;
    }

    ui_focus_next_by_default(ctx);
    ui_container_begin_named(ctx, S("inner"));

    // Ensure that attribute modifications apply to the outer container.
    ctx->attr_node = outer;
}

void ui_scrollarea_end(UiContext* ctx)
{
    ui_container_end(ctx);
    ui_container_end(ctx);

    if (ui_is_hovering(ctx) && ui_input_mouse(ctx) == MOUSE_ACTION_SCROLL) {
        UiNode* outer = ctx->attr_node;
        outer->content.scrollarea.x += ctx->input_scroll_delta.x;
        outer->content.scrollarea.y += ctx->input_scroll_delta.y;
    }
}

void ui_menubar_begin(UiContext* ctx)
{
    ui_container_begin_named(ctx, S("menubar"));
}

bool ui_menubar_menu_begin(UiContext* ctx, s8 text, c8 accelerator)
{
    slice_push(ctx->arena, &ctx->parent->attributes.grid_columns, 0);

    if (accelerator >= 'A' && accelerator <= 'Z') {
        usize off = 0;
        for (; off < text.len; off += 1) {
            c8 c = text.beg[off];
            c &= ~0x20; // transform `c` to uppercase
            if (c == accelerator) {
                break;
            }
        }

        ui_styled_label_begin(ctx, text);

        if (off < text.len) {
            // Highlight the accelerator in red.
            ui_styled_label_add_text(ctx, s8_slice(text, 0, off));
            ui_styled_label_set_foreground_indexed(ctx, 9);
            ui_styled_label_add_text(ctx, s8_slice(text, off, off + 1));
            ui_styled_label_set_foreground_indexed(ctx, 0);
            ui_styled_label_add_text(ctx, s8_slice(text, off + 1, USIZE_MAX));
        } else {
            // Add the accelerator in parentheses (still in red).
            ui_styled_label_add_text(ctx, text);
            ui_styled_label_add_text(ctx, S("("));
            ui_styled_label_set_foreground_indexed(ctx, 9);
            ui_styled_label_add_text(ctx, (s8){.beg = &accelerator, .len = 1, .cap = 1});
            ui_styled_label_set_foreground_indexed(ctx, 0);
            ui_styled_label_add_text(ctx, S(")"));
        }

        ui_styled_label_end(ctx);
    } else {
        ui_label(ctx, text);
    }

    ui_attr_padding(ctx, (Rect){1, 0, 1, 0});

    if (ui_has_focus(ctx)) {
        ui_attr_background_indexed(ctx, 15);
        ui_attr_foreground_indexed(ctx, 0);

        ui_container_begin(ctx, hash_s8(ctx->attr_node->id, S("flyout")));
        ui_attr_float(ctx, (UiFloatSpec){.offset_y = 1});
        ui_attr_grid_columns(ctx, (UiGridColumns){.count = 2, .widths = (CoordType[]){0, 0}});
        ui_attr_border(ctx);
        ui_attr_background_indexed(ctx, 15);
        ui_attr_foreground_indexed(ctx, 0);
        return true;
    }

    return false;
}

bool ui_menubar_menu_item(UiContext* ctx, s8 text, c8 accelerator, i32 shortcut)
{
    if (accelerator >= 'A' && accelerator <= 'Z') {
        usize off = 0;
        for (; off < text.len; off += 1) {
            c8 c = text.beg[off];
            c &= ~0x20; // transform `c` to uppercase
            if (c == accelerator) {
                break;
            }
        }

        ui_styled_label_begin(ctx, text);

        if (off < text.len) {
            // Highlight the accelerator in red.
            ui_styled_label_add_text(ctx, s8_slice(text, 0, off));
            ui_styled_label_set_foreground_indexed(ctx, 9);
            ui_styled_label_add_text(ctx, s8_slice(text, off, off + 1));
            ui_styled_label_set_foreground_indexed(ctx, 0);
            ui_styled_label_add_text(ctx, s8_slice(text, off + 1, USIZE_MAX));
        } else {
            // Add the accelerator in parentheses (still in red).
            ui_styled_label_add_text(ctx, text);
            ui_styled_label_add_text(ctx, S("("));
            ui_styled_label_set_foreground_indexed(ctx, 9);
            ui_styled_label_add_text(ctx, (s8){.beg = &accelerator, .len = 1, .cap = 1});
            ui_styled_label_set_foreground_indexed(ctx, 0);
            ui_styled_label_add_text(ctx, S(")"));
        }

        ui_styled_label_end(ctx);
    } else {
        ui_label(ctx, text);
    }

    i32 shortcut_letter = shortcut & 0xff;
    if (shortcut_letter >= 'A' && shortcut_letter <= 'Z') {
        s8 shortcut_text = {};
        if (shortcut & KEYBOARD_MODIFIER_CTRL) {
            s8_append(ctx->arena, &shortcut_text, S("Ctrl+"));
        }
        if (shortcut & KEYBOARD_MODIFIER_ALT) {
            s8_append(ctx->arena, &shortcut_text, S("Alt+"));
        }
        if (shortcut & KEYBOARD_MODIFIER_SHIFT) {
            s8_append(ctx->arena, &shortcut_text, S("Shift+"));
        }
        s8_append(ctx->arena, &shortcut_text, (s8){.beg = (u8*)&shortcut_letter, .len = 1, .cap = 1});

        ui_label(ctx, shortcut_text);
        ui_attr_padding(ctx, (Rect){2, 0, 0, 0});
    } else {
        ui_container_begin(ctx, hash(ctx->attr_node->id, NULL, 0));
        ui_container_end(ctx);
    }

    return ui_was_clicked(ctx);
}

void ui_menubar_menu_end(UiContext* ctx)
{
    ui_container_end(ctx);
}

void ui_menubar_end(UiContext* ctx)
{
    ui_container_end(ctx);
}
