// SPDX-License-Identifier: GPL-3.0-or-later
// Stable native metadata only; usable by freestanding generated modules.
#pragma once
#include <stdint.h>
struct NativeCodeRange {uint32_t begin,end;};
struct NativeOverlayInfo {
    uint32_t core,id,base,bytes;
    uint8_t sha256[32];
    const NativeCodeRange *code;
    uint32_t code_count;
    uint32_t flags; // Bit 0: every instruction of every static code variant is guarded.
};
static_assert(sizeof(NativeCodeRange)==8&&sizeof(NativeOverlayInfo)==64,"Native overlay metadata layout");
