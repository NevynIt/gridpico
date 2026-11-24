#ifndef GRIDPICO_RP2040_BOOTSEL_LINE_H
#define GRIDPICO_RP2040_BOOTSEL_LINE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t matched;
    bool rejected;
    bool dtr;
} gridpico_bootsel_line;

bool gridpico_bootsel_set_dtr(gridpico_bootsel_line *line, bool asserted);
bool gridpico_bootsel_feed(gridpico_bootsel_line *line, uint8_t byte);

#endif
