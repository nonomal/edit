// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "arena.h"
#include "buffer.h"
#include "input.h"
#include "loc.h"
#include "os.h"
#include "ucd.h"

// TODO:
// * Cut/Copy/Paste
// * Find with ICU
// * Line wrapping
// * Verifying UTF8 on load
// * Load non-UTF8 files
// * Focus into submenus
//   * How to know that the parent menu is still open?
// * Dialog for Save-As
//   * Perhaps as an input line in the status bar
// * Accelerators (文件 (F))
// * Undo/Redo batching/grouping
// * Output diffing / compression
// * OOM/IO error handling
// --------------------------------------------------
// * Replace
// * Multi-Cursor
// * Scrolling by dragging the track/thumb
// * When selecting text, hug the line contents like in VS Code

int main(int argc, c8** argv)
{
    os_init();

    if (argc != 2) {
        os_write_stdout(S("Usage: edit <file>\r\n"));
        os_deinit();
        return 1;
    }

    loc_init();
    VtParserState vt_parser_state = {};
    UiContext* ctx = ui_root_create();
    bool wants_save = false;
    bool wants_exit = false;

    {
        ScratchArena scratch = scratch_beg(NULL);

        s8 request = {};
        for (u32 i = 0; i < 16; ++i) {
            s8_append_fmt(scratch.arena, &request, "\033]4;", i, ";?\033\\");
        }
        s8_append(scratch.arena, &request, S("\033[c"));
        os_write_stdout(request);

        u32 indexed_colors[16] = {
            0xff000000,
            0xff212cbe,
            0xff3aae3f,
            0xff4a9abe,
            0xffbe4d20,
            0xffbe54bb,
            0xffb2a700,
            0xffbebebe,
            0xff808080,
            0xff303eff,
            0xff51ea58,
            0xff44c9ff,
            0xffff6a2f,
            0xffff74fc,
            0xfff0e100,
            0xffffffff,
        };

        for (;;) {
            s8 response = os_read_stdin(scratch.arena);
            if (response.len == 0) {
                goto exit;
            }

            c8* it = response.beg;
            c8* end = response.beg + response.len;

            do {
                // TODO: Need to fix vt_parse_next_token to support broken up OSC sequences and distinguishing between a completed and uncompleted ones.
                it = vt_parse_next_token(&vt_parser_state, it, end);

                if (vt_parser_state.kind == VT_TOKEN_KIND_OSC) {
                    s8 osc = vt_parser_state.osc;
                    // The response is in the form of `4;<color>;rgb:<r>/<g>/<b>`.
                    if (s8_starts_with(osc, S("4;"))) {
                        usize off = 2;

                        // Parse the <color>
                        u32 color = 0;
                        for (; off < osc.len && osc.beg[off] != ';'; ++off) {
                            color = color * 10 + (osc.beg[off] - '0');
                        }

                        if (color >= 16) {
                            continue;
                        }

                        osc = s8_slice(osc, off + 1, osc.len);
                        if (!s8_starts_with(osc, S("rgb:"))) {
                            continue;
                        }

                        u32 rgb = 0;
                        off = 4;

                        for (int j = 0; j < 3; j++) {
                            usize sep = s8_find(osc, off, '/');
                            usize len = sep - off;

                            if (len == 2 || len == 4) {
                                u64 val = s8_to_u64(s8_slice(osc, off, sep), 16);
                                if (len == 4) {
                                    val = (val * 0xff + 0x80) / 0xffff;
                                }
                                rgb = (rgb >> 8) | (u32)(val << 16);
                            }

                            off = sep + 1;
                        }

                        indexed_colors[color] = rgb | 0xff000000;
                    }
                } else if (vt_parser_state.kind == VT_TOKEN_KIND_CSI) {
                    if (vt_parser_state.csi.final_byte == 'c') {
                        goto setup_done;
                    }
                }
            } while (it != end);
        }

    setup_done:
        ui_root_setup_indexed_colors(ctx, indexed_colors);
        scratch_end(scratch);
    }

    TextBuffer* tb = text_buffer_create(text_buffer_allocator_default());
    text_buffer_read_file(tb, s8_from_ptr(argv[1]));

    {
        ScratchArena scratch = scratch_beg(NULL);

        s8 text = {};
        s8_reserve(scratch.arena, &text, tb->text_length);
        text.len = text_buffer_extract(tb, 0, tb->text_length, text.beg);

        UcdMeasurement wrap;
        ucd_measure_forward(text, 0, (Point){}, 20, -1, &wrap);

        scratch_end(scratch);
    }

    // 1049: Alternative Screen Buffer
    //   I put the ASB switch in the beginning, just in case the terminal performs
    //   some additional state tracking beyond the modes we enable/disable.
    // 1002: Cell Motion Mouse Tracking
    // 1006: SGR Mouse Mode
    // 2004: Bracketed Paste Mode
    os_write_stdout(S("\x1b[?1049h\x1b[?1002;1006;2004h"));
    os_inject_window_size_into_stdin();

    while (true) {
        ScratchArena scratch = scratch_beg(NULL);

        s8 input = os_read_stdin(scratch.arena);
        if (input.len == 0) {
            scratch_end(scratch);
            break;
        }

        for (;;) {
            bool last_pass = input.len == 0;
            UiInput ui_input = get_next_ui_input(&vt_parser_state, &input);

            // Windows is prone to sending broken/useless `WINDOW_BUFFER_SIZE_EVENT`s.
            // E.g. starting conhost will emit 3 in a row. Skip rendering in that case.
            if (ui_input.type == UI_INPUT_RESIZE && memcmp(&ui_input.resize, &ctx->size, sizeof(Size)) == 0) {
                continue;
            }

            ctx = ui_root_reset(ctx, ui_input);

            ui_menubar_begin(ctx);
            ui_attr_background_rgba(ctx, 0x3f7f7f7f);
            ui_attr_foreground_rgba(ctx, 0xffffffff);
            {
                if (ui_menubar_menu_begin(ctx, loc(LOC_File), 'F')) {
                    if (ui_menubar_menu_item(ctx, loc(LOC_File_Save), 'S', KEYBOARD_MODIFIER_CTRL | 'S')) {
                        wants_save = true;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_File_Save_As), 'A', KEYBOARD_MODIFIER_CTRL | KEYBOARD_MODIFIER_SHIFT | 'S')) {
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_File_Exit), 'X', KEYBOARD_MODIFIER_CTRL | 'Q')) {
                        wants_exit = true;
                    }
                    ui_menubar_menu_end(ctx);
                }
                if (ui_menubar_menu_begin(ctx, loc(LOC_Edit), 'E')) {
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Undo), 'U', KEYBOARD_MODIFIER_CTRL | 'Z')) {
                        // TODO: This and the ones below are essentially hacks. I'm not 100% sure how to inject the input yet.
                        // I could use bools and call the corresponding functions on the text buffer below, perhaps?
                        ctx->input_mouse_action = MOUSE_ACTION_NONE;
                        ctx->input_keyboard.key = VK_Z;
                        ctx->input_keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                        ctx->input_consumed = false;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Redo), 'R', KEYBOARD_MODIFIER_CTRL | 'Y')) {
                        ctx->input_mouse_action = MOUSE_ACTION_NONE;
                        ctx->input_keyboard.key = VK_Y;
                        ctx->input_keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                        ctx->input_consumed = false;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Cut), 'T', KEYBOARD_MODIFIER_CTRL | 'X')) {
                        ctx->input_mouse_action = MOUSE_ACTION_NONE;
                        ctx->input_keyboard.key = VK_X;
                        ctx->input_keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                        ctx->input_consumed = false;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Copy), 'C', KEYBOARD_MODIFIER_CTRL | 'C')) {
                        ctx->input_mouse_action = MOUSE_ACTION_NONE;
                        ctx->input_keyboard.key = VK_C;
                        ctx->input_keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                        ctx->input_consumed = false;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Paste), 'P', KEYBOARD_MODIFIER_CTRL | 'V')) {
                        ctx->input_mouse_action = MOUSE_ACTION_NONE;
                        ctx->input_keyboard.key = VK_V;
                        ctx->input_keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                        ctx->input_consumed = false;
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Find), 'F', KEYBOARD_MODIFIER_CTRL | 'F')) {
                    }
                    if (ui_menubar_menu_item(ctx, loc(LOC_Edit_Replace), 'R', KEYBOARD_MODIFIER_CTRL | 'H')) {
                    }
                    ui_menubar_menu_end(ctx);
                }
                if (ui_menubar_menu_begin(ctx, loc(LOC_Help), 'H')) {
                    if (ui_menubar_menu_item(ctx, loc(LOC_Help_About), 'A', 0)) {
                    }
                    ui_menubar_menu_end(ctx);
                }
            }
            ui_menubar_end(ctx);

            ui_focus_next_by_default(ctx);
            ui_textarea(ctx, tb, (Size){0, ui_root_get_size(ctx).height - 2});

            ui_container_begin_named(ctx, S("statusbar"));
            ui_attr_background_rgba(ctx, 0x3f7f7f7f);
            ui_attr_foreground_rgba(ctx, 0xffffffff);
            {
                s8 status = {};
                s8_append_fmt(scratch.arena, &status, "Ln ", tb->cursor.logical_pos.y + 1, ", Col ", tb->cursor.logical_pos.x + 1);
                s8_append(scratch.arena, &status, tb->overtype ? S("  OVR") : S("  INS"));
                ui_label(ctx, status);
                ui_attr_padding(ctx, (Rect){1, 0, 1, 0});
            }
            ui_container_end(ctx);

            if (wants_save) {
                text_buffer_write_file(tb, s8_from_ptr(argv[1]));
                wants_save = false;
            }

            if (wants_exit) {
                if (!tb->dirty) {
                    goto exit;
                }

                ui_container_begin_named(ctx, S("exit"));
                ui_attr_foreground_indexed(ctx, 15);
                ui_attr_background_indexed(ctx, 1);
                ui_attr_border(ctx);
                ui_attr_float(ctx, (UiFloatSpec){.gravity_x = 0.5f, .gravity_y = 0.5f, .offset_x = ctx->size.width / 2, .offset_y = ctx->size.height / 2});
                {
                    ui_label(ctx, loc(LOC_Exit_Dialog_Title));
                    ui_attr_padding(ctx, (Rect){2, 0, 2, 1});

                    ui_container_begin_named(ctx, S("buttons"));
                    ui_attr_grid_columns(ctx, (UiGridColumns){.count = 2, .widths = (CoordType[]){-1, -1}});
                    {
                        if (ui_button(ctx, loc(LOC_Exit_Dialog_Yes))) {
                            goto exit;
                        }
                        if (ui_button(ctx, loc(LOC_Exit_Dialog_No))) {
                            wants_exit = false;
                        }
                    }
                    ui_container_end(ctx);
                }
                ui_container_end(ctx);
            }

            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'S')) {
                wants_save = true;
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | KEYBOARD_MODIFIER_SHIFT | 'S')) {
                debug_print("Save As\n");
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'Q')) {
                wants_exit = true;
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'X')) {
                debug_print("Cut\n");
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'C')) {
                debug_print("Copy\n");
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'V')) {
                debug_print("Paste\n");
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'F')) {
                debug_print("Find\n");
            }
            if (ui_consume_shortcut(ctx, KEYBOARD_MODIFIER_CTRL | 'H')) {
                debug_print("Replace\n");
            }

            if (last_pass) {
                break;
            }
        }

        s8 output = ui_root_render(ctx);
        os_write_stdout(output);

        scratch_end(scratch);
    }

exit:
    // Same as in the beginning but in the reverse order.
    // It also includes DECSCUSR 0 to reset the cursor style.
    os_write_stdout(S("\x1b[?1002;1006;2004l\x1b[?1049l\x1b[0 q"));
    os_deinit();
    return 0;
}

#ifdef NODEFAULTLIB
extern int __cdecl __isa_available_init();
__declspec(dllimport) c8* __stdcall GetCommandLineA();
__declspec(dllimport) u32 GetModuleFileNameA(void* hModule, c8* lpFilename, u32 nSize);
__declspec(dllimport) void __stdcall ExitProcess(u32 uExitCode);

static bool Parse_Cmdline(const c8* cmdstart, c8** argv, c8* lpstr, int* numargs, int* numbytes)
{
    const c8* p;
    c8 c;
    int inquote;    /* 1 = inside quotes */
    int copychar;   /* 1 = copy char to *args */
    short numslash; /* num of backslashes seen */
    int nbytes = 0;

    int argc = *numargs; /* store the value to check for buffer overrun */

    *numargs = 1; /* the program name at least */

    /* first scan the program name, copy it, and count the bytes */
    p = cmdstart;
    if (argv) {
        *argv++ = lpstr;
        __analysis_assume(lpstr != NULL);
    } else {
        __analysis_assume(lpstr == NULL);
    }

    /* A quoted program name is handled here. The handling is much
       simpler than for other arguments. Basically, whatever lies
       between the leading double-quote and next one, or a terminal null
       character is simply accepted. Fancier handling is not required
       because the program name must be a legal NTFS/HPFS file name.
       Note that the double-quote characters are not copied, nor do they
       contribute to numbytes. */
    if (*p == '"') {
        /* scan from just past the first double-quote through the next
           double-quote, or up to a null, whichever comes first */
        while (*++p != '"' && *p != '\0') {
            nbytes += sizeof(c8);
            if (lpstr) {
                if (nbytes <= *numbytes) {
                    *lpstr++ = *p;
                }
            }
        }
        /* append the terminating null */
        nbytes += sizeof(c8);
        if (lpstr) {
            if (nbytes <= *numbytes) {
                *lpstr++ = '\0';
            }
        }

        /* if we stopped on a double-quote (usual case), skip over it */
        if (*p == '"')
            p++;
    } else {
        /* Not a quoted program name */
        do {
            nbytes += sizeof(c8);
            if (lpstr) {
                if (nbytes <= *numbytes) {
                    *lpstr++ = *p;
                }
            }

            c = (c8)*p++;

        } while (c > ' ');

        if (c == '\0') {
            p--;
        } else {
            if (lpstr) {
                if (nbytes <= *numbytes) {
                    *(lpstr - 1) = '\0';
                }
            }
        }
    }

    inquote = 0;

    /* loop on each argument */
    for (;;) {
        if (*p) {
            while (*p == ' ' || *p == '\t')
                ++p;
        }

        if (*p == '\0')
            break; /* end of args */

        /* scan an argument */
        if (argv && *numargs < argc) {
            *argv++ = lpstr; /* store ptr to arg */
        }
        ++*numargs; /* found another arg */

        /* loop through scanning one argument */
        for (;;) {
            copychar = 1;
            /* Rules: 2N backslashes + " ==> N backslashes and begin/end quote
                      2N+1 backslashes + " ==> N backslashes + literal "
                      N backslashes ==> N backslashes */
            numslash = 0;
            while (*p == '\\') {
                /* count number of backslashes for use below */
                ++p;
                ++numslash;
            }
            if (*p == '"') {
                /* if 2N backslashes before, start/end quote, otherwise
                   copy literally */
                if (numslash % 2 == 0) {
                    if (inquote)
                        if (p[1] == '"')
                            p++; /* Double quote inside quoted string */
                        else     /* skip first quote char and copy second */
                            copychar = 0;
                    else
                        copychar = 0; /* don't copy quote */

                    inquote = !inquote;
                }
                numslash /= 2; /* divide numslash by two */
            }

            /* copy slashes */
            while (numslash--) {
                nbytes += sizeof(c8);
                if (lpstr) {
                    if (nbytes <= *numbytes) {
                        *lpstr++ = '\\';
                    }
                }
            }

            /* if at end of arg, break loop */
            if (*p == '\0' || (!inquote && (*p == ' ' || *p == '\t')))
                break;

            /* copy character into argument */
            if (copychar) {
                nbytes += sizeof(c8);
                if (lpstr) {
                    if (nbytes <= *numbytes) {
                        *lpstr++ = *p;
                    }
                }
            }
            ++p;
        }

        /* null-terminate the argument */

        nbytes += sizeof(c8);
        if (lpstr) {
            if (nbytes <= *numbytes) {
                *lpstr++ = '\0'; /* terminate string */
            }
        }
    }

    if (lpstr) {
        return nbytes <= *numbytes && *numargs <= argc;
    } else {
        *numbytes = nbytes;
        return true;
    }
}

static c8** CommandLineToArgvA(Arena* arena, const c8* lpCmdLine, int* pNumArgs)
{
    c8** argv_U = NULL;
    const c8* cmdstart; /* start of command line to parse */
    int numbytes = 0;
    c8 pgmname[260];

    if (NULL != pNumArgs) {
        /* Get the program name pointer from Win32 Base */

        GetModuleFileNameA(NULL, pgmname, sizeof(pgmname) / sizeof(c8));

        /* if there's no command line at all (won't happen from cmd.exe, but
           possibly another program), then we use pgmname as the command line
           to parse, so that argv[0] is initialized to the program name */
        cmdstart = *lpCmdLine == '\0' ? pgmname : lpCmdLine;

        /* set *pNumArgs to 1 to start with */
        *pNumArgs = 1;

        /* first find out how much space is needed to store args */
        Parse_Cmdline(cmdstart, NULL, NULL, pNumArgs, &numbytes);

        // 0 < ((*pNumArgs+1) * sizeof(c8*) + numbytes) <= INT_MAX
        if (0 < *pNumArgs && *pNumArgs <= (int)((INT_MAX - numbytes) / sizeof(c8*) - 1)) {
            /* allocate space for argv[] vector and strings */
            argv_U = (c8**)arena_zalloc_raw(arena, (*pNumArgs + 1) * sizeof(c8*) + numbytes, 16);
            if (argv_U) {
                /* store args and argv ptrs in just allocated block */
                bool bVal = Parse_Cmdline(cmdstart, argv_U, (c8*)argv_U + (*pNumArgs + 1) * sizeof(c8*), pNumArgs, &numbytes);
                if (!bVal) {
                    argv_U = NULL;
                }
            }
        }
    }

    return argv_U;
}

extern void mainCRTStartup()
{
    // __isa_available_init ensures that `extern int __isa_available` has the right value.
    // The msvcrt/vcruntime technically initializes a bunch of other things, but we need none of those.
    __isa_available_init();

    ScratchArena scratch = scratch_beg(NULL);
    c8* command_line = GetCommandLineA();
    int argc = 0;
    c8** argv = CommandLineToArgvA(scratch.arena, command_line, &argc);

    ExitProcess(main(argc, argv));
}
#endif
