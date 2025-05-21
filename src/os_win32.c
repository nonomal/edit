// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "os.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define CONSOLE_READ_NOWAIT 0x0002
typedef BOOL(WINAPI* fn_ReadConsoleInputExW)(HANDLE hConsoleInput, PINPUT_RECORD lpBuffer, DWORD nLength, LPDWORD lpNumberOfEventsRead, USHORT wFlags);

static fn_ReadConsoleInputExW s_ReadConsoleInputExW;
static HANDLE s_stdin;
static HANDLE s_stdout;
static DWORD s_stdin_cp_old;
static DWORD s_stdout_cp_old;
static DWORD s_stdin_mode_old;
static DWORD s_stdout_mode_old;
static bool s_inject_resize;
static bool s_wants_exit;

// UTF-16 text
typedef struct s16 {
    c16* beg;
    usize len;
    usize cap;
} s16;

static s8 s16_to_s8(Arena* arena, s16 str)
{
    s8 result = {};
    result.len = WideCharToMultiByte(CP_UTF8, 0, str.beg, (int)str.len, NULL, 0, NULL, NULL);
    result.beg = arena_mallocn(arena, c8, result.len);
    WideCharToMultiByte(CP_UTF8, 0, str.beg, (int)str.len, (LPSTR)result.beg, (int)result.len, NULL, NULL);
    return result;
}

static LPWSTR s8_to_lpwstr(Arena* arena, s8 str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, (LPSTR)str.beg, (int)str.len, NULL, 0);
    if (len <= 0) {
        return NULL;
    }

    LPWSTR beg = arena_mallocn(arena, wchar_t, len + 1);
    len = MultiByteToWideChar(CP_UTF8, 0, (LPSTR)str.beg, (int)str.len, (LPWSTR)beg, len);
    if (len <= 0) {
        return NULL;
    }

    beg[len] = 0;
    return beg;
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
    s_wants_exit = true;
    CancelIoEx(s_stdin, NULL);
    return true;
}

void os_init()
{
    s_ReadConsoleInputExW = (fn_ReadConsoleInputExW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadConsoleInputExW");

    SetConsoleCtrlHandler(console_ctrl_handler, true);

    s_stdin = GetStdHandle(STD_INPUT_HANDLE);
    s_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    s_stdin_cp_old = GetConsoleCP();
    s_stdout_cp_old = GetConsoleOutputCP();
    GetConsoleMode(s_stdin, &s_stdin_mode_old);
    GetConsoleMode(s_stdout, &s_stdout_mode_old);

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleMode(s_stdin, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(s_stdout, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
}

void os_deinit()
{
    SetConsoleCP(s_stdin_cp_old);
    SetConsoleOutputCP(s_stdout_cp_old);
    SetConsoleMode(s_stdin, s_stdin_mode_old);
    SetConsoleMode(s_stdout, s_stdout_mode_old);
}

void os_inject_window_size_into_stdin()
{
    s_inject_resize = true;
}

s8 os_read_stdin(Arena* arena)
{
    s8 text = {};

    if (s_inject_resize) {
        s_inject_resize = false;

        CONSOLE_SCREEN_BUFFER_INFOEX info = {};
        info.cbSize = sizeof(info);
        if (!GetConsoleScreenBufferInfoEx(s_stdout, &info)) {
            return (s8){};
        }

        int w = max(info.srWindow.Right - info.srWindow.Left + 1, 1);
        int h = max(info.srWindow.Bottom - info.srWindow.Top + 1, 1);
        s8_append_fmt(arena, &text, "\x1b[8;", h, ";", w, "t");
    }

    while (true) {
        INPUT_RECORD input[1024];
        DWORD read = 0;
        USHORT flags = text.len == 0 ? 0 : CONSOLE_READ_NOWAIT;
        if (!s_ReadConsoleInputExW(s_stdin, &input[0], array_size(input), &read, flags) || s_wants_exit) {
            return (s8){};
        }

        for (DWORD i = 0; i < read; ++i) {
            switch (input[i].EventType) {
            case KEY_EVENT: {
                const KEY_EVENT_RECORD* event = &input[i].Event.KeyEvent;
                if (event->bKeyDown && event->uChar.UnicodeChar) {
                    // Convert the UCS2 character to UTF8 without calling any functions.
                    c8 utf8[4];
                    usize utf8_len = 0;
                    if (event->uChar.UnicodeChar < 0x80) {
                        utf8[utf8_len++] = (c8)event->uChar.UnicodeChar;
                    } else if (event->uChar.UnicodeChar < 0x800) {
                        utf8[utf8_len++] = (c8)(0xC0 | (event->uChar.UnicodeChar >> 6));
                        utf8[utf8_len++] = (c8)(0x80 | (event->uChar.UnicodeChar & 0x3F));
                    } else {
                        utf8[utf8_len++] = (c8)(0xE0 | (event->uChar.UnicodeChar >> 12));
                        utf8[utf8_len++] = (c8)(0x80 | ((event->uChar.UnicodeChar >> 6) & 0x3F));
                        utf8[utf8_len++] = (c8)(0x80 | (event->uChar.UnicodeChar & 0x3F));
                    }
                    s8_append(arena, &text, (s8){utf8, utf8_len, utf8_len});
                }
                break;
            }
            case WINDOW_BUFFER_SIZE_EVENT: {
                const WINDOW_BUFFER_SIZE_RECORD* event = &input[i].Event.WindowBufferSizeEvent;
                // If I read xterm's documentation correctly, CSI 18 t reports the window size in characters.
                // CSI 8 ; height ; width t is the response. Of course, we didn't send the request,
                // but we can use this fake response to trigger the editor to resize itself.
                int w = max((int)event->dwSize.X, 1);
                int h = max((int)event->dwSize.Y, 1);
                s8_append_fmt(arena, &text, "\x1b[8;", h, ";", w, "t");
                break;
            }
            default:
                break;
            }
        }

        if (text.len != 0) {
            break;
        }
    }

    return text;
}

void os_write_stdout(s8 str)
{
    DWORD written = 0;
    WriteFile(s_stdout, str.beg, (DWORD)str.len, &written, NULL);
    assert(written == str.len);
}

void* os_virtual_reserve(usize size)
{
    void* base = NULL;

#ifndef NDEBUG
    static uintptr_t s_base_gen;
    s_base_gen += 0x0000100000000000;
    base = (void*)s_base_gen;
#endif

    return VirtualAlloc(base, size, MEM_RESERVE, PAGE_READWRITE);
}

void os_virtual_release(void* base)
{
    VirtualFree(base, 0, MEM_RELEASE);
}

void os_virtual_commit(void* base, usize size)
{
    VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE);
}

void* os_open_file_for_reading(s8 path)
{
    ScratchArena scratch = scratch_beg(NULL);
    LPWSTR p = s8_to_lpwstr(scratch.arena, path);
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    scratch_end(scratch);
    return h;
}

void* os_open_file_for_writing(s8 path)
{
    ScratchArena scratch = scratch_beg(NULL);
    LPWSTR p = s8_to_lpwstr(scratch.arena, path);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    scratch_end(scratch);
    return h;
}

usize os_file_size(void* handle)
{
    LARGE_INTEGER size = {};
    GetFileSizeEx(handle, &size);
    return (usize)size.QuadPart;
}

void os_close_file(void* handle)
{
    CloseHandle(handle);
}

usize os_read_file(void* handle, void* buffer, usize length)
{
    DWORD read = 0;
    ReadFile(handle, buffer, (DWORD)length, &read, NULL);
    return read;
}

static PlatformError os_get_last_error()
{
    i32 error = (i32)GetLastError();
    if (error <= 0) {
        return error;
    }
    return 0x80070000 | (error & 0x0000ffff);
}

PlatformError os_write_file(void* handle, void* buffer, usize length)
{
    u8* bytes = buffer;

    while (length != 0) {
        DWORD write = (DWORD)min(length, 1024 * 1024 * 1024);
        DWORD written = 0;

        if (!WriteFile(handle, bytes, write, &written, NULL)) {
            return os_get_last_error();
        }

        bytes += written;
        length -= written;
    }

    return 0;
}

void* os_load_library(const char* name)
{
    return LoadLibraryExA(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

os_anon_proc os_get_proc_address(void* library, const char* name)
{
    return (os_anon_proc)GetProcAddress(library, name);
}

#ifndef NDEBUG

void debug_print(const char* fmt, ...)
{
    char buffer[4096];

    va_list args;
    va_start(args, fmt);
    int len = wvsprintfA(&buffer[0], fmt, args);
    va_end(args);

    if (len > 0 || len < sizeof(buffer)) {
        OutputDebugStringA(&buffer[0]);
    }
}
#endif
