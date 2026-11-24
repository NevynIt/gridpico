#ifndef GRIDPICO_RP2040_FLASH_STARTUP_H
#define GRIDPICO_RP2040_FLASH_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

#define GRIDPICO_FLASH_START_TAG UINT32_C(0x47505354)
#define GRIDPICO_FLASH_START_COMPLEMENT UINT32_C(0xb8afacab)

static inline bool gridpico_flash_previous_start_failed(bool watchdog_reset, uint32_t tag, uint32_t complement)
{
    return watchdog_reset && tag == GRIDPICO_FLASH_START_TAG && complement == GRIDPICO_FLASH_START_COMPLEMENT;
}

static inline void gridpico_flash_start_mark(volatile uint32_t scratch[2])
{
    scratch[1] = GRIDPICO_FLASH_START_COMPLEMENT;
    scratch[0] = GRIDPICO_FLASH_START_TAG;
}

static inline void gridpico_flash_start_clear(volatile uint32_t scratch[2])
{
    scratch[0] = 0;
    scratch[1] = 0;
}

typedef enum {
    GRIDPICO_FLASH_START_WAIT,
    GRIDPICO_FLASH_START_READY,
    GRIDPICO_FLASH_START_EXPIRED,
} gridpico_flash_start_state;

static inline gridpico_flash_start_state gridpico_flash_start_poll(
    uint64_t now, uint64_t deadline, bool host_running, bool native_activity, bool cdc_ready)
{
    if (now >= deadline) return GRIDPICO_FLASH_START_EXPIRED;
    if (host_running && (!native_activity || cdc_ready)) return GRIDPICO_FLASH_START_READY;
    return GRIDPICO_FLASH_START_WAIT;
}

bool gridpico_flash_init_usb_serial(void);

#endif
