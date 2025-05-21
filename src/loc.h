// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "helpers.h"

typedef enum LocId
{
    LOC_Ctrl,
    LOC_Alt,
    LOC_Shift,

    // File menu
    LOC_File,
    LOC_File_Save,
    LOC_File_Save_As,
    LOC_File_Exit,

    // Edit menu
    LOC_Edit,
    LOC_Edit_Undo,
    LOC_Edit_Redo,
    LOC_Edit_Cut,
    LOC_Edit_Copy,
    LOC_Edit_Paste,
    LOC_Edit_Find,
    LOC_Edit_Replace,

    // Help menu
    LOC_Help,
    LOC_Help_About,

    // Exit dialog
    LOC_Exit_Dialog_Title,
    LOC_Exit_Dialog_Yes,
    LOC_Exit_Dialog_No,

    LOC_COUNT,
} LocId;

void loc_init();
s8 loc(LocId id);
