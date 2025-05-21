// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "input.h"

UiInput get_next_ui_input(VtParserState* vt_parser_state, s8* input)
{
    UiInput result = {};
    c8* it = input->beg;
    c8* end = input->beg + input->len;

    while (it != end) {
        it = vt_parse_next_token(vt_parser_state, it, end);
        switch (vt_parser_state->kind) {
        case VT_TOKEN_KIND_TEXT:
            result.type = UI_INPUT_TEXT;
            result.text = vt_parser_state->text;
            goto done;
        case VT_TOKEN_KIND_CTRL:
            switch (vt_parser_state->ctrl) {
            case VK_NULL:
            case VK_TAB:
            case VK_RETURN:
                result.type = UI_INPUT_KEYBOARD;
                result.keyboard.key = vt_parser_state->ctrl;
                goto done;
            case '\x01':
            case '\x02':
            case '\x03':
            case '\x04':
            case '\x05':
            case '\x06':
            case '\x07':
            case '\x08':
            // case '\x09': // VK_TAB
            case '\x0a':
            case '\x0b':
            case '\x0c':
            // case '\x0d': // VK_RETURN
            case '\x0e':
            case '\x0f':
            case '\x10':
            case '\x11':
            case '\x12':
            case '\x13':
            case '\x14':
            case '\x15':
            case '\x16':
            case '\x17':
            case '\x18':
            case '\x19':
            case '\x1a':
                // Ctrl+A-Z with a few exceptions, because e.g. Ctrl+I is VK_TAB.
                result.type = UI_INPUT_KEYBOARD;
                result.keyboard.key = (UiInputKeyboardKey)(vt_parser_state->ctrl | 0b1000000);
                result.keyboard.modifiers = KEYBOARD_MODIFIER_CTRL;
                goto done;
            case '\x7f':
                result.type = UI_INPUT_KEYBOARD;
                result.keyboard.key = VK_BACK;
                goto done;
            default:
                assert(false);
                break;
            }
            break;
        case VT_TOKEN_KIND_ESC:
            if (vt_parser_state->esc >= ' ' && vt_parser_state->esc <= '~') {
                result.type = UI_INPUT_KEYBOARD;
                result.keyboard.key = vt_parser_state->esc;
                result.keyboard.modifiers = KEYBOARD_MODIFIER_ALT;
                goto done;
            }
            assert(false);
            break;
        case VT_TOKEN_KIND_SS3:
            if (vt_parser_state->ss3 >= 'P' && vt_parser_state->ss3 <= 'S') {
                result.type = UI_INPUT_KEYBOARD;
                result.keyboard.key = vt_parser_state->ss3 - 'P' + VK_F1;
                goto done;
            }
            assert(false);
            break;
        case VT_TOKEN_KIND_CSI:
            if (vt_parser_state->csi.final_byte <= 'H') {
                static const u8 lut[] = {
                    ['A' - 'A'] = VK_UP,
                    ['B' - 'A'] = VK_DOWN,
                    ['C' - 'A'] = VK_RIGHT,
                    ['D' - 'A'] = VK_LEFT,
                    ['F' - 'A'] = VK_END,
                    ['H' - 'A'] = VK_HOME,
                };
                u8 vk = lut[vt_parser_state->csi.final_byte - 'A'];
                if (vk) {
                    result.type = UI_INPUT_KEYBOARD;
                    result.keyboard.key = vk;
                    goto parse_modifiers;
                }
                break;
            }

            switch (vt_parser_state->csi.final_byte) {
            case '~': {
                static const u8 lut[] = {
                    [1] = VK_HOME,
                    [2] = VK_INSERT,
                    [3] = VK_DELETE,
                    [4] = VK_END,
                    [5] = VK_PRIOR,
                    [6] = VK_NEXT,
                    [15] = VK_F5,
                    [17] = VK_F6,
                    [18] = VK_F7,
                    [19] = VK_F8,
                    [20] = VK_F9,
                    [21] = VK_F10,
                    [23] = VK_F11,
                    [24] = VK_F12,
                    [25] = VK_F13,
                    [26] = VK_F14,
                    [28] = VK_F15,
                    [29] = VK_F16,
                    [31] = VK_F17,
                    [32] = VK_F18,
                    [33] = VK_F19,
                    [34] = VK_F20,
                };
                i32 p0 = vt_parser_state->csi.params[0];
                if (p0 <= array_size(lut)) {
                    u8 vk = lut[p0];
                    if (vk) {
                        result.type = UI_INPUT_KEYBOARD;
                        result.keyboard.key = vk;
                        goto parse_modifiers;
                    }
                }
                break;
            }
            case 'm':
            case 'M':
                if (vt_parser_state->csi.private_byte == '<') {
                    i32 btn = vt_parser_state->csi.params[0];

                    result.type = UI_INPUT_MOUSE;
                    result.mouse.action = MOUSE_ACTION_NONE;
                    if (btn & 0x40) {
                        result.mouse.action = MOUSE_ACTION_SCROLL;
                        result.mouse.scroll.y += btn & 0x01 ? 3 : -3;
                    } else if (vt_parser_state->csi.final_byte == 'M') {
                        MouseAction actions[] = {MOUSE_ACTION_LEFT, MOUSE_ACTION_MIDDLE, MOUSE_ACTION_RIGHT, MOUSE_ACTION_NONE};
                        result.mouse.action = actions[btn & 0x03];
                    }

                    result.mouse.modifier = KEYBOARD_MODIFIER_NONE;
                    result.mouse.modifier |= btn & 0x04 ? KEYBOARD_MODIFIER_SHIFT : 0;
                    result.mouse.modifier |= btn & 0x08 ? KEYBOARD_MODIFIER_ALT : 0;
                    result.mouse.modifier |= btn & 0x10 ? KEYBOARD_MODIFIER_CTRL : 0;

                    result.mouse.position.x = vt_parser_state->csi.params[1] - 1;
                    result.mouse.position.y = vt_parser_state->csi.params[2] - 1;
                    goto done;
                }
                break;
            case 't':
                switch (vt_parser_state->csi.params[0]) {
                case 8: // Window Size
                    result.type = UI_INPUT_RESIZE;
                    result.resize.width = max(vt_parser_state->csi.params[2], 1);
                    result.resize.height = max(vt_parser_state->csi.params[1], 1);
                    assert(result.resize.width > 0 && result.resize.height > 0);
                    assert(result.resize.width < 32768 && result.resize.height < 32768);
                    goto done;
                default:
                    break;
                }
                break;
            default:
                assert(false);
                break;
            }
            break;
        default:
            break;
        }
    }

parse_modifiers:
    i32 p1 = max(vt_parser_state->csi.params[1] - 1, 0);
    if (p1 & 0x01) {
        result.keyboard.modifiers |= KEYBOARD_MODIFIER_SHIFT;
    }
    if (p1 & 0x02) {
        result.keyboard.modifiers |= KEYBOARD_MODIFIER_ALT;
    }
    if (p1 & 0x04) {
        result.keyboard.modifiers |= KEYBOARD_MODIFIER_CTRL;
    }

done:
    *input = s8_slice(*input, it - input->beg, USIZE_MAX);
    return result;
}
