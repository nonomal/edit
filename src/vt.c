// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "vt.h"

c8* vt_parse_next_token(VtParserState* state, c8* const beg, c8* const end)
{
    c8* it = beg;

    state->kind = VT_TOKEN_KIND_PENDING;

    while (it != end) {
        switch (state->_state) {
        case VT_PARSER_STATE_KIND_GROUND:
            if (*it == '\x1b') {
                state->_state = VT_PARSER_STATE_KIND_ESC;
                it += 1;
                break;
            }
            if (*it < '\x20' || *it == '\x7f') {
                state->kind = VT_TOKEN_KIND_CTRL;
                state->ctrl = *it++;
            } else {
                state->kind = VT_TOKEN_KIND_TEXT;
                state->text.beg = it;
                for (; it != end && (*it >= '\x20' && *it != '\x7f'); it += 1) {
                }
                state->text.len = it - state->text.beg;
            }
            return it;
        case VT_PARSER_STATE_KIND_ESC: {
            c8 c = *it++;
            switch (c) {
            case '[':
                state->_state = VT_PARSER_STATE_KIND_CSI;
                state->csi.private_byte = 0;
                state->csi.final_byte = 0;
                while (state->csi.param_count > 0) {
                    state->csi.param_count -= 1;
                    state->csi.params[state->csi.param_count] = 0;
                }
                break;
            case ']':
                state->_state = VT_PARSER_STATE_KIND_OSC;
                state->osc.beg = it;
                state->osc.len = 0;
                break;
            case 'O':
                state->_state = VT_PARSER_STATE_KIND_SS3;
                break;
            case 'P':
                state->_state = VT_PARSER_STATE_KIND_DCS;
                state->dcs.beg = it;
                state->dcs.len = 0;
                break;
            default:
                state->_state = VT_PARSER_STATE_KIND_GROUND;
                state->kind = VT_TOKEN_KIND_ESC;
                state->esc = c;
                return it;
            }
            break;
        }
        case VT_PARSER_STATE_KIND_SS3:
            state->_state = VT_PARSER_STATE_KIND_GROUND;
            state->kind = VT_TOKEN_KIND_SS3;
            state->ss3 = *it++;
            return it;
        case VT_PARSER_STATE_KIND_CSI:
            for (;;) {
                // If we still have slots left, parse the parameter.
                if (state->csi.param_count < array_size(state->csi.params)) {
                    i32* dst = &state->csi.params[state->csi.param_count];
                    while (it != end && *it >= '0' && *it <= '9') {
                        i32 v = *dst * 10 + (*it - '0');
                        *dst = min(v, 0xffff);
                        it += 1;
                    }
                } else {
                    // ...otherwise, skip the parameters until we find the final byte.
                    while (it != end && *it >= '0' && *it <= '9') {
                        it += 1;
                    }
                }

                // Encountered the end of the input before finding the final byte.
                if (it == end) {
                    return it;
                }

                c8 c = *it++;
                if (c >= '\x40' && c <= '\x7e') {
                    state->_state = VT_PARSER_STATE_KIND_GROUND;
                    state->kind = VT_TOKEN_KIND_CSI;
                    state->csi.final_byte = c;
                    state->csi.param_count += 1;
                    return it;
                }
                if (c == ';') {
                    state->csi.param_count += 1;
                }
                if (c >= '<' && c <= '?') {
                    state->csi.private_byte = c;
                }
            }
        case VT_PARSER_STATE_KIND_OSC:
        case VT_PARSER_STATE_KIND_DCS:
            for (;;) {
                // Tight loop to find any indication for the end of the OSC/DCS sequence.
                for (; it != end && *it != '\a' && *it != '\x1b'; it += 1) {
                }

                state->kind = state->_state == VT_PARSER_STATE_KIND_OSC ? VT_TOKEN_KIND_OSC : VT_TOKEN_KIND_DCS;
                // .osc and .dsc share the same address.
                state->osc.len = it - state->osc.beg;

                // Encountered the end of the input before finding the terminator.
                if (it == end) {
                    return it;
                }

                c8 c = *it++;
                if (c == '\x1b') {
                    // It's only a string terminator if it's followed by \.
                    // We're at the end so we're saving the state and will continue next time.
                    if (it == end) {
                        state->_state = state->_state == VT_PARSER_STATE_KIND_OSC ? VT_PARSER_STATE_KIND_OSC_ESC : VT_PARSER_STATE_KIND_DCS_ESC;
                        return it;
                    }
                    // False alarm: Not a string terminator.
                    if (*it != '\\') {
                        continue;
                    }
                    it += 1;
                }

                state->_state = VT_PARSER_STATE_KIND_GROUND;
                return it;
            }
        case VT_PARSER_STATE_KIND_OSC_ESC:
        case VT_PARSER_STATE_KIND_DCS_ESC: {
            if (*it == '\\') {
                it += 1;
                state->kind = state->_state == VT_PARSER_STATE_KIND_OSC_ESC ? VT_TOKEN_KIND_OSC : VT_TOKEN_KIND_DCS;
                state->osc.len = it - state->osc.beg;
                return it;
            }
            state->_state = state->_state == VT_PARSER_STATE_KIND_OSC_ESC ? VT_PARSER_STATE_KIND_OSC : VT_PARSER_STATE_KIND_DCS;
            break;
        }
        default:
            unreachable();
        }
    }

    return it;
}
