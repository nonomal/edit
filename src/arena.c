// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "arena.h"

#include <stdlib.h>
#include <threads.h>

#include "os.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

#define ALLOC_CHUNK_SIZE (usize)(64 * 1024)

Arena* arena_create(usize capacity)
{
    capacity = (capacity + ALLOC_CHUNK_SIZE - 1) & ~(ALLOC_CHUNK_SIZE - 1);

    void* base = os_virtual_reserve(capacity);
    if (!base) {
        abort();
    }

    Arena temp = {
        .capacity = capacity,
        .base = base,
    };
    Arena* arena = arena_malloc(&temp, Arena);
    memcpy(arena, &temp, sizeof(Arena));
    return arena;
}

void arena_reset(Arena* arena)
{
#ifndef NDEBUG
    memset(arena->base + sizeof(Arena), 0xDD, arena->offset);
#endif
    arena->offset = sizeof(Arena);
}

void arena_destroy(Arena* arena)
{
    os_virtual_release(arena->base);
}

static thread_local Arena* s_scratch[2];

attribute_noinline static void scratch_init()
{
    for (int i = 0; i < 2; i++) {
        s_scratch[i] = arena_create(64 * 1024 * 1024);
    }
}

ScratchArena scratch_beg(Arena* conflict)
{
    if (!s_scratch[0]) {
        scratch_init();
    }

    Arena* arena = s_scratch[conflict == s_scratch[0]];
    return (ScratchArena){arena, arena->offset};
}

void scratch_end(ScratchArena scratch)
{
#ifndef NDEBUG
    memset(scratch.arena->base + scratch.offset, 0xDD, scratch.arena->offset - scratch.offset);
#endif

    scratch.arena->offset = scratch.offset;
}

void* arena_malloc_raw(Arena* arena, usize bytes, usize alignment)
{
    bytes = max(bytes, 1);
    alignment = max(alignment, 1);

    usize offset_old = arena->offset;
    usize beg = (offset_old + alignment - 1) & ~(alignment - 1);
    usize end = beg + bytes;
    u8* ptr = arena->base + beg;

    arena->offset = end;

    if (end > arena->commit) {
        usize commit_old = arena->commit;
        usize commit_new = (end + ALLOC_CHUNK_SIZE - 1) & ~(ALLOC_CHUNK_SIZE - 1);

        if (commit_new > arena->capacity) {
            abort();
        }

        os_virtual_commit(arena->base + commit_old, commit_new - commit_old);
        arena->commit = commit_new;
    }

#ifndef NDEBUG
    end += 128;
    end = min(end, arena->commit);
    memset(arena->base + offset_old, 0xCD, end - offset_old);
#endif

    return ptr;
}

void* arena_zalloc_raw(Arena* arena, usize bytes, usize alignment)
{
    void* ptr = arena_malloc_raw(arena, bytes, alignment);
    memset(ptr, 0, bytes);
    return ptr;
}

void slice_reserve(Arena* arena, Slice* dst, usize cap, usize size, usize alignment)
{
    if (cap <= dst->cap) {
        return;
    }

    alignment = max(alignment, 1);

    u8* beg_old = dst->beg;
    usize cap_old = dst->cap;

    usize cap_new = dst->cap * 2;
    cap_new = max(cap_new, cap);
    cap_new = max(cap_new, 128);

    usize bytes_old = cap_old * size;
    usize bytes_new = cap_new * size;

    dst->cap = cap_new;

    if (beg_old + bytes_old == arena->base + arena->offset) {
        arena_malloc_raw(arena, bytes_new - bytes_old, 1);
    } else {
        dst->beg = arena_malloc_raw(arena, bytes_new, alignment);
        memcpy(dst->beg, beg_old, bytes_old);
    }
}

void s8_append(Arena* arena, s8* dst, s8 suffix)
{
    if (suffix.len == 0) {
        return;
    }

    slice_reserve(arena, (Slice*)dst, dst->len + suffix.len, 1, 1);
    memcpy(dst->beg + dst->len, suffix.beg, suffix.len);
    dst->len += suffix.len;
}

void s8_append_repeat(Arena* arena, s8* dst, c8 ch, usize count)
{
    if (count == 0) {
        return;
    }

    slice_reserve(arena, (Slice*)dst, dst->len + count, 1, 1);
    memset(dst->beg + dst->len, ch, count);
    dst->len += count;
}

void s8_append_repeat_string(Arena* arena, s8* dst, s8 rep, usize count)
{
    if (count == 0) {
        return;
    }

    usize total_len = rep.len * count;

    slice_reserve(arena, (Slice*)dst, dst->len + total_len, 1, 1);

    u8* dst_beg = dst->beg + dst->len;
    u8* dst_it = dst_beg;
    u8* src = rep.beg;
    usize remaining = total_len;
    usize len_next = 0;
    usize len = rep.len;

    dst->len += total_len;

    // This loop efficiently repeats the string `rep` `count` times by doubling its length in each iteration.
    // I.e. "abc" -> "abcabc" -> "abcabcabcabc" -> "abcabcabcabcabcabcabcabc" -> ...
    // It uses `len` / `len_next` to ensure that the 1st and 2nd iteration both operate with `len == rep.len`,
    // since in the 1st iteration we need to copy `rep` into `dst` and in the 2nd iteration we perform the first doubling.
    for (;;) {
        memcpy(dst_it, src, len);

        remaining -= len;
        if (remaining == 0) {
            return;
        }

        dst_it += len;
        src = dst_beg;

        len_next += len;
        len_next = min(len_next, remaining);
        len = len_next;
    }
}

static void write_decimal(Arena* arena, s8* dst, u64 v, bool neg)
{
    // Mapping 2 digits at a time speeds things up a lot because half the divisions are necessary.
    // I got this idea from https://github.com/fmtlib/fmt which in turn got it
    // from the talk "Three Optimization Tips for C++" by Andrei Alexandrescu.
    static const u8 lut[] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    const u64 log10 = u64_log10(v);
    u64 digits = log10 + 1;
    usize new_len = dst->len + digits + neg;

    slice_reserve(arena, (Slice*)dst, new_len, 1, 1);
    dst->len = new_len;

    u8* end = dst->beg + new_len;
    u8* p = end;

    while (digits > 1) {
        const u8* s = &lut[(v % 100) * 2];
        *--p = s[1];
        *--p = s[0];
        v /= 100;
        digits -= 2;
    }
    if (digits & 1) {
        *--p = (u8)('0' + v);
    }
    if (neg) {
        *p = '-';
    }
}

void s8_append_i64(Arena* arena, s8* dst, i64 value)
{
    write_decimal(arena, dst, value < 0 ? -value : value, value < 0);
}

void s8_append_u64(Arena* arena, s8* dst, u64 value)
{
    write_decimal(arena, dst, value, false);
}

void s8_replace(Arena* arena, s8* dst, usize beg, usize end, s8 replacement)
{
    usize len_old = dst->len;
    end = min(end, len_old);
    beg = min(beg, end);

    usize len_new = dst->len - (end - beg) + replacement.len;
    slice_reserve(arena, (Slice*)dst, len_new, 1, 1);
    memmove(dst->beg + beg + replacement.len, dst->beg + end, len_old - end);
    memcpy(dst->beg + beg, replacement.beg, replacement.len);
    dst->len = len_new;
}
