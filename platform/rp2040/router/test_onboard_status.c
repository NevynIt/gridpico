#include "onboard_status.h"

#include <assert.h>
#include <stdio.h>

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

    gridpico_onboard_cadence cadence = {0};
    const uint64_t start = UINT64_C(6000000042);
    const uint32_t white = 0x10101000, blue = 0x04002000;
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start) == white);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start + 499999) == white);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start + 500000) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start + 999999) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start + 1000000) == white);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_STARTING, start + 7500000) == 0);

    const uint64_t empty = start + 8000000;
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty + 179999) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty + 180000) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty + 360000) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty + 1999999) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_EMPTY, empty + 2000000) == blue);

    const uint64_t hub = empty + 2250000;
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 179999) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 180000) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 359999) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 360000) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 539999) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 540000) == 0);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, hub + 2000000) == blue);

    const uint64_t green = hub + 2600000;
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_MIDI, green) == 0x20000000);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_MIDI, green + 5000000) == 0x20000000);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_READY_HUB, green + 5000001) == blue);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_RECOVERY, green + 5200000) == 0x10200000);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_FAULT, green + 5300000) == 0x00280000);
    assert(gridpico_onboard_word(&cadence, GRIDPICO_ONBOARD_BOOTSEL, green + 5400000) == 0x00202000);
    puts("PASS status precedence, exact GRB and monotonic cadence edges, entry reset and sparse sampling");
}
