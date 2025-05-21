// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "buffer.h"

#include "os.h"
#include "ucd.h"

#include <stdlib.h>

#define TEXT_BUFFER_ALLOC_MAX_BYTES 0x80000000 // 2GiB
#define TEXT_BUFFER_ALLOC_CHUNK_BYTES 0x10000  // 64KiB
#define TEXT_BUFFER_GAP_CHUNK_BYTES 0x1000     // 4KiB

static void* text_buffer_allocator_default_realloc(void* base, usize old_len, usize* new_len)
{
    if (!base) {
        base = os_virtual_reserve(TEXT_BUFFER_ALLOC_MAX_BYTES);
    }

    usize len = *new_len;
    len = (len + TEXT_BUFFER_ALLOC_CHUNK_BYTES - 1) & ~(TEXT_BUFFER_ALLOC_CHUNK_BYTES - 1);

    if (len > TEXT_BUFFER_ALLOC_MAX_BYTES) {
        abort();
    }

    os_virtual_commit((u8*)base + old_len, len - old_len);
    *new_len = len;
    return base;
}

static void text_buffer_allocator_default_free(void* base, usize len)
{
    if (base) {
        os_virtual_release(base);
    }
}

TextBufferAllocator text_buffer_allocator_default()
{
    return (TextBufferAllocator){
        .realloc = text_buffer_allocator_default_realloc,
        .free = text_buffer_allocator_default_free,
    };
}

TextBuffer* text_buffer_create(TextBufferAllocator allocator)
{
    assert(allocator.realloc);
    assert(allocator.free);

    usize commit = sizeof(TextBuffer);
    TextBuffer* tb = allocator.realloc(NULL, 0, &commit);
    if (!tb) {
        abort();
    }

    *tb = (TextBuffer){
        .allocator = allocator,

        .main_base = (c8*)tb,
        .main_commit = commit,

        .text = (c8*)(tb + 1),

        .word_wrap_columns = -1,
    };
    return tb;
}

void text_buffer_destroy(TextBuffer* tb)
{
    tb->allocator.free(tb->undo_base, tb->undo_commit);
    tb->allocator.free(tb->main_base, tb->main_commit);
}

static void text_buffer_allocate_gap_impl(TextBuffer* tb, usize off, usize len)
{
    c8* data = tb->text;
    const usize length = tb->text_length;
    const usize gap_off = tb->gap_off;
    const usize gap_len = tb->gap_len;

    off = min(off, length);

    // Move the existing gap if it exists
    if (off != gap_off) {
        if (gap_len > 0) {
            //
            //                       v gap_off
            // left:  |ABCDEFGHIJKLMN   OPQRSTUVWXYZ|
            //        |ABCDEFGHI   JKLMNOPQRSTUVWXYZ|
            //                  ^ off
            //        move: JKLMN
            //
            //                       v gap_off
            // !left: |ABCDEFGHIJKLMN   OPQRSTUVWXYZ|
            //        |ABCDEFGHIJKLMNOPQRS   TUVWXYZ|
            //                            ^ off
            //        move: OPQRS
            //
            const bool left = off < gap_off;
            const usize move_src = left ? off : gap_off + gap_len;
            const usize move_dst = left ? off + gap_len : gap_off;
            const usize move_len = left ? gap_off - off : off - gap_off;
            memmove(data + move_dst, data + move_src, move_len);
#ifndef NDEBUG
            memset(data + off, 0xCD, gap_len);
#endif
        }

        tb->gap_off = off;
    }

    // Enlarge the gap if needed
    if (len > gap_len) {
        usize gap_chunk_pad_chars = TEXT_BUFFER_GAP_CHUNK_BYTES / 2;
        usize gap_len_new = (len + gap_chunk_pad_chars + TEXT_BUFFER_GAP_CHUNK_BYTES - 1) & ~(TEXT_BUFFER_GAP_CHUNK_BYTES - 1);
        usize bytes_old = tb->main_commit;
        // The TextBuffer struct is stored in front of the .text data. As such, we need to account for it.
        usize bytes_new = sizeof(TextBuffer) + length + gap_len_new;

        if (bytes_new > bytes_old) {
            tb->main_base = tb->allocator.realloc(tb->main_base, bytes_old, &bytes_new);
            tb->main_commit = bytes_new;
        }

        c8* gap_beg = data + off;
        memmove(gap_beg + gap_len_new, gap_beg + gap_len, length - off);
#ifndef NDEBUG
        memset(gap_beg + gap_len, 0xCD, gap_len_new - gap_len);
#endif

        tb->gap_len = gap_len_new;
    }
}

static c8* text_buffer_allocate_gap(TextBuffer* tb, const usize off, const usize len)
{
    if (off != tb->gap_off || len > tb->gap_len) {
        text_buffer_allocate_gap_impl(tb, off, len);
    }
    return tb->text + off;
}

static void text_buffer_commit_gap(TextBuffer* tb, usize len)
{
    len = min(len, tb->gap_len);
    tb->gap_off += len;
    tb->gap_len -= len;
    tb->text_length += len;
}

s8 text_buffer_read_backward(const TextBuffer* tb, usize off)
{
    usize beg;
    usize len;

    if (off <= tb->gap_off) {
        // Cursor is before the gap: We can read until the beginning of the buffer.
        beg = 0;
        len = off;
    } else {
        // Cursor is after the gap: We can read until the end of the gap.
        beg = tb->gap_off + tb->gap_len;
        // The cursor_off doesn't account of the gap_len.
        // (This allows us to move the gap without recalculating the cursor position.)
        len = off - tb->gap_off;
    }

    return (s8){tb->text + beg, len, len};
}

s8 text_buffer_read_forward(const TextBuffer* tb, usize off)
{
    usize beg;
    usize len;

    if (off < tb->gap_off) {
        // Cursor is before the gap: We can read until the start of the gap.
        beg = off;
        len = tb->gap_off - off;
    } else {
        // Cursor is after the gap: We can read until the end of the buffer.
        beg = off + tb->gap_len;
        len = tb->text_length - off;
    }

    return (s8){tb->text + beg, len, len};
}

static void text_buffer_record_push_change(TextBuffer* tb, usize beg, usize end, s8 replacement)
{
    if (beg > end || end > tb->text_length) {
        assert(false);
        return;
    }

    usize removed_len = end - beg;
    usize space_needed = sizeof(TextBufferChange) + removed_len + replacement.len;
    usize usage_now = tb->undo_usage;
    usize usage_next = usage_now + space_needed;

    if (usage_next > tb->undo_commit) {
        tb->undo_base = tb->allocator.realloc(tb->undo_base, tb->undo_commit, &usage_next);
        tb->undo_commit = usage_next;
        if (tb->undo_tail) {
            // This ensures that text_buffer_redo() can differentiate between
            // an empty undo stack and one that has simply been fully undone.
            memset(tb->undo_base, 0, sizeof(TextBufferChange));
        }
    }

    TextBufferChange* change = (TextBufferChange*)((u8*)tb->undo_base + tb->undo_usage);
    s8 removed = {(c8*)(change + 1), removed_len, removed_len};
    s8 inserted = {removed.beg + removed.len, replacement.len, replacement.len};

    removed.len = text_buffer_extract(tb, beg, end, removed.beg);
    memcpy(inserted.beg, replacement.beg, replacement.len);

    if (tb->undo_tail) {
        tb->undo_tail->next = change;
    }

    change->prev = tb->undo_tail;
    change->next = NULL;
    change->cursor = tb->cursor;
    change->removed = removed;
    change->inserted = inserted;
    tb->undo_tail = change;
    tb->undo_usage = usage_next;
    tb->dirty = true;
}

enum CodePage_t {
    CP_UTF7 = 65000,
    CP_UTF8 = 65001,
    CP_UTF16LE = 1200,
    CP_UTF16BE = 1201,
    CP_UTF32LE = 12000,
    CP_UTF32BE = 12001,
    CP_GB18030 = 54936,
};

static int detect_bom(const c8* buffer, int read, u32* cp_out)
{
    u32 cp = 0;
    int len = 0;

    if (read >= 4) {
        len = 4;
        if (memcmp(&buffer[0], "\xFF\xFE\x00\x00", 4) == 0) {
            cp = CP_UTF32LE;
        } else if (memcmp(&buffer[0], "\x00\x00\xFE\xFF", 4) == 0) {
            cp = CP_UTF32BE;
        } else if (memcmp(&buffer[0], "\x84\x31\x95\x33", 4) == 0) {
            cp = CP_GB18030;
        } else {
            len = 0;
        }
    }

    if (cp == 0 && read >= 3) {
        len = 3;
        if (memcmp(&buffer[0], "\xEF\xBB\xBF", 3) == 0) {
            cp = CP_UTF8;
        }
        if (memcmp(&buffer[0], "\x2B\x2F\x76", 3) == 0) {
            cp = CP_UTF7;
        } else {
            len = 0;
        }
    }

    if (cp == 0 && read >= 2) {
        len = 2;
        if (memcmp(&buffer[0], "\xFF\xFE", 2) == 0) {
            cp = CP_UTF16LE;
        }
        if (memcmp(&buffer[0], "\xFE\xFF", 2) == 0) {
            cp = CP_UTF16BE;
        } else {
            len = 0;
        }
    }

    if (cp == 0) {
        cp = CP_UTF8;
    }

    *cp_out = cp;
    return len;
}

void text_buffer_read_file(TextBuffer* tb, s8 path)
{
    void* file = os_open_file_for_reading(path);
    if (!file) {
        return;
    }

    usize chunk_size = os_file_size(file) + TEXT_BUFFER_ALLOC_CHUNK_BYTES / 2;

    for (;;) {
        c8* beg = text_buffer_allocate_gap(tb, tb->text_length, chunk_size);
        usize len = os_read_file(file, beg, chunk_size);
        text_buffer_commit_gap(tb, len);
        if (len < chunk_size) {
            break;
        }
        chunk_size = TEXT_BUFFER_ALLOC_CHUNK_BYTES;
    }

    os_close_file(file);

    s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
    ucd_newlines_forward(chunk, 0, &tb->stats.lines, COORD_TYPE_MAX);
    tb->stats.lines += 1;
}

void text_buffer_write_file(TextBuffer* tb, s8 path)
{
    c8* before_gap = tb->text;
    usize before_gap_len = tb->gap_off;
    c8* after_gap = before_gap + tb->gap_off + tb->gap_len;
    usize after_gap_len = tb->text_length - tb->gap_off;

    // TODO: Write to a temp file and do an atomic rename.
    void* file = os_open_file_for_writing(path);
    if (!file) {
        return;
    }

    os_write_file(file, before_gap, before_gap_len);
    os_write_file(file, after_gap, after_gap_len);
    os_close_file(file);

    tb->dirty = false;
}

void text_buffer_selection_update(TextBuffer* tb, Point pos)
{
    if (tb->selection.state == TEXT_BUFFER_SELECTION_NONE || tb->selection.state == TEXT_BUFFER_SELECTION_DONE) {
        tb->selection.state = TEXT_BUFFER_SELECTION_MAYBE;
        tb->selection.beg = pos;
    } else {
        tb->selection.state = TEXT_BUFFER_SELECTION_ACTIVE;
        tb->selection.end = pos;
    }
}

bool text_buffer_selection_end(TextBuffer* tb)
{
    bool active = tb->selection.state == TEXT_BUFFER_SELECTION_ACTIVE;
    tb->selection.state = active ? TEXT_BUFFER_SELECTION_DONE : TEXT_BUFFER_SELECTION_NONE;
    return active;
}

static void text_buffer_goto_line_start(TextBuffer* tb, CoordType y)
{
    const usize start_offset = tb->cursor.offset;

    if (y > tb->cursor.logical_pos.y) {
        while (y > tb->cursor.logical_pos.y) {
            s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
            if (chunk.len == 0) {
                break;
            }
            tb->cursor.offset += ucd_newlines_forward(chunk, 0, &tb->cursor.logical_pos.y, y);
        }

        if (tb->cursor.offset != tb->text_length) {
            goto done;
        }

        // If there's no trailing newline on the last line in the file, we must go
        // back to the start of the line, so that `tb->cursor.logical_pos.x == 0` holds true.
        // TODO: Must debug this to make sure it works.
        //__debugbreak();
        y = tb->cursor.logical_pos.y - 1;
    }

    do {
        s8 chunk = text_buffer_read_backward(tb, tb->cursor.offset);
        if (chunk.len == 0) {
            break;
        }
        tb->cursor.offset -= chunk.len - ucd_newlines_backward(chunk, chunk.len, &tb->cursor.logical_pos.y, y);
    } while (y < tb->cursor.logical_pos.y);

done:
    tb->cursor.logical_pos.x = 0;

    if (tb->word_wrap_columns < 0) {
        // Without line wrapping it's easy: The visual line number equals the logical one.
        tb->cursor.visual_pos.x = 0;
        tb->cursor.visual_pos.y = tb->cursor.logical_pos.y;
    } else {
        // With line wrapping we need to count how many wrapped lines we've crossed.
        Point pos = {};
        usize offset, goal_offset;

        if (start_offset < tb->cursor.offset) {
            // Cursor moved down. The start position is the previous cursor position,
            // and so it may not be at the start of a line.
            pos.x = tb->cursor.visual_pos.x;
            offset = start_offset;
            goal_offset = tb->cursor.offset;
        } else {
            // Cursor moved up. The start position is the current offset which,
            // after the code above, is at the start of a line.
            offset = tb->cursor.offset;
            goal_offset = start_offset;
        }

        CoordType delta = 0;

        if (offset < goal_offset) {
            while (true) {
                s8 chunk = text_buffer_read_forward(tb, offset);
                if (chunk.len == 0) {
                    break;
                }

                UcdMeasurement wrap;
                ucd_measure_forward(chunk, 0, pos, tb->word_wrap_columns, -1, &wrap);
                offset += wrap.offset;
                if (offset >= goal_offset) {
                    break;
                }

                delta += 1;
            }
        }

        if (start_offset > tb->cursor.offset) {
            // Cursor moved up.
            delta = -delta;
        }

        tb->cursor.visual_pos.x = 0;
        tb->cursor.visual_pos.y += delta;
    }
}

void text_buffer_reflow(TextBuffer* tb, CoordType width)
{
    if (width <= 0) {
        width = -1;
    }

    if (tb->word_wrap_columns == width) {
        return;
    }

    const Point pos = tb->cursor.logical_pos;

    tb->word_wrap_columns = width;
    tb->cursor = (TextBufferCursor){};

    text_buffer_cursor_move_to_logical(tb, pos);
}

usize text_buffer_cursor_move_to_logical(TextBuffer* tb, Point pos)
{
    CoordType x = max(pos.x, 0);
    CoordType y = max(pos.y, 0);

    text_buffer_goto_line_start(tb, y);

    if (tb->word_wrap_columns < 0) {
        while (x > tb->cursor.logical_pos.x) {
            s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
            if (chunk.len == 0) {
                break;
            }

            UcdMeasurement m = ucd_measure_forward(chunk, 0, tb->cursor.visual_pos, -1, x - tb->cursor.logical_pos.x, NULL);
            tb->cursor.offset += m.offset;
            tb->cursor.logical_pos.x += m.movements;
            tb->cursor.visual_pos = m.pos;
            if (m.offset < chunk.len) {
                break;
            }
        }
    } else {
        if (x > tb->cursor.logical_pos.x) {
            // The primary loop exit condition is down below `if (tb->cursor.logical_pos.x >= x)`.
            while (true) {
                s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
                if (chunk.len == 0) {
                    break;
                }

                UcdMeasurement wrap;
                UcdMeasurement m = ucd_measure_forward(chunk, 0, tb->cursor.visual_pos, tb->word_wrap_columns, x - tb->cursor.logical_pos.x, &wrap);
                tb->cursor.offset += wrap.offset;
                tb->cursor.logical_pos.x += wrap.movements;
                tb->cursor.visual_pos = wrap.pos;
                if (m.offset < chunk.len) {
                    break;
                }

                // Line wrap.
                tb->cursor.visual_pos.x = 0;
                tb->cursor.visual_pos.y += 1;
            }
        }
    }

    assert(tb->cursor.offset <= tb->text_length);
    assert(tb->cursor.logical_pos.x >= 0);
    assert(tb->cursor.logical_pos.y >= 0);
    assert(tb->cursor.logical_pos.y < tb->stats.lines);
    return tb->cursor.offset;
}

usize text_buffer_cursor_move_to_visual(TextBuffer* tb, Point pos)
{
    CoordType x = max(pos.x, 0);
    CoordType y = max(pos.y, 0);

    if (tb->word_wrap_columns < 0) {
        text_buffer_cursor_move_to_logical(tb, (Point){0, y});

        while (x > tb->cursor.visual_pos.x) {
            s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
            if (chunk.len == 0) {
                break;
            }

            UcdMeasurement m = ucd_measure_forward(chunk, 0, tb->cursor.visual_pos, x, -1, NULL);
            tb->cursor.offset += m.offset;
            tb->cursor.logical_pos.x += m.movements;
            tb->cursor.visual_pos = m.pos;
            if (m.offset < chunk.len) {
                break;
            }
        }
    } else {
        while (y < tb->cursor.visual_pos.y) {
            text_buffer_cursor_move_to_logical(tb, (Point){0, tb->cursor.logical_pos.y - 1});
        }

        if (y > tb->cursor.visual_pos.y || x > tb->cursor.visual_pos.x) {
            // The primary loop exit condition is down below `if (x <= tb->cursor.logical_pos.x)`.
            while (true) {
                s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
                if (chunk.len == 0) {
                    break;
                }

                CoordType column_stop = tb->word_wrap_columns;
                if (tb->cursor.visual_pos.y >= y) {
                    // We shouldn't have sought beyond the target line.
                    assert(tb->cursor.visual_pos.y == y);
                    column_stop = x;
                }

                UcdMeasurement wrap;
                UcdMeasurement m = ucd_measure_forward(chunk, 0, tb->cursor.visual_pos, column_stop, -1, &wrap);
                tb->cursor.offset += wrap.offset;
                tb->cursor.logical_pos.x += wrap.movements;
                tb->cursor.visual_pos = wrap.pos;
                if (m.offset < chunk.len && tb->cursor.visual_pos.y >= y) {
                    break;
                }

                // Line wrap.
                tb->cursor.visual_pos.x = 0;
                tb->cursor.visual_pos.y += 1;
                if (m.newline) {
                    tb->cursor.logical_pos.x = 0;
                    tb->cursor.offset += ucd_newlines_forward(chunk, m.offset, &tb->cursor.logical_pos.y, tb->cursor.logical_pos.y + 1) - m.offset;
                }
            }
        }
    }

    assert(tb->cursor.offset <= tb->text_length);
    assert(tb->cursor.logical_pos.x >= 0);
    assert(tb->cursor.logical_pos.y >= 0);
    assert(tb->cursor.logical_pos.y < tb->stats.lines);
    return tb->cursor.offset;
}

usize text_buffer_cursor_move_delta(TextBuffer* tb, CoordType cursor_movements)
{
    if (cursor_movements < 0) {
        const usize offset = tb->cursor.offset;
        text_buffer_cursor_move_to_logical(tb, (Point){tb->cursor.logical_pos.x - 1, tb->cursor.logical_pos.y});
        if (offset == tb->cursor.offset) {
            text_buffer_cursor_move_to_logical(tb, (Point){COORD_TYPE_SAFE_MAX, tb->cursor.logical_pos.y - 1});
        }
    } else if (cursor_movements > 0) {
        const usize offset = tb->cursor.offset;
        text_buffer_cursor_move_to_logical(tb, (Point){tb->cursor.logical_pos.x + 1, tb->cursor.logical_pos.y});
        if (offset == tb->cursor.offset) {
            text_buffer_cursor_move_to_logical(tb, (Point){0, tb->cursor.logical_pos.y + 1});
        }
    }
    return tb->cursor.offset;
}

usize text_buffer_extract(TextBuffer* tb, usize beg, usize end, c8* dst)
{
    assert(beg <= end && end <= tb->text_length);
    if (beg >= end || end > tb->text_length) {
        return 0;
    }

    c8* buf_end = dst;

    if (beg < tb->gap_off) {
        // s(tart), l(ength)
        usize s = beg;
        usize l = min(end, tb->gap_off) - beg;
        memcpy(buf_end, tb->text + s, l);
        buf_end += l;
    }

    if (end > tb->gap_off) {
        usize s = max(beg, tb->gap_off);
        usize l = end - s;
        memcpy(buf_end, tb->text + tb->gap_len + s, l);
        buf_end += l;
    }

    return buf_end - dst;
}

static void text_buffer_apply_change(TextBuffer* tb, TextBufferChange* change)
{
    tb->cursor = change->cursor;

    // Since we'll be deleting the inserted portion, we only need to allocate the delta.
    usize gap_len = max(change->removed.len, change->inserted.len) - change->inserted.len;
    c8* dst = text_buffer_allocate_gap(tb, tb->cursor.offset, gap_len);

    // excerpt of text_buffer_delete_right
    {
        usize count = change->inserted.len;
#ifndef NDEBUG
        memset(tb->text + tb->gap_off + tb->gap_len, 0xDD, count);
#endif
        tb->gap_len += count;
        tb->text_length -= count;
    }

    // excerpt of text_buffer_insert
    {
        s8 str = change->removed;

        memcpy(dst, str.beg, str.len);
        text_buffer_commit_gap(tb, str.len);

        TextBufferCursor cursor_before = tb->cursor;
        text_buffer_cursor_move_delta(tb, -1);
        usize backoff_count = cursor_before.offset - tb->cursor.offset;

        s8 chunk = text_buffer_read_forward(tb, tb->cursor.offset);
        chunk = s8_slice(chunk, 0, backoff_count + str.len);
        UcdMeasurement after = ucd_measure_forward(chunk, 0, tb->cursor.logical_pos, COORD_TYPE_MAX, COORD_TYPE_MAX, NULL);
        tb->cursor.offset += after.offset;
        tb->cursor.logical_pos = after.pos;
    }
}

void text_buffer_undo(TextBuffer* tb)
{
    if (!tb->undo_tail) {
        return;
    }

    TextBufferChange* change = tb->undo_tail;
    tb->undo_tail = change->prev;

    text_buffer_apply_change(tb, change);

    s8 removed = change->removed;
    s8 inserted = change->inserted;
    change->cursor = tb->cursor;
    change->removed = inserted;
    change->inserted = removed;
}

void text_buffer_redo(TextBuffer* tb)
{
    TextBufferChange* change = tb->undo_tail;
    if (!change) {
        change = tb->undo_base;
        if (!change) {
            return;
        }
    }

    change = change->next;
    if (!change) {
        return;
    }

    TextBufferCursor cursor = tb->cursor;
    tb->undo_tail = change;

    text_buffer_apply_change(tb, change);

    s8 removed = change->removed;
    s8 inserted = change->inserted;
    change->cursor = cursor;
    change->removed = inserted;
    change->inserted = removed;
}

void text_buffer_write(TextBuffer* tb, s8 str)
{
    if (str.len == 0) {
        return;
    }

    c8* dst = text_buffer_allocate_gap(tb, tb->cursor.offset, str.len);
    memcpy(dst, str.beg, str.len);

    s8 text = {tb->text, tb->gap_off + str.len, tb->gap_off + str.len};
    UcdMeasurement prev_bck;
    UcdMeasurement prev_fwd;
    if (tb->gap_off == 0 || tb->text[tb->gap_off - 1] == '\t') {
        prev_bck.offset = tb->gap_off;
        prev_bck.pos = tb->cursor.logical_pos;
        prev_bck.movements = 0;
        prev_fwd = prev_bck;
    } else {
        prev_bck = ucd_measure_backward(text, tb->gap_off, tb->cursor.logical_pos, -1, 1);
        prev_fwd = ucd_measure_forward(text, prev_bck.offset, prev_bck.pos, -1, 1, NULL);
    }
    UcdMeasurement next = ucd_measure_forward(text, prev_fwd.offset, prev_fwd.pos, -1, -1, NULL);

    usize off_beg = tb->cursor.offset;
    usize off_end = tb->cursor.offset;

    if (tb->overtype) {
        UcdMeasurement fwd = ucd_measure_forward(text_buffer_read_forward(tb, tb->cursor.offset), 0, tb->cursor.logical_pos, COORD_TYPE_MAX, next.movements, NULL);
        bool combines_with_preceding = prev_fwd.offset != tb->gap_off;
        off_beg = combines_with_preceding ? prev_bck.offset : prev_fwd.offset;
        off_end = tb->gap_off + fwd.offset;
    }

    text_buffer_record_push_change(tb, off_beg, off_end, str);
    text_buffer_commit_gap(tb, str.len);

    tb->gap_len += off_end - off_beg;
    tb->cursor.offset = next.offset;
    tb->cursor.logical_pos = next.pos;
}

void text_buffer_delete(TextBuffer* tb, CoordType cursor_movements)
{
    TextBufferCursor cursor_beg = tb->cursor;
    text_buffer_cursor_move_delta(tb, cursor_movements);
    TextBufferCursor cursor_end = tb->cursor;

    if (cursor_beg.offset == cursor_end.offset) {
        return;
    }
    if (cursor_beg.offset > cursor_end.offset) {
        TextBufferCursor tmp = cursor_beg;
        cursor_beg = cursor_end;
        cursor_end = tmp;
    }

    tb->cursor = cursor_beg;

    text_buffer_allocate_gap(tb, cursor_beg.offset, 0);
    text_buffer_record_push_change(tb, cursor_beg.offset, cursor_end.offset, (s8){});

    usize count = cursor_end.offset - cursor_beg.offset;
#ifndef NDEBUG
    memset(tb->text + tb->gap_off + tb->gap_len, 0xDD, count);
#endif
    tb->gap_len += count;
    tb->text_length -= count;
}
