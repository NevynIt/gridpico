#include "hardware/regs/addressmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern const uint8_t __gridpico_pat2_slot_a[];
extern const uint8_t __gridpico_pat2_slot_b[];
extern const uint8_t __gridpico_pat2_slots_end[];

typedef struct {
    uint32_t flash_offset;
    uint32_t sector_bytes;
} pat2_slot_span;

/* Retained for offline inspection. The absolute symbols supply addresses only. */
bool gridpico_pico_pat2_slot_geometry(unsigned slot, pat2_slot_span *out)
{
    if (slot > 1 || out == NULL) return false;
    const uintptr_t begin = (uintptr_t)(slot == 0 ? __gridpico_pat2_slot_a : __gridpico_pat2_slot_b);
    const uintptr_t end = (uintptr_t)(slot == 0 ? __gridpico_pat2_slot_b : __gridpico_pat2_slots_end);
    *out = (pat2_slot_span){
        .flash_offset = (uint32_t)(begin - XIP_BASE),
        .sector_bytes = (uint32_t)(end - begin),
    };
    return true;
}
