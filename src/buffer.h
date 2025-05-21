// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "arena.h"

typedef struct TextBufferStatistics {
    CoordType lines;
} TextBufferStatistics;

typedef struct TextBufferCursor {
    usize offset;
    Point logical_pos; // in lines & graphemes; line wrapping has no influence on this
    Point visual_pos; // in rows & columns, including line wrapping
} TextBufferCursor;

typedef enum TextBufferSelectionState {
    TEXT_BUFFER_SELECTION_NONE,
    TEXT_BUFFER_SELECTION_MAYBE,
    TEXT_BUFFER_SELECTION_ACTIVE,
    TEXT_BUFFER_SELECTION_DONE,
} TextBufferSelectionState;

typedef struct TextBufferSelection {
    Point beg; // inclusive
    Point end; // inclusive
    TextBufferSelectionState state;
} TextBufferSelection;

typedef struct TextBufferChange TextBufferChange;

struct TextBufferChange {
    TextBufferChange* prev;
    TextBufferChange* next;
    TextBufferCursor cursor;
    s8 removed;  // The text that was removed.
    s8 inserted; // The text that was inserted.
};

typedef struct TextBufferAllocator TextBufferAllocator;

struct TextBufferAllocator {
    void* (*realloc)(void* base, usize old_len, usize* new_len);
    void (*free)(void* base, usize len);
};

typedef struct TextBuffer {
    TextBufferAllocator allocator;

    c8* main_base;
    usize main_commit;

    c8* text;
    usize text_length; // does not account for the gap_len
    usize gap_off;
    usize gap_len;

    void* undo_base;
    TextBufferChange* undo_tail;
    usize undo_usage;
    usize undo_commit;

    TextBufferStatistics stats;
    TextBufferCursor cursor;
    TextBufferSelection selection;

    CoordType word_wrap_columns;
    bool dirty;
    bool overtype;
} TextBuffer;

TextBufferAllocator text_buffer_allocator_default();

TextBuffer* text_buffer_create(TextBufferAllocator allocator);
void text_buffer_destroy(TextBuffer* tb);
void text_buffer_read_file(TextBuffer* tb, s8 path);
void text_buffer_write_file(TextBuffer* tb, s8 path);

s8 text_buffer_read_backward(const TextBuffer* tb, usize off);
s8 text_buffer_read_forward(const TextBuffer* tb, usize off);

void text_buffer_selection_update(TextBuffer* tb, Point pos);
bool text_buffer_selection_end(TextBuffer* tb);

void text_buffer_reflow(TextBuffer* tb, CoordType width);
usize text_buffer_cursor_move_to_logical(TextBuffer* tb, Point pos);
usize text_buffer_cursor_move_to_visual(TextBuffer* tb, Point pos);
usize text_buffer_cursor_move_delta(TextBuffer* tb, CoordType cursor_movements);
usize text_buffer_extract(TextBuffer* tb, usize beg, usize end, c8* dst);

void text_buffer_undo(TextBuffer* tb);
void text_buffer_redo(TextBuffer* tb);

void text_buffer_write(TextBuffer* tb, s8 str);
void text_buffer_delete(TextBuffer* tb, CoordType cursor_movements);
