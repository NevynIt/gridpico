#include "app/rp2040_router.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static gridpico_pico_router router;

static gridpico_result send(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t data, uint8_t value)
{
    const gridpico_pico_packet packet = {{header, status, data, value}};
    return gridpico_pico_router_receive(&router, peer, &packet);
}

static void expect(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t data, uint8_t value)
{
    const uint8_t bytes[] = {header, status, data, value};
    const gridpico_pico_packet *packet = gridpico_pico_router_note(&router, peer);
    if (packet == NULL || memcmp(packet->bytes, bytes, sizeof(bytes)) != 0) {
        fprintf(stderr, "peer %u expected %02x %02x %02x %02x", peer, header, status, data, value);
        if (packet != NULL) fprintf(stderr, ", got %02x %02x %02x %02x", packet->bytes[0], packet->bytes[1],
                                    packet->bytes[2], packet->bytes[3]);
        fputc('\n', stderr);
    }
    assert(packet != NULL && memcmp(packet->bytes, bytes, sizeof(bytes)) == 0);
    gridpico_pico_router_note_sent(&router, peer);
}

static void shift(bool down)
{
    assert(send(GRIDPICO_PICO_APC, down ? 0x19 : 0x18, down ? 0x90 : 0x80, 0x62,
                down ? 127 : 0) == GRIDPICO_OK);
}

static void sustain(bool down)
{
    assert(send(GRIDPICO_PICO_APC, 0x0b, 0xb0, 64, down ? 127 : 0) == GRIDPICO_OK);
}

static void select_mode(uint8_t pad)
{
    assert(send(GRIDPICO_PICO_APC, 0x19, 0x90, pad, 127) == GRIDPICO_OK);
    assert(send(GRIDPICO_PICO_APC, 0x18, 0x80, pad, 0) == GRIDPICO_OK);
}

static void drain_apc(void)
{
    unsigned count = 0;
    while (gridpico_pico_router_note(&router, GRIDPICO_PICO_APC) != NULL) {
        assert(++count <= GRIDPICO_PICO_QUEUE_SIZE);
        gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_APC);
    }
}

int main(void)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, true);
    gridpico_apc_patch_snapshot snap;
    assert(gridpico_patch_session_read_apc_patch(&router.session, &snap) == GRIDPICO_OK);
    assert(snap.layout.rows[2] == 0 && snap.layout.columns[2] == 0);
    assert(gridpico_patch_session_assign_apc_patch(&router.session, snap.layout_revision,
                                                  GRIDPICO_PATCH_INPUT, 2, 6) == GRIDPICO_OK);
    assert(gridpico_patch_session_read_apc_patch(&router.session, &snap) == GRIDPICO_OK);
    assert(gridpico_patch_session_assign_apc_patch(&router.session, snap.layout_revision,
                                                  GRIDPICO_PATCH_OUTPUT, 2, 7) == GRIDPICO_OK);
    const gridpico_local_route_request b_in = {{6, 4, GRIDPICO_LOCAL_MANAGED}, true};
    const gridpico_local_route_request b_out = {{1, 7, GRIDPICO_LOCAL_MANAGED}, true};
    const gridpico_local_route_request echo = {{6, 3, GRIDPICO_LOCAL_MANAGED}, true};
    assert(gridpico_patch_session_set_local_route(&router.session, &b_in) == GRIDPICO_OK);
    assert(gridpico_patch_session_set_local_route(&router.session, &b_out) == GRIDPICO_OK);
    assert(gridpico_patch_session_set_local_route(&router.session, &echo) == GRIDPICO_INVALID);
    assert(send(GRIDPICO_PICO_WEB, 0x29, 0x93, 65, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_SYNTH, 9, 0x93, 65, 96);
    assert(send(GRIDPICO_PICO_WEB, 0x28, 0x83, 65, 43) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_SYNTH, 8, 0x83, 65, 43);
    assert(send(GRIDPICO_PICO_APC, 9, 0x90, 61, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_SYNTH, 9, 0x90, 61, 96);
    expect(GRIDPICO_PICO_WEB, 9, 0x90, 61, 96);
    expect(GRIDPICO_PICO_WEB, 0x29, 0x90, 61, 96);

    shift(true);
    sustain(true);
    assert(router.keys.chooser.open);
    expect(GRIDPICO_PICO_APC, 0x19, 0x96, 0x20, 0x15);
    expect(GRIDPICO_PICO_APC, 0x19, 0x96, 0x21, 0x01);
    select_mode(0x21);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
    gridpico_pico_router_finish_mode(&router, true, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
    expect(GRIDPICO_PICO_SYNTH, 8, 0x80, 61, 0);
    expect(GRIDPICO_PICO_WEB, 8, 0x80, 61, 0);
    expect(GRIDPICO_PICO_WEB, 0x28, 0x80, 61, 0);
    gridpico_pico_router_finish_mode(&router, false, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
    gridpico_pico_router_finish_mode(&router, true, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_KEYS);
    sustain(false);
    shift(false);
    assert(send(GRIDPICO_PICO_APC, 9, 0x90, 61, 96) == GRIDPICO_OK);
    assert(send(GRIDPICO_PICO_APC, 8, 0x80, 61, 41) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);

    assert(send(GRIDPICO_PICO_APC, 0x0c, 0xc3, 7, 0) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x0c, 0xc3, 7, 0);
    assert(send(GRIDPICO_PICO_APC, 0x0d, 0xd3, 17, 0) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x0d, 0xd3, 17, 0);
    assert(send(GRIDPICO_PICO_APC, 0x0c, 0xc3, 7, 1) == GRIDPICO_INVALID);
    assert(send(GRIDPICO_PICO_APC, 0x0e, 0xe7, 0, 64) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x0e, 0xe7, 0, 64);
    assert(send(GRIDPICO_PICO_WEB, 0x19, 0x96, 0x20, 17) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_APC, 0x19, 0x96, 0x20, 17);
    assert(send(GRIDPICO_PICO_WEB, 0x1d, 0xd2, 17, 0) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_APC, 0x1d, 0xd2, 17, 0);
    assert(send(GRIDPICO_PICO_WEB, 0x09, 0x90, 65, 96) == GRIDPICO_OK);
    assert(send(GRIDPICO_PICO_WEB, 0x29, 0x90, 65, 96) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);

    sustain(true); /* Reversed order is ordinary and cannot open the chooser. */
    expect(GRIDPICO_PICO_WEB, 0x0b, 0xb0, 64, 127);
    shift(true);
    expect(GRIDPICO_PICO_WEB, 0x19, 0x90, 0x62, 127);
    assert(!router.keys.chooser.open);
    sustain(false);
    expect(GRIDPICO_PICO_WEB, 0x0b, 0xb0, 64, 0);
    sustain(true);
    assert(router.keys.chooser.open);
    assert(send(GRIDPICO_PICO_APC, 0x0b, 0xb1, 64, 100) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x0b, 0xb1, 64, 100);
    assert(send(GRIDPICO_PICO_APC, 9, 0x92, 67, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 9, 0x92, 67, 96);
    select_mode(0x20);
    expect(GRIDPICO_PICO_WEB, 0x0b, 0xb1, 64, 0);
    expect(GRIDPICO_PICO_WEB, 0x18, 0x80, 0x62, 0);
    expect(GRIDPICO_PICO_WEB, 8, 0x82, 67, 0);
    drain_apc();
    gridpico_pico_router_finish_mode(&router, true, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
    sustain(false);
    shift(false);
    assert(send(GRIDPICO_PICO_APC, 8, 0x82, 67, 42) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    assert(send(GRIDPICO_PICO_APC, 0x0b, 0xb1, 64, 0) == GRIDPICO_OK);
    assert(send(GRIDPICO_PICO_WEB, 0x29, 0x92, 69, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_SYNTH, 9, 0x92, 69, 96);
    assert(send(GRIDPICO_PICO_WEB, 0x28, 0x82, 69, 0) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_SYNTH, 8, 0x82, 69, 0);

    shift(true);
    sustain(true);
    select_mode(0x21);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, false);
    assert(send(GRIDPICO_PICO_APC, 0x19, 0x90, 0x21, 127) == GRIDPICO_UNAVAILABLE);
    gridpico_pico_router_finish_mode(&router, true, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
    assert(router.keys.next_mode == GRIDPICO_APC_CHOOSER_NO_REQUEST);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    shift(true);
    sustain(true);
    select_mode(0x21);
    drain_apc();
    gridpico_pico_router_finish_mode(&router, true, true, true);
    assert(router.keys.mode == GRIDPICO_APC_CHOOSER_KEYS);
    shift(false);
    sustain(false);
    assert(send(GRIDPICO_PICO_APC, 0x09, 0x95, 70, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x09, 0x95, 70, 96);
    assert(send(GRIDPICO_PICO_APC, 0x0a, 0xa5, 70, 71) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x0a, 0xa5, 70, 71);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, false);
    expect(GRIDPICO_PICO_WEB, 0x08, 0x85, 70, 0);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    assert(send(GRIDPICO_PICO_APC, 0x08, 0x85, 70, 11) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    assert(send(GRIDPICO_PICO_APC, 0x09, 0x95, 70, 96) == GRIDPICO_OK);
    expect(GRIDPICO_PICO_WEB, 0x09, 0x95, 70, 96);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, false);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    assert(send(GRIDPICO_PICO_APC, 0x08, 0x85, 70, 11) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    puts("PASS A/B assignment and no echo, complete channel voice, Panel, cutover releases, detach and fresh edges");
}
