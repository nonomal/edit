// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "helpers.h"

typedef struct UcdMeasurement {
    usize offset;
    Point pos;
    CoordType movements;
    bool newline;
} UcdMeasurement;

UcdMeasurement ucd_measure_forward(s8 str, usize offset, Point pos, CoordType column_stop, CoordType cursor_movement_limit, UcdMeasurement* line_break);
// ucd_measure_backward returns a negative column if it has crossed a newline as it cannot
// possibly know what column its now in without iterating all the way to the start of the line.
// Doing so is the job of the caller as that depends on the way the text is stored
// (a rope for instance will have potentially many segments to iterate through).
UcdMeasurement ucd_measure_backward(s8 str, usize offset, Point pos, CoordType column_stop, CoordType cursor_movement_limit);

usize ucd_newlines_forward(s8 str, usize offset, CoordType* line, CoordType line_stop);
usize ucd_newlines_backward(s8 str, usize offset, CoordType* line, CoordType line_stop);
