#ifndef GRIDPICO_RP2040_ONBOARD_STATUS_H
#define GRIDPICO_RP2040_ONBOARD_STATUS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GRIDPICO_ONBOARD_STARTING,
    GRIDPICO_ONBOARD_READY_EMPTY,
    GRIDPICO_ONBOARD_READY_HUB,
    GRIDPICO_ONBOARD_READY_MIDI,
    GRIDPICO_ONBOARD_RECOVERY,
    GRIDPICO_ONBOARD_FAULT,
    GRIDPICO_ONBOARD_BOOTSEL,
} gridpico_onboard_status;

typedef struct {
    bool bootsel, fault, recovery, started, midi, hub;
} gridpico_onboard_facts;

static inline gridpico_onboard_status gridpico_onboard_classify(gridpico_onboard_facts facts)
{
    if (facts.bootsel) return GRIDPICO_ONBOARD_BOOTSEL;
    if (facts.fault) return GRIDPICO_ONBOARD_FAULT;
    if (facts.recovery) return GRIDPICO_ONBOARD_RECOVERY;
    if (!facts.started) return GRIDPICO_ONBOARD_STARTING;
    if (facts.midi) return GRIDPICO_ONBOARD_READY_MIDI;
    return facts.hub ? GRIDPICO_ONBOARD_READY_HUB : GRIDPICO_ONBOARD_READY_EMPTY;
}

static inline uint32_t gridpico_onboard_grb(gridpico_onboard_status status)
{
    static const uint32_t rgb[] = {
        [GRIDPICO_ONBOARD_STARTING]   = 0x101010,
        [GRIDPICO_ONBOARD_READY_EMPTY] = 0x000420,
        [GRIDPICO_ONBOARD_READY_HUB]  = 0x000420,
        [GRIDPICO_ONBOARD_READY_MIDI] = 0x002000,
        [GRIDPICO_ONBOARD_RECOVERY]   = 0x201000,
        [GRIDPICO_ONBOARD_FAULT]      = 0x280000,
        [GRIDPICO_ONBOARD_BOOTSEL]    = 0x200020,
    };
    const uint32_t color = rgb[status];
    return ((color & 0x00ff00u) << 16) | (color & 0xff0000u) | ((color & 0xffu) << 8);
}

typedef struct {
    bool offered;
    uint32_t word;
} gridpico_onboard_pixel;

typedef bool (*gridpico_onboard_offer_fn)(uint32_t word);

/* Call with the latest status. A refused offer leaves no work queued. */
static inline bool gridpico_onboard_present(gridpico_onboard_pixel *pixel, gridpico_onboard_status status,
                                           gridpico_onboard_offer_fn offer)
{
    const uint32_t word = gridpico_onboard_grb(status);
    if (pixel->offered && pixel->word == word) return true;
    if (!offer(word)) return false;
    *pixel = (gridpico_onboard_pixel){.offered = true, .word = word};
    return true;
}

void gridpico_onboard_init(void);
void gridpico_onboard_show(gridpico_onboard_status status);

#endif
