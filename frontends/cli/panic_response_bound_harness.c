#include "frontends/cli/cli_frontend.h"
#include "platform/rp2040/router/cdc_cli.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(GRIDPICO_PATCH_MAX_PORTS == 18 || GRIDPICO_PATCH_MAX_PORTS == 19 ||
                   GRIDPICO_PATCH_MAX_PORTS == 36,
               "this fixture targets Pico7 Matrix or portable profile extents");

typedef struct {
    gridpico_panic_stage_report report;
    gridpico_result read_result;
    gridpico_result apply_result;
    unsigned reads;
    unsigned applies;
} panic_fixture;

typedef struct {
    char bytes[2048];
    size_t size;
    unsigned writes;
} transcript;

static gridpico_result read_snapshot(void *context, gridpico_patch_snapshot *out)
{
    (void)context;
    memset(out, 0, sizeof *out);
    return GRIDPICO_OK;
}

static gridpico_result read_panic(void *context, gridpico_panic_stage_report *out)
{
    panic_fixture *fixture = context;
    ++fixture->reads;
    if (fixture->read_result == GRIDPICO_OK) *out = fixture->report;
    return fixture->read_result;
}

static gridpico_result apply_panic(void *context, gridpico_panic_stage_report *out)
{
    panic_fixture *fixture = context;
    ++fixture->applies;
    if (fixture->apply_result == GRIDPICO_OK) *out = fixture->report;
    return fixture->apply_result;
}

static void capture(void *context, const char *bytes, size_t size)
{
    transcript *out = context;
    ++out->writes;
    assert(size <= sizeof out->bytes - out->size);
    memcpy(out->bytes + out->size, bytes, size);
    out->size += size;
}

static void reset(transcript *out)
{
    memset(out, 0, sizeof *out);
}

static void assert_bytes(const transcript *actual, const char *expected, size_t size)
{
    if (actual->size != size || memcmp(actual->bytes, expected, size) != 0) {
        fprintf(stderr, "transcript mismatch: got %zu, expected %zu\n", actual->size, size);
        fwrite(actual->bytes, 1, actual->size, stderr);
        fputs("\n--- expected ---\n", stderr);
        fwrite(expected, 1, size, stderr);
        fputc('\n', stderr);
    }
    assert(actual->size == size);
    assert(memcmp(actual->bytes, expected, size) == 0);
}

static void feed_cdc_line(gridpico_cdc_cli *cdc, gridpico_cli_frontend *cli,
                          const char *line, const char *expected, size_t expected_size)
{
    for (const char *byte = line; *byte != '\0'; ++byte)
        gridpico_cdc_cli_feed(cdc, cli, (uint8_t)*byte, false);
    gridpico_cdc_cli_feed(cdc, cli, '\n', false);
    const char *reply = NULL;
    const size_t reply_size = gridpico_cdc_cli_offer(cdc, &reply);
    assert(reply_size == expected_size);
    assert(memcmp(reply, expected, expected_size) == 0);
    gridpico_cdc_cli_sent(cdc, reply_size);
}

int main(void)
{
    static gridpico_cli_frontend cli;
    static gridpico_panic_stage_report scratch;
    panic_fixture fixture = {
        .read_result = GRIDPICO_OK,
        .apply_result = GRIDPICO_OK,
        .report = {
            .state = GRIDPICO_PANIC_STAGE_COMPLETE,
            .count = 1,
            .outputs = {{
                .output = {.endpoint = 1, .generation = 7},
                .requested_count = 2,
                .accepted_count = 2,
                .state = GRIDPICO_PANIC_OUTPUT_COMPLETE,
            }},
        },
    };
    cli.session_context = &fixture;
    cli.read_snapshot = read_snapshot;
    cli.musical_control_context = &fixture;
    cli.read_panic = read_panic;
    cli.apply_panic = apply_panic;
    cli.panic_scratch = &scratch;
    transcript actual = {0};

    assert(gridpico_cli_execute_line(&cli, "panic", capture, &actual) == GRIDPICO_OK);
    assert(fixture.applies == 1 && fixture.reads == 0);
    const char one_row[] =
        "PANIC_STAGE CC123 COMPLETE 1\r\n"
        "PANIC_OUT 1 7 2 2 COMPLETE\r\n"
        "OK\r\n";
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && \
    defined(PICO_RP2040) && PICO_RP2040 && \
    defined(GRIDPICO_RP2040_MATRIX_CLI) && \
    defined(GRIDPICO_RP2040_PICO7_NORMAL) && \
    !defined(GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH)
    assert(actual.writes == 2);
    assert_bytes(&actual, "OK\r\n", sizeof "OK\r\n" - 1);
#elif defined(GRIDPICO_RP2040_MATRIX_CLI)
    assert(actual.writes == 1);
    assert_bytes(&actual, one_row, sizeof one_row - 1);
#else
    const char human_action[] =
        "All-notes-off stage: PANIC_STAGE CC123 COMPLETE 1\r\n"
        "All-notes-off output: PANIC_OUT 1 7 2 2 COMPLETE\r\n"
        "Done.\r\n";
    assert_bytes(&actual, human_action, sizeof human_action - 1);
    reset(&actual);
    assert(gridpico_cli_execute_line(&cli, "format machine", capture, &actual) ==
           GRIDPICO_OK);
    assert_bytes(&actual, "FORMAT MACHINE\r\nOK\r\n",
                 sizeof "FORMAT MACHINE\r\nOK\r\n" - 1);
#endif

    reset(&actual);
    assert(gridpico_cli_execute_line(&cli, "panic status", capture, &actual) == GRIDPICO_OK);
    assert(fixture.reads == 1 && fixture.applies == 1);
    assert(actual.writes == 1);
    assert_bytes(&actual, one_row, sizeof one_row - 1);

    reset(&actual);
    fixture.report.state = GRIDPICO_PANIC_STAGE_COMPLETE;
    fixture.report.count = GRIDPICO_PATCH_MAX_PORTS;
    for (size_t i = 0; i < fixture.report.count; ++i) {
        fixture.report.outputs[i] = (gridpico_panic_output){
            .output = {
                .endpoint = (gridpico_port_id)(UINT16_MAX -
                    GRIDPICO_PATCH_MAX_PORTS + i + 1),
                .generation = UINT32_MAX,
            },
            .requested_count = 16,
            .accepted_count = 0,
            .state = GRIDPICO_PANIC_OUTPUT_UNCONFIRMED,
        };
    }
    assert(gridpico_cli_execute_line(&cli, "panic status", capture, &actual) == GRIDPICO_OK);
    assert(fixture.reads == 2 && actual.writes == 1);
    char expected[2048];
    size_t expected_size = 0;
    int size = snprintf(expected, sizeof expected, "PANIC_STAGE CC123 COMPLETE %u\r\n",
                        (unsigned)GRIDPICO_PATCH_MAX_PORTS);
    assert(size > 0 && (size_t)size < sizeof expected);
    expected_size = (size_t)size;
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_PORTS; ++i) {
        size = snprintf(expected + expected_size, sizeof expected - expected_size,
                        "PANIC_OUT %u %u 16 - UNCONFIRMED\r\n",
                        (unsigned)(UINT16_MAX - GRIDPICO_PATCH_MAX_PORTS + i + 1),
                        (unsigned)UINT32_MAX);
        assert(size > 0 && (size_t)size < sizeof expected - expected_size);
        expected_size += (size_t)size;
    }
    memcpy(expected + expected_size, "OK\r\n", sizeof "OK\r\n" - 1);
    expected_size += sizeof "OK\r\n" - 1;
    assert(expected_size <= 45 * GRIDPICO_PATCH_MAX_PORTS + 36);
    assert(expected_size == 45 * GRIDPICO_PATCH_MAX_PORTS + 35);
    assert_bytes(&actual, expected, expected_size);

    reset(&actual);
    fixture.apply_result = GRIDPICO_UNAVAILABLE;
    assert(gridpico_cli_execute_line(&cli, "panic", capture, &actual) ==
           GRIDPICO_UNAVAILABLE);
    assert(fixture.applies == 2);
    assert(actual.writes == 2);
    assert_bytes(&actual, "ERR UNAVAILABLE\r\n", sizeof "ERR UNAVAILABLE\r\n" - 1);

    reset(&actual);
    fixture.read_result = GRIDPICO_UNAVAILABLE;
    assert(gridpico_cli_execute_line(&cli, "panic status", capture, &actual) ==
           GRIDPICO_UNAVAILABLE);
    assert(fixture.reads == 3);
    assert_bytes(&actual, "ERR UNAVAILABLE\r\n", sizeof "ERR UNAVAILABLE\r\n" - 1);

    reset(&actual);
    fixture.read_result = GRIDPICO_OK;
    fixture.report.count = GRIDPICO_PATCH_MAX_PORTS + 1;
    assert(gridpico_cli_execute_line(&cli, "panic status", capture, &actual) ==
           GRIDPICO_INVALID);
    assert(fixture.reads == 4);
    assert_bytes(&actual, "ERR INVALID\r\n", sizeof "ERR INVALID\r\n" - 1);

    gridpico_cdc_cli cdc = {0};
    fixture.apply_result = GRIDPICO_OK;
    fixture.report.count = 1;
    fixture.report.state = GRIDPICO_PANIC_STAGE_RUNNING;
    fixture.report.outputs[0] = (gridpico_panic_output){
        .output = {.endpoint = 1, .generation = 9},
        .requested_count = 16,
        .state = GRIDPICO_PANIC_OUTPUT_PENDING,
    };
    const char pending_status[] =
        "PANIC_STAGE CC123 RUNNING 1\r\n"
        "PANIC_OUT 1 9 16 0 PENDING\r\n"
        "OK\r\n";
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && \
    defined(PICO_RP2040) && PICO_RP2040 && \
    defined(GRIDPICO_RP2040_MATRIX_CLI) && \
    defined(GRIDPICO_RP2040_PICO7_NORMAL) && \
    !defined(GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH)
    feed_cdc_line(&cdc, &cli, "panic", "OK\r\n", sizeof "OK\r\n" - 1);
#else
    feed_cdc_line(&cdc, &cli, "panic", pending_status,
                  sizeof pending_status - 1);
#endif
    assert(fixture.applies == 3 && fixture.reads == 4);
    fixture.apply_result = GRIDPICO_UNAVAILABLE;
    feed_cdc_line(&cdc, &cli, "panic", "ERR UNAVAILABLE\r\n",
                  sizeof "ERR UNAVAILABLE\r\n" - 1);
    assert(fixture.applies == 4 && fixture.reads == 4);
    fixture.read_result = GRIDPICO_OK;
    feed_cdc_line(&cdc, &cli, "panic status", pending_status,
                  sizeof pending_status - 1);
    assert(fixture.reads == 5 && fixture.applies == 4);

    puts("Pico7 Panic 891-byte response bound PASS");
    return 0;
}
