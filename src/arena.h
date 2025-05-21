// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "helpers.h"

typedef struct Arena {
    u8* base;
    usize capacity;
    usize commit;
    usize offset;
} Arena;

Arena* arena_create(usize capacity);
void arena_reset(Arena* arena);
void arena_destroy(Arena* arena);

typedef struct ScratchArena {
    Arena* arena;
    usize offset;
} ScratchArena;

ScratchArena scratch_beg(Arena* conflict);
void scratch_end(ScratchArena scratch);

void* arena_malloc_raw(Arena* arena, usize bytes, usize alignment);
void* arena_zalloc_raw(Arena* arena, usize bytes, usize alignment);

#define arena_malloc(arena, tp) (tp*)arena_malloc_raw(arena, sizeof(tp), max(_Alignof(tp), 16))
#define arena_zalloc(arena, tp) (tp*)arena_zalloc_raw(arena, sizeof(tp), max(_Alignof(tp), 16))
#define arena_mallocn(arena, tp, n) (tp*)arena_malloc_raw(arena, n * sizeof(tp), max(_Alignof(tp), 16))
#define arena_zallocn(arena, tp, n) (tp*)arena_zalloc_raw(arena, n * sizeof(tp), max(_Alignof(tp), 16))

typedef struct Slice {
    void* beg;
    usize len;
    usize cap;
} Slice;

void slice_reserve(Arena* arena, Slice* dst, usize cap, usize size, usize alignment);
#define slice_push(arena, dst, ...)                                                                        \
    do {                                                                                                   \
        __typeof__(__VA_ARGS__) item = __VA_ARGS__;                                                        \
        if ((dst)->len >= (dst)->cap) {                                                                    \
            slice_reserve(arena, (Slice*)(dst), (dst)->len + 1, _Alignof(__typeof__(item)), sizeof(item)); \
        }                                                                                                  \
        (dst)->beg[(dst)->len++] = item;                                                                   \
    } while (0)

#define s8_reserve(arena, dst, cap) slice_reserve(arena, (Slice*)dst, cap, 1, 1)
void s8_append(Arena* arena, s8* dst, s8 suffix);
void s8_append_repeat(Arena* arena, s8* dst, c8 ch, usize count);
void s8_append_repeat_string(Arena* arena, s8* dst, s8 rep, usize count);
void s8_append_i64(Arena* arena, s8* dst, i64 value);
void s8_append_u64(Arena* arena, s8* dst, u64 value);
void s8_replace(Arena* arena, s8* dst, usize beg, usize end, s8 replacement);

attribute_forceinline void s8_append_literal(Arena* arena, s8* dst, const char* str)
{
#pragma warning(suppress : 4210)
    extern size_t strlen(char const*);
    usize len = strlen(str);
    s8_append(arena, dst, (s8){(c8*)str, len, len});
}

// clang-format off
#define s8_append_auto(arena, dst, value) _Generic((value), \
    char*: s8_append_literal,                               \
    s8: s8_append,                                          \
    signed char: s8_append_i64,                             \
    signed short: s8_append_i64,                            \
    signed int: s8_append_i64,                              \
    signed long: s8_append_i64,                             \
    signed long long: s8_append_i64,                        \
    unsigned char: s8_append_u64,                           \
    unsigned short: s8_append_u64,                          \
    unsigned int: s8_append_u64,                            \
    unsigned long: s8_append_u64,                           \
    unsigned long long: s8_append_u64                       \
)(arena, dst, value)
// clang-format on

#define s8_append_fmt(arena, dst, ...)                               \
    do {                                                             \
        __VA_ARGS_HELPER(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1) \
        (arena, dst, __VA_ARGS__);                                   \
    } while (0)

#define __VA_ARGS_HELPER(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) __VA_ARGS_HELPER_##N
#define __VA_ARGS_HELPER_1(arena, dst, a1) \
    s8_append_auto(arena, dst, a1)
#define __VA_ARGS_HELPER_2(arena, dst, a1, a2) \
    s8_append_auto(arena, dst, a1);            \
    s8_append_auto(arena, dst, a2)
#define __VA_ARGS_HELPER_3(arena, dst, a1, a2, a3) \
    s8_append_auto(arena, dst, a1);                \
    s8_append_auto(arena, dst, a2);                \
    s8_append_auto(arena, dst, a3)
#define __VA_ARGS_HELPER_4(arena, dst, a1, a2, a3, a4) \
    s8_append_auto(arena, dst, a1);                    \
    s8_append_auto(arena, dst, a2);                    \
    s8_append_auto(arena, dst, a3);                    \
    s8_append_auto(arena, dst, a4)
#define __VA_ARGS_HELPER_5(arena, dst, a1, a2, a3, a4, a5) \
    s8_append_auto(arena, dst, a1);                        \
    s8_append_auto(arena, dst, a2);                        \
    s8_append_auto(arena, dst, a3);                        \
    s8_append_auto(arena, dst, a4);                        \
    s8_append_auto(arena, dst, a5)
#define __VA_ARGS_HELPER_6(arena, dst, a1, a2, a3, a4, a5, a6) \
    s8_append_auto(arena, dst, a1);                            \
    s8_append_auto(arena, dst, a2);                            \
    s8_append_auto(arena, dst, a3);                            \
    s8_append_auto(arena, dst, a4);                            \
    s8_append_auto(arena, dst, a5);                            \
    s8_append_auto(arena, dst, a6)
#define __VA_ARGS_HELPER_7(arena, dst, a1, a2, a3, a4, a5, a6, a7) \
    s8_append_auto(arena, dst, a1);                                \
    s8_append_auto(arena, dst, a2);                                \
    s8_append_auto(arena, dst, a3);                                \
    s8_append_auto(arena, dst, a4);                                \
    s8_append_auto(arena, dst, a5);                                \
    s8_append_auto(arena, dst, a6);                                \
    s8_append_auto(arena, dst, a7)
#define __VA_ARGS_HELPER_8(arena, dst, a1, a2, a3, a4, a5, a6, a7, a8) \
    s8_append_auto(arena, dst, a1);                                    \
    s8_append_auto(arena, dst, a2);                                    \
    s8_append_auto(arena, dst, a3);                                    \
    s8_append_auto(arena, dst, a4);                                    \
    s8_append_auto(arena, dst, a5);                                    \
    s8_append_auto(arena, dst, a6);                                    \
    s8_append_auto(arena, dst, a7);                                    \
    s8_append_auto(arena, dst, a8)
#define __VA_ARGS_HELPER_9(arena, dst, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    s8_append_auto(arena, dst, a1);                                        \
    s8_append_auto(arena, dst, a2);                                        \
    s8_append_auto(arena, dst, a3);                                        \
    s8_append_auto(arena, dst, a4);                                        \
    s8_append_auto(arena, dst, a5);                                        \
    s8_append_auto(arena, dst, a6);                                        \
    s8_append_auto(arena, dst, a7);                                        \
    s8_append_auto(arena, dst, a8);                                        \
    s8_append_auto(arena, dst, a9)
#define __VA_ARGS_HELPER_10(arena, dst, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    s8_append_auto(arena, dst, a1);                                              \
    s8_append_auto(arena, dst, a2);                                              \
    s8_append_auto(arena, dst, a3);                                              \
    s8_append_auto(arena, dst, a4);                                              \
    s8_append_auto(arena, dst, a5);                                              \
    s8_append_auto(arena, dst, a6);                                              \
    s8_append_auto(arena, dst, a7);                                              \
    s8_append_auto(arena, dst, a8);                                              \
    s8_append_auto(arena, dst, a9);                                              \
    s8_append_auto(arena, dst, a10)
