#ifndef FIXTURE_HARDWARE_FLASH_H
#define FIXTURE_HARDWARE_FLASH_H
#include <stdint.h>
#define FLASH_UNIQUE_ID_SIZE_BYTES 8
void flash_get_unique_id(uint8_t *id);
#endif
