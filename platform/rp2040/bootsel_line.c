#include "bootsel_line.h"

bool gridpico_bootsel_set_dtr(gridpico_bootsel_line *line, bool asserted)
{
    if (line->dtr == asserted) return false;
    *line = (gridpico_bootsel_line){.dtr = asserted};
    return true;
}

bool gridpico_bootsel_feed(gridpico_bootsel_line *line, uint8_t byte)
{
    static const uint8_t command[] = "BOOTSEL";
    if (!line->dtr) return false;
    if (byte == '\n') {
        const bool accepted = !line->rejected && line->matched == sizeof(command) - 1;
        *line = (gridpico_bootsel_line){.dtr = true};
        return accepted;
    }
    if (line->matched < sizeof(command) - 1 && command[line->matched] == byte) {
        ++line->matched;
    } else {
        line->rejected = true;
    }
    return false;
}
