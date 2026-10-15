/* Compiles cruth.h as C11 so the shell-facing header never picks up C++-isms.
 * SPDX-License-Identifier: MIT */
#include "cruth/cruth.h"

int cruth_c_header_check(void) {
    CruthBrush brush = cruth_brush_default(CRUTH_BRUSH_CLAY);
    return brush.type == CRUTH_BRUSH_CLAY ? 0 : 1;
}
