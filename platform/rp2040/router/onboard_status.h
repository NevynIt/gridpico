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
    bool sampled;
    gridpico_onboard_status status;
    uint64_t entered_us;
} gridpico_onboard_cadence;

/* Sample only the current phase. A refused write never restarts the cadence. */
static inline uint32_t gridpico_onboard_word(gridpico_onboard_cadence *cadence,
                                            gridpico_onboard_status status, uint64_t now_us)
{
    if (!cadence->sampled || cadence->status != status) {
        *cadence = (gridpico_onboard_cadence){.sampled = true, .status = status, .entered_us = now_us};
    }

    const uint64_t elapsed = now_us - cadence->entered_us;
    if (status == GRIDPICO_ONBOARD_STARTING) {
        if (elapsed % 1000000u >= 500000u) return 0;
    } else if (status == GRIDPICO_ONBOARD_READY_EMPTY || status == GRIDPICO_ONBOARD_READY_HUB) {
        const uint64_t phase = elapsed % 2000000u;
        if (phase >= 180000u && !(status == GRIDPICO_ONBOARD_READY_HUB && phase >= 360000u && phase < 540000u)) {
            return 0;
        }
    }
    return gridpico_onboard_grb(status);
}

void gridpico_onboard_init(void);
void gridpico_onboard_show(gridpico_onboard_status status);

#endif
