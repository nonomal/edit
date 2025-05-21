// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "arena.h"

typedef u32 PlatformError;

void os_init();
void os_deinit();
void os_inject_window_size_into_stdin();
s8 os_read_stdin(Arena* arena);
void os_write_stdout(s8 str);

void* os_virtual_reserve(usize size);
void os_virtual_release(void* base);
void os_virtual_commit(void* base, usize size);

void* os_open_file_for_reading(s8 path);
void* os_open_file_for_writing(s8 path);
usize os_file_size(void* handle);
void os_close_file(void* handle);
usize os_read_file(void* handle, void* buffer, usize length);
PlatformError os_write_file(void* handle, void* buffer, usize length);

#ifdef _MSC_VER
// C4047: '=': '...' differs in levels of indirection from 'os_anon_proc'
// C4113: 'os_anon_proc' differs in parameter lists from '...'
// C4113: '=': incompatible types - from 'os_anon_proc' to '...'
#define OS_SUPPRESS_GET_PROC_NAGGING_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4047 4113 4133))
#define OS_SUPPRESS_GET_PROC_NAGGING_END __pragma(warning(pop))
#else
#define OS_SUPPRESS_GET_PROC_NAGGING_BEGIN
#define OS_SUPPRESS_GET_PROC_NAGGING_END
#endif

typedef void (*os_anon_proc)();
void* os_load_library(const char* name);
os_anon_proc os_get_proc_address(void* library, const char* name);

inline u32 os_bit_width_u32(u32 val)
{
    unsigned long index;
    if (!_BitScanReverse(&index, val)) {
        index = (unsigned long)-1;
    }
    return index + 1;
}

inline u32 os_bit_width_u64(u64 val)
{
    unsigned long index;
    if (!_BitScanReverse64(&index, val)) {
        index = (unsigned long)-1;
    }
    return index + 1;
}

#define os_bit_width(val) _Generic((val), u32: os_bit_width_u32, u64: os_bit_width_u64)(val)

inline u32 os_bit_ceil_u32(u32 val)
{
    return (u32)1 << os_bit_width_u32(val - 1);
}

inline u64 os_bit_ceil_u64(u64 val)
{
    return (u64)1 << os_bit_width_u64(val - 1);
}

#define os_bit_ceil(val) _Generic((val), u32: os_bit_ceil_u32, u64: os_bit_ceil_u64)(val)

#ifndef NDEBUG
void debug_print(const char* fmt, ...);
#else
#define debug_print(...) ((void)0)
#endif
