/* Feeds the decoders packets nobody would send: valid ones, built by our
 * own encoders, with bytes changed, cut off, or added. Built with the
 * address and undefined-behaviour sanitizers, which stop the run at the
 * first read or write out of place; a run that ends has found none.
 *
 *   fuzz [iterations [seed]]
 *
 * No fuzzing tool is needed: the changes are made here, from a generator
 * that gives the same run for the same seed. It is less clever than a tool
 * that watches which code a packet reaches. */

#include "rtpmidi/applemidi.h"
#include "rtpmidi/clock.h"
#include "rtpmidi/peers.h"
#include "rtpmidi/receiver.h"
#include "rtpmidi/rtpmidi.h"
#include "rtpmidi/sender.h"
#include "rtpmidi/timing.h"
#include "netmidi2/netmidi2.h"
#include "ump/ump.h"
#include <blemidi/ble_midi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t state = 0x9e3779b97f4a7c15ull;

/* How far the changed packets got, to know the run is not all refusals. */
static unsigned long rtp_given, rtp_decoded, rtp_journals, rtp_recovered;

static uint32_t rnd(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return (uint32_t)(state >> 16);
}

/* Changes a packet in place and returns its new length. */
static size_t mutate(uint8_t *data, size_t length, size_t capacity)
{
    unsigned changes = 1 + rnd() % 4;

    while (changes--) {
        switch (rnd() % 6) {
        case 0:                             /* a byte to anything */
            if (length)
                data[rnd() % length] = (uint8_t)rnd();
            break;
        case 1:                             /* a bit */
            if (length)
                data[rnd() % length] ^= (uint8_t)(1u << (rnd() % 8));
            break;
        case 2:                             /* cut off */
            if (length)
                length = rnd() % length;
            break;
        case 3:                             /* bytes added at the end */
            while (length < capacity && rnd() % 4)
                data[length++] = (uint8_t)rnd();
            break;
        case 4:                             /* a byte to an extreme */
            if (length)
                data[rnd() % length] = rnd() & 1 ? 0xff : 0x00;
            break;
        default:                            /* a piece copied elsewhere */
            if (length > 4) {
                size_t from = rnd() % length, to = rnd() % length;
                size_t n = 1 + rnd() % 8;
                if (from + n <= length && to + n <= length)
                    memmove(data + to, data + from, n);
            }
            break;
        }
    }
    return length;
}

static void no_message(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    (void)bytes;
    (void)length;
}

static void no_report(void *context, const char *text)
{
    (void)context;
    (void)text;
}

static const struct rtpmidi_receiver_output quiet = {
    no_message, no_message, no_report
};

/* One valid RTP-MIDI packet with a journal behind it, from a sender that
 * has sent a little of everything. */
static size_t rtp_seed(uint8_t *wire, size_t capacity)
{
    static struct rtpmidi_sender sender;
    static const uint8_t status[] = {0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0,
                                     0xe0, 0xf1, 0xf2, 0xf3, 0xf6, 0xf8,
                                     0xfa, 0xfb, 0xfc, 0xfe, 0xff};
    static const uint8_t controller[] = {0, 6, 7, 32, 38, 64, 96, 97, 98,
                                         99, 100, 101, 120, 121, 123, 127};
    uint8_t message[3];
    uint8_t sysex[40];
    size_t length = 0;
    unsigned n = 2 + rnd() % 24;
    unsigned i;
    uint16_t sequence = (uint16_t)rnd();

    rtpmidi_sender_reset(&sender);
    for (i = 0; i < n; ++i) {
        size_t size;

        message[0] = status[rnd() % sizeof(status)];
        if (message[0] < 0xf0)
            message[0] |= (uint8_t)(rnd() % 3);
        message[1] = (uint8_t)(rnd() & 0x7f);
        message[2] = (uint8_t)(rnd() & 0x7f);
        if ((message[0] & 0xf0) == 0xb0 && rnd() % 2)
            message[1] = controller[rnd() % sizeof(controller)];
        size = message[0] >= 0xf4 ? 1 :
               (message[0] & 0xf0) == 0xc0 || (message[0] & 0xf0) == 0xd0 ||
               message[0] == 0xf1 || message[0] == 0xf3 ? 2 : 3;
        if (rnd() % 8 == 0) {
            size_t k, m = 3 + rnd() % 30;

            sysex[0] = 0xf0;
            for (k = 1; k + 1 < m; ++k)
                sysex[k] = (uint8_t)(rnd() & 0x7f);
            sysex[m - 1] = 0xf7;
            rtpmidi_sender_record_sysex(&sender, sequence, i, sysex, m);
            ++sequence;
            continue;
        }
        if (rtpmidi_sender_packet(&sender, sequence, i, 1, message, size,
                                  wire, capacity, &length) != 0)
            length = 0;
        ++sequence;
    }
    if (!length) {
        message[0] = 0x90;
        message[1] = 60;
        message[2] = 100;
        (void)rtpmidi_sender_packet(&sender, sequence, 0, 1, message, 3,
                                    wire, capacity, &length);
    }
    return length;
}

static void rtp_take(const uint8_t *data, size_t length)
{
    static struct rtpmidi_receiver receiver;
    static struct rtpmidi_event_queue queue;
    static struct rtpmidi_sysex_assembler assembler;
    static uint8_t gathered[RTPMIDI_SYSEX_MAX];
    static int ready;
    struct rtpmidi_packet packet;
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_system_state system;
    struct rtpmidi_journal_sysex sysex;
    struct rtpmidi_receiver_clock clock;
    const uint8_t *message;
    size_t message_length;
    unsigned guard = 0;
    size_t i;

    if (!ready || rnd() % 64 == 0) {
        rtpmidi_queue_reset(&queue);
        rtpmidi_receiver_init(&receiver, &quiet, NULL, &queue);
        rtpmidi_sysex_buffer(&assembler, gathered, sizeof(gathered));
        ready = 1;
    }
    ++rtp_given;
    if (rtpmidi_decode(data, length, &packet) != 0)
        return;
    ++rtp_decoded;
    rtpmidi_reader_init(&reader, &packet);
    while (rtpmidi_reader_next(&reader, &event) == 1 && ++guard < 5000) {
        if (event.sysex) {
            if (rtpmidi_sysex_feed(&assembler, &event, packet.sequence,
                                   &message, &message_length) == 1)
                rtpmidi_receiver_deliver_sysex(&receiver, message,
                                               message_length);
        } else {
            rtpmidi_receiver_deliver(&receiver, event.bytes, event.length);
        }
    }
    if (!packet.journal ||
        rtpmidi_journal_decode(packet.journal, packet.journal_length,
                               &journal) != 0)
        return;
    ++rtp_journals;
    {
        int have_system = rtpmidi_journal_decode_system(&journal, &system) > 0;

        if (have_system)
            (void)rtpmidi_journal_decode_sysex(&system, &sysex);
        for (i = 0; i < journal.channel_count; ++i) {
            static struct rtpmidi_journal_notes notes;
            static struct rtpmidi_journal_note_extras extras;
            static struct rtpmidi_journal_channel_state channel_state;
            static struct rtpmidi_journal_controls controls;
            static struct rtpmidi_journal_parameters parameters;
            static struct rtpmidi_journal_aftertouch aftertouch;

            (void)rtpmidi_journal_decode_notes(&journal.channels[i], &notes);
            (void)rtpmidi_journal_decode_note_extras(&journal.channels[i],
                                                     &extras);
            (void)rtpmidi_journal_decode_channel_state(&journal.channels[i],
                                                       &channel_state);
            (void)rtpmidi_journal_decode_controls(&journal.channels[i],
                                                  &controls);
            (void)rtpmidi_journal_decode_parameters(&journal.channels[i],
                                                    &parameters);
            (void)rtpmidi_journal_decode_aftertouch(&journal.channels[i],
                                                    &aftertouch);
        }
        clock.synchronized = (int)(rnd() & 1);
        clock.peer_to_local = (int64_t)(int32_t)rnd();
        clock.now = 1000000 + rnd() % 100000;
        rtpmidi_receiver_recover(&receiver, &journal,
                                 have_system ? &system : NULL,
                                 (int)(rnd() & 1), packet.timestamp,
                                 rnd() & 1 ? &clock : NULL);
        ++rtp_recovered;
    }
    /* what recovery queued for later is taken off again */
    {
        struct rtpmidi_queued_event queued;
        guard = 0;
        while (rtpmidi_queue_pop_due(&queue, UINT64_MAX, &queued) &&
               ++guard < 1000)
            rtpmidi_queue_event_release(&queued);
    }
}

static void applemidi_take(void)
{
    static const uint8_t name[] = "A Session";
    struct applemidi_packet packet, decoded;
    uint8_t wire[128];
    size_t length = 0;

    memset(&packet, 0, sizeof(packet));
    switch (rnd() % 4) {
    case 0: packet.command = APPLEMIDI_IN; break;
    case 1: packet.command = APPLEMIDI_OK; break;
    case 2: packet.command = APPLEMIDI_CK; break;
    default: packet.command = APPLEMIDI_BY; break;
    }
    packet.ssrc = rnd();
    packet.name = name;
    packet.name_length = sizeof(name) - 1;
    packet.sync_count = (uint8_t)(rnd() % 3);
    if (applemidi_encode(&packet, wire, sizeof(wire), &length) != 0)
        length = 0;
    if (rnd() % 3 == 0)
        length = mutate(wire, length, sizeof(wire));
    if (applemidi_decode(wire, length, &decoded) != APPLEMIDI_DECODED)
        return;
    if (decoded.name) {
        /* the name is inside the packet */
        volatile uint8_t touch = 0;
        size_t i;
        for (i = 0; i < decoded.name_length; ++i)
            touch ^= decoded.name[i];
        (void)touch;
    }
    /* The table of peers and a clock take whatever was decoded, from a
       handful of addresses and either port, in any order: invitations,
       answers nobody asked for, goodbyes from strangers. */
    {
        static struct applemidi_peers peers;
        static struct applemidi_clock clock;
        static uint32_t now;
        static int ready;
        struct applemidi_packet reply;
        enum applemidi_peer_event event;
        int data_port, index;
        uint32_t address = 0xc0a80001u + rnd() % 4;
        uint16_t port = (uint16_t)(5004 + 2 * (rnd() % 3));
        int on_data = (int)(rnd() & 1);

        if (!ready || rnd() % 500 == 0) {
            (void)applemidi_peers_init(&peers, rnd(),
                                       (const uint8_t *)"Fuzzed", 6);
            if (rnd() % 4 == 0)
                peers.limit = 1 + (int)(rnd() % 3);
            if (rnd() % 4 == 0) {
                peers.accept = APPLEMIDI_ACCEPT_LISTED;
                (void)applemidi_peers_allow(&peers, 0xc0a80002u, NULL);
                (void)applemidi_peers_allow(&peers, 0, "A Session");
            }
            applemidi_clock_init(&clock, (int)(rnd() & 1), now);
            ready = 1;
        }
        now += rnd() % 3000;
        if (rnd() % 16 == 0)
            decoded.ssrc = peers.local_ssrc;        /* ours, as a peer's */
        (void)applemidi_peers_receive(&peers, address,
                                      (uint16_t)(port + on_data), on_data,
                                      &decoded, now, &reply, &data_port,
                                      &index, &event);
        if (rnd() % 8 == 0)
            (void)applemidi_peers_invite(&peers, address, port, rnd(), now,
                                         &reply);
        index = (int)(rnd() % APPLEMIDI_PEERS_MAX);
        if (peers.peer[index].used) {
            if (rnd() % 8 == 0)
                (void)applemidi_peers_retry(&peers, index, &reply, &data_port);
            if (rnd() % 32 == 0)
                (void)applemidi_peers_end(&peers, index, &reply);
            applemidi_peers_seen(&peers, index, now);
        }
        (void)applemidi_peers_find_data(&peers, address, (uint16_t)(port + 1));
        (void)applemidi_peers_find_ssrc(&peers, decoded.ssrc);
        if (decoded.command == APPLEMIDI_CK)
            (void)applemidi_clock_receive(&clock, peers.local_ssrc, &decoded,
                                          now, &reply);
        (void)applemidi_clock_poll(&clock, peers.local_ssrc, now, &reply);
        (void)applemidi_clock_expired(&clock, now);
    }
}

static void no_umps(void *context, int index, const uint32_t *words,
                    size_t count)
{
    volatile uint32_t touch = 0;
    size_t i;
    (void)context;
    (void)index;
    for (i = 0; i < count; ++i)
        touch ^= words[i];
    (void)touch;
}

/* Two tables talk; what one sends is changed on its way to the other. */
static void netmidi2_take(void)
{
    static struct netmidi2_table host, client;
    static uint8_t a[NETMIDI2_MAX_PACKET + 64], b[NETMIDI2_MAX_PACKET + 64];
    static uint32_t now;
    static int ready;
    struct netmidi2_writer out, reply;
    uint32_t words[4];
    int index;
    size_t length;
    unsigned turn;

    if (!ready || rnd() % 200 == 0) {
        netmidi2_table_init(&host, "Host", "h-1", no_umps, NULL);
        netmidi2_table_init(&client, "Client", "c-1", no_umps, NULL);
        host.accept = 1;
        ready = 1;
        netmidi2_writer_init(&out, a, NETMIDI2_MAX_PACKET);
        (void)netmidi2_table_invite(&client, 77, now, &out);
        netmidi2_writer_init(&reply, b, NETMIDI2_MAX_PACKET);
        (void)netmidi2_table_receive(&host, 99, a, out.length, now, &reply,
                                     &index);
        netmidi2_writer_init(&out, a, NETMIDI2_MAX_PACKET);
        (void)netmidi2_table_receive(&client, 77, b, reply.length, now, &out,
                                     &index);
    }
    now += rnd() % 400;
    for (turn = 0; turn < 2; ++turn) {
        struct netmidi2_table *from = turn ? &host : &client;
        struct netmidi2_table *to = turn ? &client : &host;
        uint64_t from_key = turn ? 77 : 99, to_key = turn ? 99 : 77;
        struct netmidi2_session *session = netmidi2_table_session(from, 0);

        netmidi2_writer_init(&out, a, NETMIDI2_MAX_PACKET);
        if (session && rnd() % 8 == 0) {
            /* a stream message: discovery of the endpoint or of its
               function block, or a request about timestamps */
            static const uint32_t stream[] = {
                0xf0000101u, 0xf0100003u, 0xf010ff01u, 0xf0050103u
            };
            words[0] = stream[rnd() % 4] ^ (rnd() % 4 ? 0 : rnd() & 0xffff);
            words[1] = rnd() & 0xff;
            words[2] = words[3] = 0;
            (void)netmidi2_send_ump(session, words, 4, now, &out);
        } else if (rnd() % 16 == 0) {
            /* asking for what was sent to be sent again, from anywhere */
            uint8_t count[4];

            count[0] = count[1] = count[2] = count[3] = (uint8_t)rnd();
            (void)netmidi2_add(&out, NETMIDI2_RETRANSMIT_REQUEST,
                               (uint8_t)(rnd() % 3 ? 0 : rnd()),
                               (uint8_t)rnd(), count, 4);
        } else if (session && rnd() % 2) {
            words[0] = 0x20900000u | (rnd() & 0x7f7f);
            words[1] = rnd();
            (void)netmidi2_send_ump(session, words, 1 + rnd() % 2, now, &out);
        } else {
            (void)netmidi2_table_tick(from, 0, now, &out);
        }
        length = out.length;
        if (rnd() % 3 == 0)
            length = mutate(a, length, sizeof(a));
        netmidi2_writer_init(&reply, b, NETMIDI2_MAX_PACKET);
        (void)netmidi2_table_receive(to, rnd() % 8 ? from_key : rnd() % 5,
                                     a, length, now, &reply, &index);
        (void)to_key;
    }
}

static void no_ump(void *context, const uint32_t *words, unsigned count)
{
    (void)context;
    (void)words;
    (void)count;
}

static void ump_take(void)
{
    static struct ump_to_midi1 down;
    static struct ump_from_midi1 up;
    static uint8_t buffer[64];
    static int ready;
    uint32_t words[4];
    unsigned i, n;

    if (!ready || rnd() % 100 == 0) {
        ump_to_midi1_init(&down, (uint8_t)(rnd() & 15));
        ump_to_midi1_buffer(&down, buffer, sizeof(buffer));
        ump_from_midi1_init(&up, 0);
        ready = 1;
    }
    for (i = 0; i < 4; ++i)
        words[i] = rnd();
    /* often a type that is translated, and of the translator's group */
    if (rnd() % 2)
        words[0] = (words[0] & 0x00ffffffu) |
                   ((uint32_t)(rnd() % 6) << 28) | ((uint32_t)down.group << 24);
    n = ump_words(words[0]);
    (void)ump_to_midi1(&down, words, rnd() % 8 ? n : rnd() % 5, no_message,
                       NULL);
    for (i = 0; i < 6; ++i) {
        uint8_t byte = (uint8_t)rnd();
        if (rnd() % 3)
            byte &= 0x7f;
        ump_from_midi1_byte(&up, byte, no_ump, NULL);
    }
}

static int no_byte(void *context, uint16_t time, uint8_t byte)
{
    (void)context;
    (void)time;
    (void)byte;
    return 0;
}

static void ble_take(void)
{
    static struct blemidi_midi_decoder decoder;
    static const uint8_t valid[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 64, 90,
                                    0x82, 0xf0, 1, 2, 0x83, 0xf8, 3};
    uint8_t packet[40];
    size_t length = sizeof(valid);

    if (rnd() % 50 == 0)
        blemidi_midi_decoder_init(&decoder);
    memcpy(packet, valid, sizeof(valid));
    length = mutate(packet, length, sizeof(packet));
    (void)blemidi_midi_decode(&decoder, packet, length, no_byte, NULL);
}

int main(int argc, char **argv)
{
    static uint8_t wire[1400];
    unsigned long iterations = argc > 1 ? strtoul(argv[1], NULL, 10) : 200000;
    unsigned long i;

    if (argc > 2)
        state ^= strtoull(argv[2], NULL, 10) * 0x2545f4914f6cdd1dull;
    for (i = 0; i < iterations; ++i) {
        size_t length = rtp_seed(wire, 1100);

        rtp_take(wire, length);                     /* as it was made */
        length = mutate(wire, length, sizeof(wire));
        rtp_take(wire, length);
        applemidi_take();
        netmidi2_take();
        ump_take();
        ble_take();
    }
    printf("fuzz: %lu rounds without a finding; of %lu RTP packets %lu were "
           "taken as RTP-MIDI, %lu had a journal that was read and %lu of "
           "those went through recovery\n", iterations, rtp_given,
           rtp_decoded, rtp_journals, rtp_recovered);
    return 0;
}
