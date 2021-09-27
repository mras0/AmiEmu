#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <ostream>

extern uint32_t debug_flags;
extern std::ostream* debug_stream;

constexpr uint32_t debug_flag_copper = 1 << 0;
constexpr uint32_t debug_flag_bpl    = 1 << 1;
constexpr uint32_t debug_flag_sprite = 1 << 2;
constexpr uint32_t debug_flag_disk   = 1 << 3;

#define DEBUG_COPPER (debug_flags & debug_flag_copper)
#define DEBUG_BPL    (debug_flags & debug_flag_bpl)
#define DEBUG_SPRITE (debug_flags & debug_flag_sprite)
#define DEBUG_DISK   (debug_flags & debug_flag_disk)
#define DEBUG_DMA    (DEBUG_COPPER|DEBUG_BPL|DEBUG_SPRITE|DEBUG_DISK)

#endif