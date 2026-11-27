#include "onboard_status.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "ws2812.pio.h"

enum { PIXEL_PIN = 16, PIXEL_BITS_PER_SEC = 800000, PIXEL_FRAME_AND_RESET_US = 400 };
#if !defined(WAVESHARE_RP2040_ZERO) || !defined(PICO_DEFAULT_WS2812_PIN)
#error "Onboard status requires the Waveshare RP2040-Zero WS2812 board capability"
#endif
_Static_assert(PIXEL_PIN == PICO_DEFAULT_WS2812_PIN, "The RP2040-Zero onboard pixel must be on GP16");

static int pixel_sm = -1;
static uint32_t pixel_clock;
static uint64_t next_offer_us;
static bool word_sent;
static uint32_t pixel_word;
static gridpico_onboard_cadence cadence;

void gridpico_onboard_init(void)
{
    const int sm = pio_claim_unused_sm(pio1, false);
    if (sm < 0) return;
    const int offset = pio_add_program(pio1, &ws2812_program);
    if (offset < 0) {
        pio_sm_unclaim(pio1, (uint)sm);
        return;
    }
    ws2812_program_init(pio1, (uint)sm, (uint)offset, PIXEL_PIN, PIXEL_BITS_PER_SEC, false);
    pixel_clock = clock_get_hz(clk_sys);
    pixel_sm = sm;
}

void gridpico_onboard_show(gridpico_onboard_status status)
{
    if (pixel_sm < 0) return;
    const uint sm = (uint)pixel_sm;
    const uint64_t now = time_us_64();
    const uint32_t word = gridpico_onboard_word(&cadence, status, now);
    const uint32_t hz = clock_get_hz(clk_sys);
    if (word_sent && pixel_word == word && hz == pixel_clock) return;
    /* FIFO empty alone does not account for the word currently shifting. */
    if (now < next_offer_us || !pio_sm_is_tx_fifo_empty(pio1, sm)) return;
    if (hz != pixel_clock) {
        const uint32_t bit_clock = PIXEL_BITS_PER_SEC * (ws2812_T1 + ws2812_T2 + ws2812_T3);
        pio_sm_set_clkdiv_int_frac8(pio1, sm, hz / bit_clock, (uint8_t)((hz % bit_clock) * 256u / bit_clock));
        pio_sm_clkdiv_restart(pio1, sm);
        pixel_clock = hz;
    }
    pio_sm_put(pio1, sm, word);
    next_offer_us = now + PIXEL_FRAME_AND_RESET_US;
    pixel_word = word;
    word_sent = true;
}
