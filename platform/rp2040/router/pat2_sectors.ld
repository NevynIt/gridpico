/* Companion to the stock copy-to-RAM map. Absolute symbols reserve no bytes. */
__gridpico_pat2_slot_a = 0x101f4000;
__gridpico_pat2_slot_b = 0x101f5000;
__gridpico_pat2_slots_end = 0x101f6000;

ASSERT(ORIGIN(FLASH) == 0x10000000 && LENGTH(FLASH) == 0x200000,
       "PAT2 requires the RP2040 2 MiB XIP flash region")
ASSERT(((__gridpico_pat2_slot_a | __gridpico_pat2_slot_b | __gridpico_pat2_slots_end) & 0xfff) == 0,
       "PAT2 sector boundaries must be 4 KiB aligned")
ASSERT(__gridpico_pat2_slot_b - __gridpico_pat2_slot_a == 0x1000 &&
       __gridpico_pat2_slots_end - __gridpico_pat2_slot_b == 0x1000,
       "PAT2 slots must be adjacent 4 KiB sectors")
ASSERT(__gridpico_pat2_slot_a >= ORIGIN(FLASH) &&
       __gridpico_pat2_slots_end <= ORIGIN(FLASH) + LENGTH(FLASH),
       "PAT2 slots must lie inside the flash region")
ASSERT(__flash_binary_end <= __gridpico_pat2_slot_a,
       "The flash image overlaps the PAT2 reservation")
