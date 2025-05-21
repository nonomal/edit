// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef unreachable
#define unreachable() __assume(false)
#endif

#ifdef _MSC_VER
#include <intrin.h>
#define os_WIN32 1
#define attribute_forceinline __forceinline
#define attribute_noinline __declspec(noinline)
#else
#define os_UNIX 1
#define attribute_forceinline __attribute__((always_inline)) inline
#define attribute_noinline __attribute__((noinline))
#endif

#if defined(__x86_64__) || (defined(_M_X64) && !defined(_M_ARM64EC))
#define PLATFORM_X64 1
#elif defined(__aarch64__) || (defined(_M_ARM64) || defined(_M_ARM64EC))
#define PLATFORM_ARM64 1
#else
#error "Unsupported platform"
#endif

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
typedef ptrdiff_t isize;
typedef size_t usize;
typedef float f32;
typedef double f64;
#define USIZE_MAX ((usize) - 1)

typedef i32 CoordType;
#define COORD_TYPE_MIN (-2147483647 - 1)
#define COORD_TYPE_MAX 2147483647
#define COORD_TYPE_SAFE_MIN (-32767 - 1)
#define COORD_TYPE_SAFE_MAX 32767

#define array_size(a) (sizeof(a) / sizeof((a)[0]))

// clang-format off
attribute_forceinline i32 min_i32(i32 a, i32 b) { return a < b ? a : b; }
attribute_forceinline u32 min_u32(u32 a, u32 b) { return a < b ? a : b; }
attribute_forceinline i64 min_i64(i64 a, i64 b) { return a < b ? a : b; }
attribute_forceinline u64 min_u64(u64 a, u64 b) { return a < b ? a : b; }
attribute_forceinline f32 min_f32(f32 a, f32 b) { return a < b ? a : b; }
attribute_forceinline f64 min_f64(f64 a, f64 b) { return a < b ? a : b; }

attribute_forceinline i32 max_i32(i32 a, i32 b) { return a > b ? a : b; }
attribute_forceinline u32 max_u32(u32 a, u32 b) { return a > b ? a : b; }
attribute_forceinline i64 max_i64(i64 a, i64 b) { return a > b ? a : b; }
attribute_forceinline u64 max_u64(u64 a, u64 b) { return a > b ? a : b; }
attribute_forceinline f32 max_f32(f32 a, f32 b) { return a > b ? a : b; }
attribute_forceinline f64 max_f64(f64 a, f64 b) { return a > b ? a : b; }

attribute_forceinline i32 clamp_i32(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
attribute_forceinline u32 clamp_u32(u32 v, u32 lo, u32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
attribute_forceinline i64 clamp_i64(i64 v, i64 lo, i64 hi) { return v < lo ? lo : (v > hi ? hi : v); }
attribute_forceinline u64 clamp_u64(u64 v, u64 lo, u64 hi) { return v < lo ? lo : (v > hi ? hi : v); }
attribute_forceinline f32 clamp_f32(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
attribute_forceinline f64 clamp_f64(f64 v, f64 lo, f64 hi) { return v < lo ? lo : (v > hi ? hi : v); }

#define min(a, b) _Generic((a), i32 : min_i32, u32 : min_u32, i64 : min_i64, u64 : min_u64, f32 : min_f32, f64 : min_f64)(a, b)
#define max(a, b) _Generic((a), i32 : max_i32, u32 : max_u32, i64 : max_i64, u64 : max_u64, f32 : max_f32, f64 : max_f64)(a, b)
#define clamp(v, lo, hi) _Generic((v), i32 : clamp_i32, u32 : clamp_u32, i64 : clamp_i64, u64 : clamp_u64, f32 : clamp_f32, f64 : clamp_f64)(v, lo, hi)
// clang-format on

typedef struct Point {
    CoordType x;
    CoordType y;
} Point;

typedef struct Size {
    CoordType width;
    CoordType height;
} Size;

typedef struct Rect {
    CoordType left;
    CoordType top;
    CoordType right;
    CoordType bottom;
} Rect;

bool rect_is_empty(Rect rect);
bool rect_contains(Rect rect, Point point);
Rect rect_intersect(Rect a, Rect b);

typedef u8 c8;   // UTF-8 code unit
typedef u16 c16; // UTF-16 code unit

// UTF-8 text
typedef struct s8 {
    c8* beg;
    usize len;
    usize cap;
} s8;

attribute_forceinline s8 S(const char* str)
{
    usize len = strlen(str);
    return (s8){(c8*)str, len, len};
}

s8 s8_from_ptr(const c8* ptr);
s8 s8_slice(s8 str, usize beg, usize end);
bool s8_starts_with(s8 str, s8 prefix);
// NOTE: Calling s8_transform_lowercase_ascii on non-ASCII strings will result in garbage.
void s8_transform_lowercase_ascii(s8 str);
usize s8_find(s8 str, usize off, c8 ch);
u64 s8_to_u64(s8 str, int base);

u64 hash(u64 seed, void* data, usize len);
u64 hash_s8(u64 seed, s8 str);

u64 u64_log10(u64 v);

#define DOUBLY_LINKED_ENTRY(type) \
    type* prev;                   \
    type* next;

#define DOUBLY_LINKED_INIT(list, field)  \
    (list)->field.prev = &(list)->field; \
    (list)->field.next = &(list)->field;

#define DOUBLY_LINKED_PUSH_TAIL(list, field, item)  \
    do {                                            \
        __typeof__(list) prev = (list)->field.prev; \
        entry->field.next = (list);                 \
        entry->field.prev = prev;                   \
        prev->field.next = item;                    \
        (list)->field.prev = entry;                 \
    } while (0)

#define DOUBLY_LINKED_INSERT_TAIL(head, obj, field) \
    (obj)->field.Flink = (head);                    \
    (obj)->field.Blink = (head)->field.Blink;       \
    (head)->field.Blink->Flink = (obj);             \
    (head)->field.Blink = (obj);

// Reference code for single/doubly linked lists with sentinels.
// The problem with that is that it requires ugly offsetof() casts to get the owning structure.
#if 0
typedef struct SinglyLinkedEntry {
    struct SinglyLinkedEntry* next;
} SinglyLinkedEntry;

attribute_forceinline SinglyLinkedEntry* singly_linked_pop_head(SinglyLinkedEntry* list)
{
    SinglyLinkedEntry* entry = list->next;
    if (entry != NULL) {
        list->next = entry->next;
    }
    return entry;
}

attribute_forceinline void singly_linked_push_head(SinglyLinkedEntry* list, SinglyLinkedEntry* entry)
{
    entry->next = list->next;
    list->next = entry;
}

typedef struct DoublyLinkedEntry {
    struct DoublyLinkedEntry* Flink;
    struct DoublyLinkedEntry* Blink;
} DoublyLinkedEntry;

attribute_forceinline void doubly_linked_init(DoublyLinkedEntry* list)
{
    list->Flink = list->Blink = list;
}

bool attribute_forceinline doubly_linked_is_empty(const DoublyLinkedEntry* list)
{
    return list->Flink == list;
}

attribute_forceinline void doubly_linked_remove_entry(DoublyLinkedEntry* entry)
{
    DoublyLinkedEntry* Flink = entry->Flink;
    DoublyLinkedEntry* Blink = entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
}

attribute_forceinline DoublyLinkedEntry* doubly_linked_remove_head(DoublyLinkedEntry* list)
{
    DoublyLinkedEntry* entry = list->Flink;
    DoublyLinkedEntry* Flink = entry->Flink;
    list->Flink = Flink;
    Flink->Blink = list;
    return entry;
}

attribute_forceinline DoublyLinkedEntry* doubly_linked_remove_tail(DoublyLinkedEntry* list)
{
    DoublyLinkedEntry* entry = list->Blink;
    DoublyLinkedEntry* Blink = entry->Blink;
    list->Blink = Blink;
    Blink->Flink = list;
    return entry;
}

attribute_forceinline void doubly_linked_push_head(DoublyLinkedEntry* list, DoublyLinkedEntry* entry)
{
    DoublyLinkedEntry* Flink = list->Flink;
    entry->Flink = Flink;
    entry->Blink = list;
    Flink->Blink = entry;
    list->Flink = entry;
}

attribute_forceinline void doubly_linked_push_tail(DoublyLinkedEntry* list, DoublyLinkedEntry* entry)
{
    DoublyLinkedEntry* Blink = list->Blink;
    entry->Flink = list;
    entry->Blink = Blink;
    Blink->Flink = entry;
    list->Blink = entry;
}

attribute_forceinline void doubly_linked_append_list(DoublyLinkedEntry* list, DoublyLinkedEntry* append)
{
    DoublyLinkedEntry* end = list->Blink;
    list->Blink->Flink = append;
    list->Blink = append->Blink;
    append->Blink->Flink = list;
    append->Blink = end;
}
#endif
