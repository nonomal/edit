// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "tui.h"
#include "vt.h"

UiInput get_next_ui_input(VtParserState* vt_parser_state, s8* input);
