// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

struct UText;
typedef struct UText UText;

struct TextBuffer;
typedef struct TextBuffer TextBuffer;

void icu_init();
//UText* text_buffer_utext(TextBuffer* tb);
void text_buffer_utext_close(UText* ut);
