#include "bootsel_line.h"

#include <assert.h>
#include <stddef.h>

static unsigned accepted(gridpico_bootsel_line *line, const char *text)
{
    unsigned count = 0;
    for (; *text != '\0'; ++text) {
        if (gridpico_bootsel_feed(line, (uint8_t)*text)) ++count;
    }
    return count;
}

int main(void)
{
    gridpico_bootsel_line line = {0};
    assert(accepted(&line, "BOOTSEL\n") == 0);
    assert(gridpico_bootsel_set_dtr(&line, true));
    assert(accepted(&line, "BOOT") == 0);
    assert(!gridpico_bootsel_set_dtr(&line, true));
    assert(accepted(&line, "SEL\n") == 1);
    assert(accepted(&line, "\nbootsel\nBOOTSEL\r\n BOOTSEL\nBOOTSEL x\nxBOOTSEL\n") == 0);
    for (unsigned i = 0; i < 256; ++i) assert(!gridpico_bootsel_feed(&line, 'x'));
    assert(accepted(&line, "BOOTSEL\nBOOTSEL\n") == 1);
    assert(accepted(&line, "BOOT") == 0);
    assert(gridpico_bootsel_set_dtr(&line, false));
    assert(accepted(&line, "BOOTSEL\n") == 0);
    assert(gridpico_bootsel_set_dtr(&line, true));
    assert(accepted(&line, "SEL\n") == 0);
    assert(accepted(&line, "BOOTSEL\n") == 1);
    return 0;
}
