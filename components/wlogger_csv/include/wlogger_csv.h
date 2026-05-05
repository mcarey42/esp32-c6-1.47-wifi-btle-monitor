#pragma once
#include "wlogger_types.h"
#include <stddef.h>

// Formats one detection as a CSV row (with trailing \n).
// Returns number of bytes written (excluding NUL), or -1 on overflow.
int wlogger_csv_format(char *out, size_t out_size, const detection_t *d);
