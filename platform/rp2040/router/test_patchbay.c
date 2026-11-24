#include "app/rp2040_router.h"

#include <assert.h>
#include <string.h>

static gridpico_pico_router router;

static void receive(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t number, uint8_t value)
{
    const gridpico_pico_packet packet = {{header, status, number, value}};
    assert(gridpico_pico_router_receive(&router, peer, &packet) == GRIDPICO_OK);
}

static void expect_note(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t key, uint8_t velocity)
{
    const gridpico_pico_packet expected = {{header, status, key, velocity}};
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, peer);
    assert(note != NULL && memcmp(note->bytes, expected.bytes, sizeof(expected.bytes)) == 0);
    gridpico_pico_router_note_sent(&router, peer);
}

static uint8_t drain_pages(gridpico_pico_peer peer)
{
    size_t count = 0;
    uint8_t last_palette = 0xff;
    const gridpico_pico_packet *packet;
    while ((packet = gridpico_pico_router_led(&router, peer)) != NULL) {
        const size_t frame = count++ % GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES;
        assert(count <= 2 * GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES && packet->bytes[0] == 0x19);
        if (frame == 0) {
            assert(packet->bytes[1] == 0x96 && packet->bytes[2] == 0x20);
            last_palette = packet->bytes[3];
        }
        gridpico_pico_router_led_sent(&router, peer);
        gridpico_pico_router_led_complete(&router, peer, true);
    }
    assert(count != 0 && count % GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES == 0);
    return last_palette;
}

int main(void)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, true);
    assert(drain_pages(GRIDPICO_PICO_APC) == 0x15);
    assert(drain_pages(GRIDPICO_PICO_WEB) == 0x15);

    receive(GRIDPICO_PICO_APC, 0x09, 0x90, 60, 96);
    expect_note(GRIDPICO_PICO_SYNTH, 0x09, 0x90, 60, 96);
    expect_note(GRIDPICO_PICO_WEB, 0x09, 0x90, 60, 96);
    receive(GRIDPICO_PICO_WEB, 0x19, 0x90, 0x20, 127);
    assert(!gridpico_pico_router_pad_status(&router).enabled);
    expect_note(GRIDPICO_PICO_SYNTH, 0x08, 0x80, 60, 0);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    assert(drain_pages(GRIDPICO_PICO_WEB) == 0x01);

    /* A press over the old physical page must release before it can try again. */
    const gridpico_pico_packet old_press = {{0x19, 0x90, 0x20, 127}};
    assert(gridpico_pico_router_receive(&router, GRIDPICO_PICO_APC, &old_press) == GRIDPICO_UNAVAILABLE);
    assert(!gridpico_pico_router_pad_status(&router).enabled);
    assert(drain_pages(GRIDPICO_PICO_APC) == 0x01);
    assert(gridpico_pico_router_receive(&router, GRIDPICO_PICO_APC, &old_press) == GRIDPICO_OK);
    assert(!gridpico_pico_router_pad_status(&router).enabled);
    receive(GRIDPICO_PICO_APC, 0x18, 0x80, 0x20, 0);
    receive(GRIDPICO_PICO_APC, 0x19, 0x90, 0x20, 127);
    assert(gridpico_pico_router_pad_status(&router).active);
    assert(drain_pages(GRIDPICO_PICO_APC) == 0x15);
    assert(drain_pages(GRIDPICO_PICO_WEB) == 0x15);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    receive(GRIDPICO_PICO_APC, 0x08, 0x80, 60, 32);
    expect_note(GRIDPICO_PICO_WEB, 0x08, 0x80, 60, 32);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);

    receive(GRIDPICO_PICO_WEB, 0x09, 0x90, 62, 96);
    expect_note(GRIDPICO_PICO_SYNTH, 0x09, 0x90, 62, 96);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    receive(GRIDPICO_PICO_WEB, 0x08, 0x80, 62, 17);
    expect_note(GRIDPICO_PICO_SYNTH, 0x08, 0x80, 62, 17);

    gridpico_apc_physical *physical = &router.panels[GRIDPICO_PICO_APC].frontend;
    gridpico_apc_physical *native = &router.panels[GRIDPICO_PICO_WEB].frontend;
    assert(physical->audition(physical->panel_context, 4, true) == GRIDPICO_OK);
    gridpico_pico_router_tick(&router, 1000);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 48, 96);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    assert(native->audition(native->panel_context, 4, false) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    assert(physical->audition(physical->panel_context, 4, false) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 48, 0);
    gridpico_pico_router_tick(&router, 300000);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);

    assert(native->audition(native->panel_context, 3, true) == GRIDPICO_OK);
    static const uint8_t melody[] = {48, 55, 60, 67, 72};
    for (size_t i = 0; i < sizeof(melody); ++i) {
        const uint64_t attack_at = 300000 + i * 200000;
        gridpico_pico_router_tick(&router, attack_at);
        expect_note(GRIDPICO_PICO_WEB, 9, 0x90, melody[i], 96);
        assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
        gridpico_pico_router_tick(&router, attack_at + 150000);
        expect_note(GRIDPICO_PICO_WEB, 8, 0x80, melody[i], 0);
    }
    gridpico_pico_router_tick(&router, 1500000);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
}
