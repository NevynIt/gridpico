#include "flash_startup.h"

#include <assert.h>

int main(void)
{
    volatile uint32_t scratch[] = {0, 0, 2, 3, 4, 5, 6, 7};
    assert(!gridpico_flash_previous_start_failed(false, scratch[0], scratch[1]));
    assert(!gridpico_flash_previous_start_failed(true, scratch[0], scratch[1]));

    gridpico_flash_start_mark(scratch);
    assert(scratch[0] == UINT32_C(0x47505354) && scratch[1] == UINT32_C(0xb8afacab));
    assert(!gridpico_flash_previous_start_failed(false, scratch[0], scratch[1]));
    assert(gridpico_flash_previous_start_failed(true, scratch[0], scratch[1]));
    assert(!gridpico_flash_previous_start_failed(true, scratch[0], 0));
    assert(!gridpico_flash_previous_start_failed(true, 0, scratch[1]));
    gridpico_flash_start_clear(scratch);
    assert(!gridpico_flash_previous_start_failed(true, scratch[0], scratch[1]));
    for (uint32_t i = 2; i < 8; ++i) assert(scratch[i] == i);

    const uint64_t deadline = 4000000;
    assert(gridpico_flash_start_poll(1000000, deadline, false, false, false) == GRIDPICO_FLASH_START_WAIT);
    assert(gridpico_flash_start_poll(1000000, deadline, false, true, true) == GRIDPICO_FLASH_START_WAIT);
    assert(gridpico_flash_start_poll(1000000, deadline, true, true, false) == GRIDPICO_FLASH_START_WAIT);
    assert(gridpico_flash_start_poll(1000000, deadline, true, false, false) == GRIDPICO_FLASH_START_READY);
    assert(gridpico_flash_start_poll(1000000, deadline, true, true, true) == GRIDPICO_FLASH_START_READY);
    assert(gridpico_flash_start_poll(deadline - 1, deadline, true, true, true) == GRIDPICO_FLASH_START_READY);
    assert(gridpico_flash_start_poll(deadline, deadline, true, true, true) == GRIDPICO_FLASH_START_EXPIRED);
    assert(gridpico_flash_start_poll(deadline + 1, deadline, false, false, false) == GRIDPICO_FLASH_START_EXPIRED);
}
