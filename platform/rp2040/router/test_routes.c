#include "app/rp2040_router.h"

#include <assert.h>
#include <string.h>

static gridpico_pico_router router;

static gridpico_result receive(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t key, uint8_t velocity)
{
    const gridpico_pico_packet packet = {{header, status, key, velocity}};
    return gridpico_pico_router_receive(&router, peer, &packet);
}

static void expect_note(gridpico_pico_peer peer, uint8_t header, uint8_t status, uint8_t key, uint8_t velocity)
{
    const gridpico_pico_packet expected = {{header, status, key, velocity}};
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, peer);
    assert(note != NULL && memcmp(note->bytes, expected.bytes, sizeof(expected.bytes)) == 0);
    gridpico_pico_router_note_sent(&router, peer);
}

static void expect_led(gridpico_pico_peer peer, uint8_t color)
{
    const gridpico_pico_packet expected = {{0x19, 0x96, 0x00, color}};
    const gridpico_pico_packet *led = gridpico_pico_router_led(&router, peer);
    assert(led != NULL && memcmp(led->bytes, expected.bytes, sizeof(expected.bytes)) == 0);
    gridpico_pico_router_led_sent(&router, peer);
}

static void connect(void)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    const gridpico_route_snapshot pending = gridpico_pico_router_pad_status(&router);
    assert(pending.enabled && !pending.active);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    expect_led(GRIDPICO_PICO_WEB, 0);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    expect_led(GRIDPICO_PICO_APC, 0);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, true);
    assert(gridpico_pico_router_pad_status(&router).active);
    expect_led(GRIDPICO_PICO_WEB, 0x15);
    expect_led(GRIDPICO_PICO_APC, 0x15);
}

static void test_inputs_and_releases(void)
{
    connect();
    assert(receive(GRIDPICO_PICO_APC, 9, 0x96, 60, 100) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_WEB, 9, 0x96, 60, 100);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x96, 60, 100);
    assert(receive(GRIDPICO_PICO_WEB, 9, 0x96, 60, 70) == GRIDPICO_OK);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB) == NULL);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);

    assert(receive(GRIDPICO_PICO_APC, 8, 0x86, 60, 23) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_WEB, 8, 0x86, 60, 23);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    assert(receive(GRIDPICO_PICO_WEB, 9, 0x96, 60, 0) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x86, 60, 0);
    assert(receive(GRIDPICO_PICO_APC, 9, 0x86, 61, 20) == GRIDPICO_INVALID);
}

static void test_shared_pad_independent_holds(void)
{
    connect();
    assert(receive(GRIDPICO_PICO_APC, 9, 0x90, 60, 100) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_WEB, 9, 0x90, 60, 100);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 60, 100);
    assert(receive(GRIDPICO_PICO_APC, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 60, 0);
    expect_led(GRIDPICO_PICO_WEB, 0);
    /* Leave the physical LED pending while Web receives the committed state. */
    assert(receive(GRIDPICO_PICO_APC, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    assert(gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB) == NULL);

    assert(receive(GRIDPICO_PICO_WEB, 9, 0x90, 61, 96) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 61, 96);
    assert(receive(GRIDPICO_PICO_APC, 9, 0x90, 62, 100) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_WEB, 9, 0x90, 62, 100);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);

    assert(receive(GRIDPICO_PICO_WEB, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    expect_led(GRIDPICO_PICO_APC, 0x15);
    expect_led(GRIDPICO_PICO_WEB, 0x15);
    assert(receive(GRIDPICO_PICO_APC, 0x18, 0x80, 0, 0) == GRIDPICO_OK);
    assert(receive(GRIDPICO_PICO_WEB, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    assert(gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB) == NULL);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, false);
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 61, 0);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    expect_led(GRIDPICO_PICO_WEB, 0x15);
    assert(receive(GRIDPICO_PICO_WEB, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    expect_led(GRIDPICO_PICO_WEB, 0);
}

static void test_destination_replacement(void)
{
    connect();
    assert(receive(GRIDPICO_PICO_APC, 9, 0x90, 60, 100) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 60, 100);
    expect_note(GRIDPICO_PICO_WEB, 9, 0x90, 60, 100);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, false);
    expect_led(GRIDPICO_PICO_WEB, 0);
    assert(receive(GRIDPICO_PICO_APC, 8, 0x80, 60, 11) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_WEB, 8, 0x80, 60, 11);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, true);
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 60, 0);
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    expect_led(GRIDPICO_PICO_APC, 0x15);
    expect_led(GRIDPICO_PICO_WEB, 0x15);
}

static void test_stalled_export(void)
{
    connect();
    bool rejected = false;
    for (unsigned i = 0; i < GRIDPICO_PICO_QUEUE_SIZE; ++i) {
        const gridpico_result on = receive(GRIDPICO_PICO_APC, 9, 0x90, 60, 100);
        if (on == GRIDPICO_UNAVAILABLE) rejected = true;
        else assert(on == GRIDPICO_OK);
        expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 60, 100);
        assert(receive(GRIDPICO_PICO_APC, 8, 0x80, 60, 23) == GRIDPICO_OK);
        expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 60, 23);
    }
    assert(rejected);
}

#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
static void test_program_change_admission(void)
{
    static const gridpico_local_route native_to_synth = {2, 4, GRIDPICO_LOCAL_MANAGED};
    static const gridpico_pico_packet minimum = {{0x0c, 0xc3, 0x00, 0}};
    static const gridpico_pico_packet maximum = {{0x0c, 0xc3, 0x7f, 0}};
    static const gridpico_pico_packet invalid[] = {
        {{0x0c, 0xc3, 0x80, 0}}, {{0x0c, 0xc3, 0x7f, 1}}, {{0x0d, 0xc3, 0, 0}},
        {{0x0c, 0xc4, 0, 0}}, {{0x1c, 0xc3, 0, 0}}, {{0x0c, 0xb3, 0, 0}},
    };
    connect();
    const gridpico_revision revision = gridpico_pico_router_pad_status(&router).applied_revision;
    assert(gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    assert(gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &maximum));
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_APC, &minimum));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &invalid[i]));
    }
    assert(gridpico_pico_router_pad_status(&router).applied_revision == revision);
    assert(gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB) == NULL);
    assert(gridpico_pico_router_led(&router, GRIDPICO_PICO_APC) == NULL);

    /* Only the physical APC is sounding. It still makes the synth busy. */
    assert(receive(GRIDPICO_PICO_APC, 9, 0x90, 61, 96) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x90, 61, 96);
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    assert(receive(GRIDPICO_PICO_APC, 8, 0x80, 61, 32) == GRIDPICO_OK);
    assert(gridpico_patch_graph_destination_idle(&router.graph, native_to_synth.destination));
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x80, 61, 32);
    /* The native export still holds both frames. It cannot block the synth. */
    assert(gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    expect_note(GRIDPICO_PICO_WEB, 9, 0x90, 61, 96);
    expect_note(GRIDPICO_PICO_WEB, 8, 0x80, 61, 32);

    assert(receive(GRIDPICO_PICO_APC, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    assert(receive(GRIDPICO_PICO_APC, 9, 0x90, 62, 96) == GRIDPICO_OK);
    assert(receive(GRIDPICO_PICO_APC, 0x18, 0x80, 0, 0) == GRIDPICO_OK);
    assert(receive(GRIDPICO_PICO_APC, 0x19, 0x90, 0, 127) == GRIDPICO_OK);
    assert(gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    assert(gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) == NULL);
    expect_note(GRIDPICO_PICO_WEB, 9, 0x90, 62, 96);

    assert(receive(GRIDPICO_PICO_WEB, 9, 0x93, 63, 96) == GRIDPICO_OK);
    expect_note(GRIDPICO_PICO_SYNTH, 9, 0x93, 63, 96);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, false);
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    assert(receive(GRIDPICO_PICO_WEB, 8, 0x83, 63, 32) == GRIDPICO_OK);
    assert(!gridpico_patch_graph_destination_idle(&router.graph, native_to_synth.destination));
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_SYNTH, true);
    assert(gridpico_patch_graph_destination_idle(&router.graph, native_to_synth.destination));
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
    expect_note(GRIDPICO_PICO_SYNTH, 8, 0x83, 63, 0);
    assert(gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));

    const gridpico_local_route_request disable = {.route = native_to_synth, .enabled = false};
    assert(gridpico_patch_session_set_local_route(&router.session, &disable) == GRIDPICO_OK);
    assert(!gridpico_pico_router_can_program_change(&router, GRIDPICO_PICO_WEB, &minimum));
}
#endif

int main(void)
{
    test_inputs_and_releases();
    test_shared_pad_independent_holds();
    test_destination_replacement();
    test_stalled_export();
#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
    test_program_change_admission();
#endif
}
