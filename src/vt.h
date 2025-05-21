// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "helpers.h"

typedef enum VtParserStateKind {
    VT_PARSER_STATE_KIND_GROUND,
    VT_PARSER_STATE_KIND_ESC,
    VT_PARSER_STATE_KIND_SS3,
    VT_PARSER_STATE_KIND_CSI,
    VT_PARSER_STATE_KIND_OSC,
    VT_PARSER_STATE_KIND_DCS,
    VT_PARSER_STATE_KIND_OSC_ESC,
    VT_PARSER_STATE_KIND_DCS_ESC,
} VtParserStateKind;

typedef enum VtTokenType {
    VT_TOKEN_KIND_TEXT,
    VT_TOKEN_KIND_CTRL,
    VT_TOKEN_KIND_ESC,
    VT_TOKEN_KIND_SS3,
    VT_TOKEN_KIND_CSI,
    VT_TOKEN_KIND_OSC,
    VT_TOKEN_KIND_DCS,
    VT_TOKEN_KIND_PENDING,
} VtTokenKind;

typedef struct CsiState {
    i32 params[32];
    i32 param_count;
    c8 private_byte;
    c8 final_byte;
} CsiState;

typedef struct VtParserState {
    VtParserStateKind _state; // Stores the internal state of the tokenizer.
    VtTokenKind kind;         // Tells you which one of the union members you need to look at.

    s8 text;      // The plain text as a string.
    c8 ctrl;      // The single control character.
    c8 esc;       // A character that was prefixed by an ESC character.
    c8 ss3;       // The DCS contents as a string.
    CsiState csi; // The CSI parameters and final byte.
    s8 osc;       // The OSC contents as a string.
    s8 dcs;       // The DCS contents as a string.
} VtParserState;

c8* vt_parse_next_token(VtParserState* state, c8* const beg, c8* const end);
