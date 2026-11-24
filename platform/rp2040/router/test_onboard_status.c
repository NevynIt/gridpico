#include "onboard_status.h"

#include <assert.h>
#include <stdio.h>

static bool available;
static unsigned attempts, received;
static uint32_t last_word;

static bool try_pixel(uint32_t word)
{
    ++attempts;
    if (!available) return false;
    ++received;
    last_word = word;
    return true;
}

int main(void)
{
    gridpico_onboard_facts facts = {0};
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_STARTING);
    facts.started = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_READY_EMPTY);
    facts.hub = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_READY_HUB);
    facts.midi = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_READY_MIDI);
    facts.recovery = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_RECOVERY);
    facts.fault = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_FAULT);
    facts.bootsel = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_BOOTSEL);
    facts = (gridpico_onboard_facts){.midi = true, .hub = true};
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_STARTING);
    facts.recovery = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_RECOVERY);
    facts.fault = true;
    assert(gridpico_onboard_classify(facts) == GRIDPICO_ONBOARD_FAULT);

    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_STARTING) == 0x10101000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_READY_EMPTY) == 0x04002000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_READY_HUB) == 0x04002000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_READY_MIDI) == 0x20000000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_RECOVERY) == 0x10200000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_FAULT) == 0x00280000);
    assert(gridpico_onboard_grb(GRIDPICO_ONBOARD_BOOTSEL) == 0x00202000);

    gridpico_onboard_pixel pixel = {0};
    assert(!gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_STARTING, try_pixel));
    assert(!pixel.offered && received == 0);
    available = true;
    assert(gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_STARTING, try_pixel));
    assert(received == 1 && last_word == 0x10101000);
    assert(gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_STARTING, try_pixel));
    assert(attempts == 2 && received == 1);

    available = false;
    assert(!gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_RECOVERY, try_pixel));
    assert(pixel.word == 0x10101000);
    available = true;
    assert(gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_READY_HUB, try_pixel));
    assert(received == 2 && last_word == 0x04002000);
    assert(gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_READY_EMPTY, try_pixel));
    assert(attempts == 4 && received == 2);

    assert(gridpico_onboard_present(&pixel, GRIDPICO_ONBOARD_BOOTSEL, try_pixel));
    assert(received == 3 && last_word == 0x00202000);
    puts("PASS status precedence, exact logical palette/GRB, shared blue, no stale or duplicate offer");
}
