/* A Network MIDI 2.0 endpoint on a UDP socket, for trying the whole path on
 * a host: socket, sessions (src/netmidi2.c) and the translation between
 * UMP and MIDI 1.0 bytes (src/ump.c). POSIX only; nothing of AROS.
 *
 *   netmidi2-peer PORT [HOST_ADDRESS HOST_PORT] [--send|--jr]
 *
 * It listens on PORT as a Host. With an address it also invites that Host
 * as a Client. With --send it sends, once to each session that becomes
 * established, a Note On, a Note Off, a Control Change and a SysEx message
 * given as MIDI 1.0 bytes. Everything received is printed as the MIDI 1.0
 * bytes it translates to, one message a line. SIGTERM ends it with Bye. */
#define _DEFAULT_SOURCE 1

#include "netmidi2/netmidi2.h"
#include "ump/ump.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static int fd = -1;
static struct netmidi2_table table;
static struct ump_to_midi1 to_midi[NETMIDI2_SESSIONS_MAX];
static uint8_t sysex[NETMIDI2_SESSIONS_MAX][512];
static int announced[NETMIDI2_SESSIONS_MAX];
static int sent[NETMIDI2_SESSIONS_MAX];
static uint32_t established_at[NETMIDI2_SESSIONS_MAX];

static void stop(int number)
{
    (void)number;
    stopping = 1;
}

static uint32_t now_ms(void)
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (uint32_t)((uint64_t)time.tv_sec * 1000 + time.tv_usec / 1000);
}

static uint64_t key_of(const struct sockaddr_in *address)
{
    return ((uint64_t)ntohl(address->sin_addr.s_addr) << 16) |
           ntohs(address->sin_port);
}

static void address_of(uint64_t key, struct sockaddr_in *address)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl((uint32_t)(key >> 16));
    address->sin_port = htons((uint16_t)key);
}

static void send_to(uint64_t key, const struct netmidi2_writer *packet)
{
    struct sockaddr_in address;
    if (packet->length <= 4)
        return;
    address_of(key, &address);
    sendto(fd, packet->data, packet->length, 0,
           (const struct sockaddr *)&address, sizeof(address));
}

static void print_midi(void *context, const uint8_t *bytes, size_t length)
{
    size_t i;
    printf("midi from=%d ", *(int *)context);
    for (i = 0; i < length; ++i)
        printf("%02x", (unsigned int)bytes[i]);
    putchar('\n');
}

static void take_umps(void *context, int index, const uint32_t *words,
                      size_t count)
{
    size_t i = 0;
    (void)context;
    while (i < count) {
        unsigned n = ump_words(words[i]);
        if (i + n > count)
            break;
        ump_to_midi1(&to_midi[index], words + i, n, print_midi, &index);
        i += n;
    }
}

struct outgoing {
    uint32_t words[NETMIDI2_MAX_UMP_WORDS];
    unsigned count;
};

static void collect(void *context, const uint32_t *words, unsigned count)
{
    struct outgoing *out = context;
    if (out->count + count <= NETMIDI2_MAX_UMP_WORDS) {
        memcpy(out->words + out->count, words, count * sizeof(*words));
        out->count += count;
    }
}

/* MIDI 1.0 bytes in, one UMP Data command out. */
static void send_midi(int index, const uint8_t *bytes, size_t length)
{
    static uint8_t buffer[NETMIDI2_MAX_PACKET];
    struct netmidi2_session *session = netmidi2_table_session(&table, index);
    struct ump_from_midi1 from;
    struct outgoing out;
    struct netmidi2_writer packet;
    size_t i;

    out.count = 0;
    ump_from_midi1_init(&from, 0);
    for (i = 0; i < length; ++i)
        ump_from_midi1_byte(&from, bytes[i], collect, &out);
    netmidi2_writer_init(&packet, buffer, sizeof(buffer));
    if (session && netmidi2_send_ump(session, out.words, out.count, now_ms(),
                                     &packet) == 0)
        send_to(session->peer, &packet);
}

static uint64_t now_us(void)
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_usec;
}

/* Our time as a JR Clock or JR Timestamp carries it: ticks of 1/31250
 * second, sixteen bits (M2-104-UM 7.2.2). */
static uint32_t jr_ticks(uint64_t us)
{
    return (uint32_t)((us / 32) & 0xffff);
}

static void send_words(int index, const uint32_t *words, unsigned count)
{
    static uint8_t buffer[NETMIDI2_MAX_PACKET];
    struct netmidi2_session *session = netmidi2_table_session(&table, index);
    struct netmidi2_writer packet;

    netmidi2_writer_init(&packet, buffer, sizeof(buffer));
    if (session && netmidi2_send_ump(session, words, count, now_ms(),
                                     &packet) == 0)
        send_to(session->peer, &packet);
}

/* --jr: asks the other end to take our timestamps, lets it learn our
 * clock, then sends twelve notes meant 50 ms apart, each up to 20 ms late
 * and unevenly so, with the time it was meant for. Nothing is read
 * meanwhile: it lasts a second. */
static void send_timed(int index)
{
    uint32_t words[4];
    uint64_t start;
    unsigned seed = 7;
    int i;

    words[0] = 0xf0050102u;         /* Stream Configuration Request:
                                       MIDI 1.0 protocol, you receive JR */
    words[1] = words[2] = words[3] = 0;
    send_words(index, words, 4);
    for (i = 0; i < 5; ++i) {
        usleep(20000);
        words[0] = 0x00100000u | jr_ticks(now_us());
        send_words(index, words, 1);
    }
    start = now_us() + 50000;
    for (i = 0; i < 12; ++i) {
        uint64_t ideal = start + (uint64_t)i * 50000;
        uint64_t leave, now;

        seed = seed * 1103515245u + 12345u;
        leave = ideal + (seed >> 16) % 20000;
        now = now_us();
        if (leave > now)
            usleep((useconds_t)(leave - now));
        words[0] = 0x00100000u | jr_ticks(now_us());
        words[1] = 0x00200000u | jr_ticks(ideal);
        words[2] = 0x20900064u | ((uint32_t)(40 + i) << 8);
        send_words(index, words, 3);
    }
    usleep(300000);
}

int main(int argc, char **argv)
{
    static uint8_t in[2048], out[NETMIDI2_MAX_PACKET];
    static const uint8_t messages[] = {
        0x90, 0x3c, 0x64, 0x80, 0x3c, 0x40, 0xb0, 0x07, 0x64,
        0xf0, 0x7d, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xf7
    };
    struct sockaddr_in local, source;
    struct netmidi2_writer packet;
    char name[64];
    int sending = 0;
    int timing = 0;
    int index;
    int argi = 2;

    if (argc < 2) {
        fputs("usage: netmidi2-peer PORT [HOST_ADDRESS HOST_PORT] [--send|--jr]\n",
              stderr);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((uint16_t)atoi(argv[1]));
    if (fd < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
        perror("bind");
        return 1;
    }
    snprintf(name, sizeof(name), "Peer on %s", argv[1]);
    netmidi2_table_init(&table, name, "netmidi2-peer", take_umps, NULL);
    for (index = 0; index < NETMIDI2_SESSIONS_MAX; ++index) {
        ump_to_midi1_init(&to_midi[index], 0);
        ump_to_midi1_buffer(&to_midi[index], sysex[index],
                            sizeof(sysex[index]));
    }
    if (argc >= 4 && argv[2][0] != '-') {
        struct sockaddr_in host;
        memset(&host, 0, sizeof(host));
        host.sin_family = AF_INET;
        host.sin_port = htons((uint16_t)atoi(argv[3]));
        if (inet_pton(AF_INET, argv[2], &host.sin_addr) != 1) {
            fputs("the Host has to be an IPv4 address\n", stderr);
            return 2;
        }
        netmidi2_writer_init(&packet, out, sizeof(out));
        if (netmidi2_table_invite(&table, key_of(&host), now_ms(),
                                  &packet) >= 0)
            send_to(key_of(&host), &packet);
        argi = 4;
    }
    if (argi < argc && strcmp(argv[argi], "--send") == 0)
        sending = 1;
    if (argi < argc && strcmp(argv[argi], "--jr") == 0)
        timing = 1;
    printf("listening port=%s\n", argv[1]);

    while (!stopping) {
        fd_set readable;
        struct timeval wait = {0, 20000};
        uint32_t now;

        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        if (select(fd + 1, &readable, NULL, NULL, &wait) > 0) {
            socklen_t size = sizeof(source);
            ssize_t count = recvfrom(fd, in, sizeof(in), 0,
                                     (struct sockaddr *)&source, &size);
            if (count > 0) {
                netmidi2_writer_init(&packet, out, sizeof(out));
                if (netmidi2_table_receive(&table, key_of(&source), in,
                                           (size_t)count, now_ms(), &packet,
                                           &index) == 0)
                    send_to(key_of(&source), &packet);
            }
        }
        now = now_ms();
        for (index = 0; index < NETMIDI2_SESSIONS_MAX; ++index) {
            struct netmidi2_session *session =
                netmidi2_table_session(&table, index);
            uint64_t peer;
            if (!session) {
                if (announced[index]) {
                    printf("closed slot=%d\n", index);
                    announced[index] = sent[index] = 0;
                    ump_to_midi1_buffer(&to_midi[index], sysex[index],
                                        sizeof(sysex[index]));
                }
                continue;
            }
            peer = session->peer;
            if (session->state == NETMIDI2_ESTABLISHED && !announced[index]) {
                printf("established slot=%d role=%s peer=\"%s\" id=\"%s\"\n",
                       index, session->host ? "host" : "client",
                       session->peer_name, session->peer_product_id);
                announced[index] = 1;
                established_at[index] = now;
            }
            if (timing && announced[index] && !sent[index] &&
                session->state == NETMIDI2_ESTABLISHED &&
                now - established_at[index] >= 300) {
                send_timed(index);
                sent[index] = 1;
                puts("sent");
                stopping = 1;
            }
            if (sending && announced[index] && !sent[index] &&
                session->state == NETMIDI2_ESTABLISHED &&
                now - established_at[index] >= 300) {
                send_midi(index, messages, sizeof(messages));
                sent[index] = 1;
                puts("sent");
            }
            netmidi2_writer_init(&packet, out, sizeof(out));
            if (netmidi2_table_tick(&table, index, now, &packet))
                send_to(peer, &packet);
        }
    }
    for (index = 0; index < NETMIDI2_SESSIONS_MAX; ++index) {
        struct netmidi2_session *session =
            netmidi2_table_session(&table, index);
        netmidi2_writer_init(&packet, out, sizeof(out));
        if (session && netmidi2_bye(session, NETMIDI2_BYE_USER_TERMINATED,
                                    now_ms(), &packet) == 0)
            send_to(session->peer, &packet);
    }
    close(fd);
    return 0;
}
