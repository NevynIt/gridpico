#define _GNU_SOURCE

#include <alsa/asoundlib.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/pidfd.h>
#include <unistd.h>

enum {
    MAX_ALSA_POLL = 4,
    IO_BYTES = 128,
    LINE_BYTES = 11,
    START_CHECKS = 70,
    START_INTERVAL_MS = 100,
    CHECK_INTERVAL_MS = 250,
    ANNOUNCE_BATCH = 16
};

typedef struct {
    snd_seq_t *seq;
    pid_t engine;
    int pidfd;
    const char *port_name;
    snd_seq_addr_t source;
    snd_seq_addr_t target;
} recorder;

typedef enum {
    TARGET_ABSENT,
    TARGET_FOUND,
    TARGET_UNAVAILABLE
} target_status;

static bool alive(int pidfd)
{
    struct pollfd watched = { .fd = pidfd, .events = POLLIN };
    return poll(&watched, 1, 0) == 0;
}

static bool same_address(snd_seq_addr_t left, snd_seq_addr_t right)
{
    return left.client == right.client && left.port == right.port;
}

static bool eligible_port(const snd_seq_port_info_t *port, const char *name)
{
    const unsigned int caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
    return (snd_seq_port_info_get_capability(port) & caps) == caps &&
           (snd_seq_port_info_get_type(port) & SND_SEQ_PORT_TYPE_MIDI_GENERIC) &&
           strcmp(snd_seq_port_info_get_name(port), name) == 0;
}

static target_status find_target(recorder *capture)
{
    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    if (snd_seq_get_client_info(capture->seq, client) < 0 ||
        snd_seq_client_info_get_pid(client) != getpid() || !alive(capture->pidfd)) {
        return TARGET_UNAVAILABLE;
    }
    snd_seq_client_info_set_client(client, -1);
    bool found = false;
    int status;
    while ((status = snd_seq_query_next_client(capture->seq, client)) >= 0) {
        if (snd_seq_client_info_get_type(client) != SND_SEQ_USER_CLIENT ||
            snd_seq_client_info_get_pid(client) != capture->engine) {
            continue;
        }
        const int id = snd_seq_client_info_get_client(client);
        if (id < 0 || id >= SND_SEQ_ADDRESS_UNKNOWN) {
            return TARGET_UNAVAILABLE;
        }
        snd_seq_port_info_set_client(port, id);
        snd_seq_port_info_set_port(port, -1);
        while ((status = snd_seq_query_next_port(capture->seq, port)) >= 0) {
            if (!eligible_port(port, capture->port_name)) {
                continue;
            }
            if (found) {
                return TARGET_UNAVAILABLE;
            }
            const int number = snd_seq_port_info_get_port(port);
            if (number < 0 || number > UCHAR_MAX) {
                return TARGET_UNAVAILABLE;
            }
            capture->target = (snd_seq_addr_t){ (unsigned char)id, (unsigned char)number };
            found = true;
        }
        if (status != -ENOENT) {
            return TARGET_UNAVAILABLE;
        }
    }
    if (status != -ENOENT || !alive(capture->pidfd)) {
        return TARGET_UNAVAILABLE;
    }
    return found ? TARGET_FOUND : TARGET_ABSENT;
}

static bool await_target(recorder *capture)
{
    struct pollfd engine = { .fd = capture->pidfd, .events = POLLIN };
    for (int attempt = 0; attempt < START_CHECKS; ++attempt) {
        const target_status status = find_target(capture);
        if (status != TARGET_ABSENT) {
            return status == TARGET_FOUND;
        }
        if (poll(&engine, 1, START_INTERVAL_MS) != 0) {
            return false;
        }
    }
    return false;
}

static void subscription(const recorder *capture, snd_seq_port_subscribe_t *sub)
{
    snd_seq_port_subscribe_set_sender(sub, &capture->source);
    snd_seq_port_subscribe_set_dest(sub, &capture->target);
}

static bool live_subscription(const recorder *capture)
{
    snd_seq_port_subscribe_t *sub;
    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    subscription(capture, sub);
    return alive(capture->pidfd) &&
           snd_seq_get_any_client_info(capture->seq, capture->target.client, client) >= 0 &&
           snd_seq_client_info_get_type(client) == SND_SEQ_USER_CLIENT &&
           snd_seq_client_info_get_pid(client) == capture->engine &&
           snd_seq_get_any_port_info(capture->seq, capture->target.client,
                                      capture->target.port, port) >= 0 &&
           eligible_port(port, capture->port_name) &&
           snd_seq_get_port_subscription(capture->seq, sub) >= 0 &&
           snd_seq_port_subscribe_get_exclusive(sub) != 0 &&
           alive(capture->pidfd);
}

static bool attach(recorder *capture)
{
    if (!await_target(capture) || snd_seq_set_client_name(capture->seq, "Gridpico Recorder") < 0) {
        return false;
    }

    /* Claim exactly one writable sink. The source owner can subscribe even
     * without SUBS_READ, which keeps other clients from attaching to it. */
    const int port = snd_seq_create_simple_port(capture->seq, "Gridpico Recorder output",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0 || snd_seq_connect_from(capture->seq, port,
                                           SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE) < 0) {
        return false;
    }
    const int client = snd_seq_client_id(capture->seq);
    if (client < 0 || client >= SND_SEQ_ADDRESS_UNKNOWN || port > UCHAR_MAX) {
        return false;
    }
    capture->source = (snd_seq_addr_t){ (unsigned char)client, (unsigned char)port };
    snd_seq_port_subscribe_t *sub;
    snd_seq_port_subscribe_alloca(&sub);
    subscription(capture, sub);
    snd_seq_port_subscribe_set_exclusive(sub, 1);
    return snd_seq_subscribe_port(capture->seq, sub) >= 0 && live_subscription(capture);
}

static bool no_loss(const recorder *capture)
{
    for (size_t i = 0; i < ANNOUNCE_BATCH; ++i) {
        snd_seq_event_t *event;
        const int result = snd_seq_event_input(capture->seq, &event);
        if (result == -EAGAIN || result == -EINTR) {
            return true;
        }
        if (result < 0) {
            return false;
        }
        if (event->source.client != SND_SEQ_CLIENT_SYSTEM ||
            event->source.port != SND_SEQ_PORT_SYSTEM_ANNOUNCE) {
            continue;
        }
        if ((event->type == SND_SEQ_EVENT_CLIENT_EXIT &&
             event->data.addr.client == capture->target.client) ||
            (event->type == SND_SEQ_EVENT_PORT_EXIT &&
             same_address(event->data.addr, capture->target)) ||
            (event->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED &&
             same_address(event->data.connect.sender, capture->source) &&
             same_address(event->data.connect.dest, capture->target))) {
            return false;
        }
    }
    return true;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool note_command(const char *line, size_t size, snd_seq_event_t *event)
{
    if (size != LINE_BYTES || memcmp(line, "midi ", 5) != 0) {
        return false;
    }
    unsigned int bytes[3];
    for (size_t i = 0; i < 3; ++i) {
        const int high = hex_digit(line[5 + 2 * i]);
        const int low = hex_digit(line[6 + 2 * i]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[i] = 16u * (unsigned int)high + (unsigned int)low;
    }
    if (bytes[1] > 127 || bytes[2] > 127) {
        return false;
    }

    const unsigned int kind = bytes[0] >> 4;
    if (kind != 8 && kind != 9) {
        return false;
    }
    snd_seq_ev_clear(event);
    event->type = kind == 9 ? SND_SEQ_EVENT_NOTEON : SND_SEQ_EVENT_NOTEOFF;
    event->data.note.channel = (unsigned char)(bytes[0] & 15u);
    event->data.note.note = (unsigned char)bytes[1];
    event->data.note.velocity = (unsigned char)bytes[2];
    return true;
}

static bool reply(const char *text)
{
    return puts(text) != EOF && fflush(stdout) == 0;
}

static int failure(const char *text)
{
    reply(text);
    return EXIT_FAILURE;
}

static int serve(recorder *capture)
{
    struct pollfd descriptors[2 + MAX_ALSA_POLL] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = capture->pidfd, .events = POLLIN }
    };
    const int count = snd_seq_poll_descriptors_count(capture->seq, POLLIN);
    if (count <= 0 || count > MAX_ALSA_POLL ||
        snd_seq_poll_descriptors(capture->seq, descriptors + 2, (unsigned int)count, POLLIN) != count ||
        !reply("ready")) {
        return failure("error UNAVAILABLE");
    }

    char line[LINE_BYTES];
    size_t used = 0;
    for (;;) {
        const int ready = poll(descriptors, 2u + (unsigned int)count, CHECK_INTERVAL_MS);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0 || descriptors[1].revents || !no_loss(capture) ||
            !live_subscription(capture)) {
            return failure("error UNAVAILABLE");
        }
        if (!descriptors[0].revents) {
            continue;
        }
        char bytes[IO_BYTES];
        const ssize_t size = read(STDIN_FILENO, bytes, sizeof bytes);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size == 0 && used == 0) {
            return EXIT_SUCCESS;
        }
        if (size <= 0) {
            return failure("error INVALID");
        }
        for (ssize_t i = 0; i < size; ++i) {
            if (bytes[i] != '\n') {
                if (used == sizeof line) {
                    return failure("error INVALID");
                }
                line[used++] = bytes[i];
                continue;
            }
            snd_seq_event_t note;
            if (!note_command(line, used, &note)) {
                return failure("error INVALID");
            }
            used = 0;
            snd_seq_ev_set_source(&note, capture->source.port);
            snd_seq_ev_set_subs(&note);
            snd_seq_ev_set_direct(&note);
            if (!live_subscription(capture) ||
                snd_seq_event_output_direct(capture->seq, &note) <= 0 ||
                !no_loss(capture) || !live_subscription(capture) || !reply("ok")) {
                /* An ambiguous write aborts the take. The wrapper stops its synth. */
                return failure("error UNAVAILABLE");
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3 || !argv[1][0] || strspn(argv[1], "0123456789") != strlen(argv[1]) ||
        !argv[2][0] || strlen(argv[2]) >= 64) {
        return failure("error INVALID");
    }
    errno = 0;
    const long pid = strtol(argv[1], NULL, 10);
    if (errno || pid <= 1 || pid > INT_MAX) {
        return failure("error INVALID");
    }
    recorder capture = {
        .engine = (pid_t)pid, .pidfd = pidfd_open((pid_t)pid, 0), .port_name = argv[2]
    };
    if (capture.pidfd < 0 ||
        snd_seq_open(&capture.seq, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        if (capture.pidfd >= 0) {
            close(capture.pidfd);
        }
        return failure("error UNAVAILABLE");
    }
    const int result = attach(&capture) ? serve(&capture) : failure("error UNAVAILABLE");
    snd_seq_close(capture.seq);
    close(capture.pidfd);
    return result;
}
