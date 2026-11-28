/* Companion assertions for the SDK copy-to-RAM map and PAT2 reservation. */
ASSERT(__gridpico_usb_drive_start >= ORIGIN(FLASH) &&
       __gridpico_usb_drive_end <= ORIGIN(FLASH) + LENGTH(FLASH),
       "USB drive image must reside in XIP flash")
ASSERT((__gridpico_usb_drive_start & 0x1ff) == 0 &&
       __gridpico_usb_drive_end - __gridpico_usb_drive_start == 0x60000,
       "USB drive image must be a sector-aligned 768-sector volume")
ASSERT(__gridpico_usb_drive_end <= __flash_binary_end &&
       __flash_binary_end <= __gridpico_pat2_slot_a,
       "USB drive image exceeds the reserved flash boundary")
