#include "app/runner_io.h"

#include <stdio.h>
#include <string.h>

request_status read_request(char line[REQUEST_SIZE])
{
    size_t length = 0;
    bool malformed = false;
    int byte;

    while ((byte = getchar()) != EOF && byte != '\n') {
        if (length == REQUEST_SIZE - 1 || byte < 0x20 || byte > 0x7e) {
            malformed = true;
        } else if (!malformed) {
            line[length++] = (char)byte;
        }
    }
    line[length] = '\0';

    if (byte == EOF) {
        return REQUEST_EOF;
    }
    return malformed ? REQUEST_MALFORMED : REQUEST_LINE;
}

static bool parse_byte(const char **cursor, unsigned maximum, uint8_t *value)
{
    const char *start = *cursor;
    unsigned parsed = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        parsed = parsed * 10u + (unsigned)(**cursor - '0');
        if (parsed > maximum) {
            return false;
        }
        ++*cursor;
    }
    if (*cursor == start || *cursor - start > 3) {
        return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

bool parse_note(const char *line, gridpico_note *note)
{
    if (strncmp(line, "on 1 ", 5) == 0) {
        note->kind = GRIDPICO_NOTE_ON;
        line += 5;
    } else if (strncmp(line, "off 1 ", 6) == 0) {
        note->kind = GRIDPICO_NOTE_OFF;
        line += 6;
    } else {
        return false;
    }

    note->channel_index = 0;
    return parse_byte(&line, 127, &note->key) && *line++ == ' ' &&
           parse_byte(&line, 127, &note->velocity) && *line == '\0' &&
           (note->kind == GRIDPICO_NOTE_OFF || note->velocity != 0);
}

bool parse_surface(const char *line, gridpico_apc_surface_message *message)
{
    return parse_byte(&line, 239, &message->bytes[0]) && message->bytes[0] >= 128 &&
           *line++ == ' ' && parse_byte(&line, 127, &message->bytes[1]) &&
           *line++ == ' ' && parse_byte(&line, 127, &message->bytes[2]) && *line == '\0';
}

bool write_note(gridpico_note note)
{
    return printf("%s %u %u %u\n", note.kind == GRIDPICO_NOTE_ON ? "on" : "off",
                  (unsigned)note.channel_index + 1u,
                  (unsigned)note.key, (unsigned)note.velocity) >= 0;
}

bool write_surface(gridpico_apc_surface_message message)
{
    return printf("surface %u %u %u\n", (unsigned)message.bytes[0],
                  (unsigned)message.bytes[1], (unsigned)message.bytes[2]) >= 0;
}
