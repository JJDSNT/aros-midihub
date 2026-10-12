#ifndef __AROS__
#define _DEFAULT_SOURCE 1
#endif

#include <rtpmidi/applemidi.h>
#include <midihub/config.h>
#include <netmidi2/netmidi2.h>
#include <ump/ump.h>
#include <rtpmidi/clock.h>
#include <rtpmidi/peers.h>
#include <rtpmidi/receiver.h>
#include <rtpmidi/rtpmidi.h>
#include <rtpmidi/session.h>
#include <rtpmidi/sender.h>
#include <rtpmidi/timing.h>
#include "camd.h"
#include "discovery.h"
#include "network.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __AROS__
#include <dos/dos.h>
#include <proto/bsdsocket.h>
#else
#include <signal.h>
static volatile sig_atomic_t stopping;
static void stop_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}
#endif

/* What is kept for one peer's stream, beside its session in the peer
 * table at the same index. */
struct stream {
    int initiating;             /* we sent the invitation */
    struct applemidi_clock clock;
    int have_received_sequence;
    uint16_t sequence;          /* of the next packet to this peer */
    uint32_t received_sequence;
    uint64_t last_invite;
    uint64_t last_rtp_send;
    uint64_t last_activity;
    unsigned int retries;
    struct rtpmidi_sysex_assembler sysex;
    struct rtpmidi_event_queue queue;
    struct rtpmidi_sender sender;
    struct rtpmidi_receiver receiver;
};

struct runtime {
    struct netmidi_network_runtime network;
    struct netmidi_discovery discovery;
    int discovered;             /* services the system advertises for us */
    int configured;             /* a peer was named to invite */
    uint32_t configured_address;
    uint16_t configured_port;
    /* a peer to invite wherever it is advertised, by its session's name */
    char wanted[64];
    int wanted_present;
    uint64_t wanted_tried;
    uint64_t wanted_wait;
    int probe_note;
    int probe_sysex;
    int probe_state;
    uint64_t note_time;
    char session_name[64];
    struct applemidi_peers peers;
    struct stream stream[APPLEMIDI_PEERS_MAX];
    /* Network MIDI 2.0, on a port of its own when one is given */
    int midi2_enabled;
    struct netmidi2_table midi2;
    struct ump_to_midi1 midi2_in[NETMIDI2_SESSIONS_MAX];
    uint8_t midi2_sysex[NETMIDI2_SESSIONS_MAX][RTPMIDI_SYSEX_MAX];
    int midi2_known[NETMIDI2_SESSIONS_MAX];
    unsigned long midi2_gaps[NETMIDI2_SESSIONS_MAX];
    unsigned long midi2_resets[NETMIDI2_SESSIONS_MAX];
    struct netmidi_camd_runtime camd;
};

static uint64_t now_ticks(void)
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (uint64_t)time.tv_sec * 10000 + (uint64_t)time.tv_usec / 100;
}

static uint64_t elapsed_ticks(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

static int parse_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long number = strtoul(text, &end, 10);
    if (!*text || *text == '-' || *end || number == 0 || number >= 65535)
        return -1;
    *port = (uint16_t)number;
    return 0;
}

static void peer_address(const struct applemidi_peer *peer, int data_port,
                         struct sockaddr_in *address)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(peer->address);
    address->sin_port = htons((uint16_t)(peer->control_port +
                                         (data_port ? 1 : 0)));
}

static int send_apple_to(struct runtime *rt, int data_port,
                         const struct sockaddr_in *address,
                         const struct applemidi_packet *packet)
{
    uint8_t bytes[128];
    size_t length;
    if (applemidi_encode(packet, bytes, sizeof(bytes), &length) != 0)
        return -1;
    return netmidi_network_send(&rt->network, data_port, bytes, length,
                                address);
}

static int send_apple(struct runtime *rt, int index, int data_port,
                      const struct applemidi_packet *packet)
{
    struct sockaddr_in address;
    peer_address(&rt->peers.peer[index], data_port, &address);
    return send_apple_to(rt, data_port, &address, packet);
}

static void send_sync(struct runtime *rt, int index, uint64_t now)
{
    struct applemidi_packet sync;
    if (applemidi_clock_poll(&rt->stream[index].clock, rt->peers.local_ssrc,
                             now, &sync))
        send_apple(rt, index, 1, &sync);
}

/* A value nobody can guess, for an SSRC or a token. */
static uint32_t random32(uint64_t now)
{
    uint32_t value;
    if (netmidi_network_random(&value, sizeof(value)) != 0) {
        static int told;
        if (!told) {
            puts("MIDIHub: no random source; identifiers are weak");
            srand((unsigned int)(now ^ (now >> 32)));
            told = 1;
        }
        value = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
    }
    return value;
}

static void output_message(void *context, const uint8_t *bytes,
                           size_t length)
{
    struct runtime *rt = context;
    netmidi_camd_deliver(&rt->camd, bytes, length);
}

static void output_sysex(void *context, const uint8_t *message, size_t length)
{
    struct runtime *rt = context;
    netmidi_camd_deliver_sysex(&rt->camd, message, length);
}

static void output_report(void *context, const char *text)
{
    (void)context;
    fputs("MIDIHub: ", stdout);
    fputs(text, stdout);
}

static const struct rtpmidi_receiver_output receiver_output = {
    output_message, output_sysex, output_report
};

/* A slot of the peer table has a new occupant: nothing of the last one's
 * stream may be left in it. */
static void stream_begin(struct runtime *rt, int index, int initiating,
                         uint64_t now)
{
    struct stream *stream = &rt->stream[index];
    rtpmidi_queue_reset(&stream->queue);
    memset(stream, 0, sizeof(*stream));
    stream->initiating = initiating;
    stream->last_invite = now;
    stream->last_activity = now;
    stream->sequence = (uint16_t)random32(now);
    applemidi_clock_init(&stream->clock, initiating, now);
    rtpmidi_sysex_reset(&stream->sysex);
    rtpmidi_queue_reset(&stream->queue);
    rtpmidi_sender_reset(&stream->sender);
    rtpmidi_receiver_init(&stream->receiver, &receiver_output, rt,
                          &stream->queue);
}

/* The session is over, or began again: what the peer left sounding is
 * released and what was kept for its stream goes. */
static void stream_forget(struct runtime *rt, int index, uint64_t now)
{
    struct stream *stream = &rt->stream[index];
    applemidi_clock_init(&stream->clock, stream->initiating, now);
    stream->have_received_sequence = 0;
    rtpmidi_sysex_reset(&stream->sysex);
    rtpmidi_queue_reset(&stream->queue);
    if (stream->receiver.output) {
        rtpmidi_receiver_release_notes(&stream->receiver);
        rtpmidi_receiver_reset_channels(&stream->receiver);
        rtpmidi_receiver_reset_system(&stream->receiver);
    }
    rtpmidi_sender_reset(&stream->sender);
}

static int invite_configured(struct runtime *rt, uint64_t now)
{
    struct applemidi_packet invite;
    int index = applemidi_peers_invite(&rt->peers, rt->configured_address,
                                       rt->configured_port, random32(now),
                                       (uint32_t)(now / 10), &invite);
    if (index < 0)
        return -1;
    stream_begin(rt, index, 1, now);
    return send_apple(rt, index, 0, &invite);
}

static int configured_index(const struct runtime *rt)
{
    return rt->configured ?
           applemidi_peers_find_control(&rt->peers, rt->configured_address,
                                        rt->configured_port) : -1;
}

static int send_feedback(struct runtime *rt, int index, uint16_t sequence)
{
    struct stream *stream = &rt->stream[index];
    struct applemidi_packet feedback;
    int16_t advance;
    int fresh = 1;

    if (!stream->have_received_sequence) {
        stream->received_sequence = sequence;
        stream->have_received_sequence = 1;
    } else {
        advance = (int16_t)(sequence - (uint16_t)stream->received_sequence);
        if (advance <= 0)
            fresh = 0;
        else
            stream->received_sequence += (uint16_t)advance;
    }
    memset(&feedback, 0, sizeof(feedback));
    feedback.command = APPLEMIDI_RS;
    feedback.ssrc = rt->peers.local_ssrc;
    feedback.feedback_sequence =
        applemidi_feedback_field((uint16_t)stream->received_sequence);
    send_apple(rt, index, 0, &feedback);
    return fresh;
}

static void receive_rtp(struct runtime *rt, int index, const uint8_t *bytes,
                        size_t count)
{
    struct stream *stream = &rt->stream[index];
    struct rtpmidi_packet midi;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_system_state system_state;
    struct rtpmidi_journal_sysex sysex_state;
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;
    const uint8_t *sysex_message;
    size_t sysex_length;
    int action;
    int sysex_result;
    int system_decoded = 0;
    int16_t sequence_advance = 0;
    uint16_t previous_sequence = 0;
    uint64_t now;
    size_t i;

    if (rtpmidi_decode(bytes, count, &midi) != 0 ||
        midi.ssrc != rt->peers.peer[index].session.peer_ssrc)
        return;
    if (midi.journal &&
        rtpmidi_journal_decode(midi.journal, midi.journal_length,
                          &journal) != 0) {
        puts("MIDIHub: malformed recovery journal discarded");
        return;
    }
    if (midi.journal && journal.system) {
        system_decoded = rtpmidi_journal_decode_system(&journal, &system_state);
        if (system_decoded < 0) {
            puts("MIDIHub: malformed system recovery journal discarded");
            return;
        }
        if (system_state.sysex &&
            rtpmidi_journal_decode_sysex(&system_state, &sysex_state) < 0) {
            puts("MIDIHub: malformed SysEx recovery journal discarded");
            return;
        }
    }
    rtpmidi_reader_init(&reader, &midi);
    while ((action = rtpmidi_reader_next(&reader, &event)) == 1) {}
    if (action < 0) {
        puts("MIDIHub: unsupported or malformed MIDI command");
        return;
    }
    if (stream->have_received_sequence) {
        previous_sequence = (uint16_t)stream->received_sequence;
        sequence_advance = (int16_t)(midi.sequence -
                                     previous_sequence);
        if (sequence_advance > 1) {
            printf("MIDIHub: %u RTP packet(s) lost; journal %s\n",
                   (unsigned int)(sequence_advance - 1),
                   !midi.journal ? "unavailable" :
                   rtpmidi_journal_covers_gap(
                       previous_sequence, midi.sequence, journal.checkpoint) ?
                                             "covers gap" :
                                             "does not cover gap");
        }
    }
    if (!send_feedback(rt, index, midi.sequence))
        return; /* A duplicate or older packet must not replay MIDI events. */
    if (sequence_advance > 1 && midi.journal &&
        rtpmidi_journal_covers_gap(previous_sequence, midi.sequence,
                              journal.checkpoint)) {
        struct rtpmidi_receiver_clock clock;
        clock.synchronized = stream->clock.synchronized;
        clock.peer_to_local = stream->clock.peer_to_local;
        clock.now = now_ticks();
        rtpmidi_receiver_recover(&stream->receiver, &journal,
                            system_decoded > 0 ? &system_state : NULL,
                            sequence_advance == 2, midi.timestamp, &clock);
    }
    if (!midi.midi_length)
        return; /* Guard packet: acknowledge its journal without MIDI output. */
    printf("MIDIHub: RTP-MIDI seq=%u timestamp=%lu length=%lu bytes=",
           (unsigned int)midi.sequence, (unsigned long)midi.timestamp,
           (unsigned long)midi.midi_length);
    for (i = 0; i < midi.midi_length && i < 16; ++i)
        printf("%02x", (unsigned int)midi.midi[i]);
    if (i < midi.midi_length)
        fputs("...", stdout);
    putchar('\n');
    rtpmidi_reader_init(&reader, &midi);
    while ((action = rtpmidi_reader_next(&reader, &event)) == 1) {
        if (event.sysex) {
            sysex_result = rtpmidi_sysex_feed(&stream->sysex, &event,
                                          midi.sequence,
                                          &sysex_message, &sysex_length);
            if (sysex_result == 1) {
                uint64_t due;
                printf("MIDIHub: SysEx complete bytes=%lu\n",
                       (unsigned long)sysex_length);
                now = now_ticks();
                if (stream->clock.synchronized &&
                    rtpmidi_clock_due(stream->sysex.timestamp, now,
                                 stream->clock.peer_to_local, &due) == 0 &&
                    due > now) {
                    if (due - now > 100000 ||
                        rtpmidi_queue_push_sysex(&stream->queue, due,
                                            sysex_message,
                                            sysex_length) != 0)
                        puts("MIDIHub: future SysEx event discarded");
                    continue;
                }
                rtpmidi_receiver_deliver_sysex(&stream->receiver,
                                               sysex_message, sysex_length);
            } else if (sysex_result < 0)
                puts("MIDIHub: SysEx segment discarded");
        } else {
            uint64_t due;
            now = now_ticks();
            if (stream->clock.synchronized &&
                rtpmidi_clock_due(event.timestamp, now,
                             stream->clock.peer_to_local, &due) == 0 &&
                due > now) {
                if (due - now > 100000 ||
                    rtpmidi_queue_push(&stream->queue, due, event.bytes,
                                  event.length) != 0)
                    puts("MIDIHub: future MIDI event discarded");
                continue;
            }
            rtpmidi_receiver_deliver(&stream->receiver, event.bytes,
                                     event.length);
        }
    }
    if (action < 0)
        puts("MIDIHub: unsupported or malformed MIDI command");
}

static void receive_packet(struct runtime *rt, int data_port)
{
    struct sockaddr_in source;
    socklen_t source_length = sizeof(source);
    uint8_t bytes[1500];
    int count = netmidi_network_receive(&rt->network, data_port, bytes,
                                        sizeof(bytes), &source,
                                        &source_length);
    struct applemidi_packet packet;
    struct applemidi_packet response;
    enum applemidi_peer_event event;
    uint32_t address;
    uint16_t port;
    unsigned int collisions;
    unsigned int completed;
    int response_port;
    int index;
    int action;
    uint64_t now;

    if (count <= 0 || source_length < sizeof(source) ||
        source.sin_family != AF_INET)
        return;
    address = ntohl(source.sin_addr.s_addr);
    port = ntohs(source.sin_port);
    if (port == 65535 || (data_port && port == 0))
        return;
    now = now_ticks();
    if (applemidi_decode(bytes, (size_t)count, &packet) != 0) {
        index = data_port ?
                applemidi_peers_find_data(&rt->peers, address, port) : -1;
        if (index >= 0) {
            rt->stream[index].last_activity = now;
            applemidi_peers_seen(&rt->peers, index, (uint32_t)(now / 10));
            receive_rtp(rt, index, bytes, (size_t)count);
        }
        return;
    }
    if (packet.command == APPLEMIDI_CK) {
        index = data_port ?
                applemidi_peers_find_data(&rt->peers, address, port) : -1;
        if (index < 0 ||
            packet.ssrc != rt->peers.peer[index].session.peer_ssrc)
            return;
        rt->stream[index].last_activity = now;
        applemidi_peers_seen(&rt->peers, index, (uint32_t)(now / 10));
        completed = rt->stream[index].clock.completed;
        if (applemidi_clock_receive(&rt->stream[index].clock,
                                    rt->peers.local_ssrc, &packet, now,
                                    &response))
            send_apple(rt, index, 1, &response);
        if (rt->stream[index].clock.completed != completed)
            puts("MIDIHub: clock exchange completed");
        return;
    }
    if (packet.command == APPLEMIDI_RS) {
        index = data_port ? -1 :
                applemidi_peers_find_control(&rt->peers, address, port);
        if (index >= 0 &&
            rt->peers.peer[index].session.phase ==
                APPLEMIDI_SESSION_CONNECTED &&
            packet.ssrc == rt->peers.peer[index].session.peer_ssrc)
            rtpmidi_sender_ack(&rt->stream[index].sender,
                applemidi_feedback_acked(packet.feedback_sequence,
                    (uint16_t)(rt->stream[index].sequence - 1)));
        return;
    }

    /* IN, OK, NO, BY: the peer table's business. Whether the packet made a
     * new peer is seen from the slot it comes back with. */
    index = applemidi_peers_find_control(&rt->peers, address,
                (uint16_t)(data_port ? port - 1 : port));
    collisions = rt->peers.ssrc_collisions;
    {
        int known = index;
        action = applemidi_peers_receive(&rt->peers, address, port, data_port,
                                         &packet, (uint32_t)(now / 10),
                                         &response, &response_port, &index,
                                         &event);
        if (action < 0)
            return;
        if (known < 0 && index >= 0)
            stream_begin(rt, index, 0, now);
    }
    if (action == 1) {
        /* a refusal goes to someone who has no place in the table */
        source.sin_port = htons((uint16_t)(data_port == response_port ?
            port : response_port ? port + 1 : port - 1));
        send_apple_to(rt, response_port, &source, &response);
    }
    if (rt->peers.ssrc_collisions != collisions) {
        puts("MIDIHub: peer has our SSRC");
        if (applemidi_peers_renew(&rt->peers, random32(now)) == 0) {
            puts("MIDIHub: taking another SSRC");
            if (rt->configured)
                invite_configured(rt, now);
        }
        return;
    }
    if (index < 0)
        return;
    rt->stream[index].last_activity = now;
    if (packet.command == APPLEMIDI_OK && action == 1 &&
        response.command == APPLEMIDI_IN) {
        rt->stream[index].last_invite = now;
        rt->stream[index].retries = 0;
    }
    if (event == APPLEMIDI_PEER_RESTARTED) {
        puts("MIDIHub: peer began again");
        stream_forget(rt, index, now);
    } else if (event == APPLEMIDI_PEER_CONNECTED) {
        puts("MIDIHub: session connected");
        applemidi_clock_init(&rt->stream[index].clock,
                             rt->stream[index].initiating, now);
        send_sync(rt, index, now);
    } else if (event == APPLEMIDI_PEER_LEFT) {
        puts(packet.command == APPLEMIDI_NO ?
             "MIDIHub: invitation refused" : "MIDIHub: peer disconnected");
        stream_forget(rt, index, now);
    }
}

/* Network MIDI 2.0. A peer's UMPs become MIDI 1.0 for CAMD by the default
 * translation, and what CAMD sends becomes UMPs with the MIDI 1.0 protocol:
 * this endpoint declares no other. Each session has a translator of its
 * own; all of them, like the AppleMIDI peers, meet in the one pair of
 * clusters. */

static uint64_t midi2_key(const struct sockaddr_in *address)
{
    return ((uint64_t)ntohl(address->sin_addr.s_addr) << 16) |
           ntohs(address->sin_port);
}

static void midi2_send(struct runtime *rt, uint64_t key,
                       const struct netmidi2_writer *packet)
{
    struct sockaddr_in address;
    if (packet->length <= 4)
        return;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl((uint32_t)(key >> 16));
    address.sin_port = htons((uint16_t)key);
    netmidi_network_midi2_send(&rt->network, packet->data, packet->length,
                               &address);
}

static void midi2_to_camd(void *context, const uint8_t *bytes, size_t length)
{
    struct runtime *rt = context;
    size_t i;
    printf("MIDIHub: MIDI 2.0 bytes=");
    for (i = 0; i < length && i < 16; ++i)
        printf("%02x", (unsigned int)bytes[i]);
    puts(i < length ? "..." : "");
    if (bytes[0] == 0xf0)
        netmidi_camd_deliver_sysex(&rt->camd, bytes, length);
    else
        netmidi_camd_deliver(&rt->camd, bytes, length);
}

static void midi2_sink(void *context, int index, const uint32_t *words,
                       size_t count)
{
    struct runtime *rt = context;
    size_t i = 0;
    while (i < count) {
        unsigned n = ump_words(words[i]);
        if (i + n > count)
            break;
        ump_to_midi1(&rt->midi2_in[index], words + i, n, midi2_to_camd, rt);
        i += n;
    }
}

/* A session ended, was reset or lost data for good: nothing here knows
 * which notes its peer left sounding, so every channel is told to stop. */
static void midi2_all_notes_off(struct runtime *rt)
{
    uint8_t message[3] = {0xb0, 123, 0};
    unsigned int channel;
    for (channel = 0; channel < 16; ++channel) {
        message[0] = (uint8_t)(0xb0 | channel);
        netmidi_camd_deliver(&rt->camd, message, sizeof(message));
    }
}

static void midi2_receive(struct runtime *rt)
{
    static uint8_t in[2048], out[NETMIDI2_MAX_PACKET];
    struct sockaddr_in source;
    socklen_t source_length = sizeof(source);
    struct netmidi2_writer packet;
    int index;
    int count = netmidi_network_midi2_receive(&rt->network, in, sizeof(in),
                                              &source, &source_length);
    if (count <= 0 || source_length < sizeof(source) ||
        source.sin_family != AF_INET)
        return;
    netmidi2_writer_init(&packet, out, sizeof(out));
    if (netmidi2_table_receive(&rt->midi2, midi2_key(&source), in,
                               (size_t)count, (uint32_t)(now_ticks() / 10),
                               &packet, &index) == 0)
        midi2_send(rt, midi2_key(&source), &packet);
}

static void midi2_periodic(struct runtime *rt, uint64_t now)
{
    static uint8_t out[NETMIDI2_MAX_PACKET];
    struct netmidi2_writer packet;
    int index;

    for (index = 0; index < NETMIDI2_SESSIONS_MAX; ++index) {
        struct netmidi2_session *session =
            netmidi2_table_session(&rt->midi2, index);
        uint64_t peer;
        if (!session) {
            if (rt->midi2_known[index]) {
                puts("MIDIHub: MIDI 2.0 session closed");
                midi2_all_notes_off(rt);
                rt->midi2_known[index] = 0;
            }
            continue;
        }
        peer = session->peer;
        if (session->state == NETMIDI2_ESTABLISHED &&
            !rt->midi2_known[index]) {
            printf("MIDIHub: MIDI 2.0 session established peer=\"%s\"\n",
                   session->peer_name);
            rt->midi2_known[index] = 1;
            rt->midi2_gaps[index] = session->gaps;
            rt->midi2_resets[index] = session->resets;
            ump_to_midi1_init(&rt->midi2_in[index], 0);
            ump_to_midi1_buffer(&rt->midi2_in[index], rt->midi2_sysex[index],
                                sizeof(rt->midi2_sysex[index]));
        }
        if (rt->midi2_known[index] &&
            (session->gaps != rt->midi2_gaps[index] ||
             session->resets != rt->midi2_resets[index])) {
            puts("MIDIHub: MIDI 2.0 data lost or session reset");
            rt->midi2_gaps[index] = session->gaps;
            rt->midi2_resets[index] = session->resets;
            ump_to_midi1_buffer(&rt->midi2_in[index], rt->midi2_sysex[index],
                                sizeof(rt->midi2_sysex[index]));
            midi2_all_notes_off(rt);
        }
        netmidi2_writer_init(&packet, out, sizeof(out));
        if (netmidi2_table_tick(&rt->midi2, index, (uint32_t)(now / 10),
                                &packet))
            midi2_send(rt, peer, &packet);
    }
}

struct midi2_outgoing {
    struct runtime *rt;
    uint32_t words[NETMIDI2_MAX_UMP_WORDS];
    size_t count;
};

/* One UMP Data command to every established session. */
static void midi2_flush(struct midi2_outgoing *outgoing)
{
    static uint8_t out[NETMIDI2_MAX_PACKET];
    struct runtime *rt = outgoing->rt;
    struct netmidi2_writer packet;
    int index;

    if (!outgoing->count)
        return;
    for (index = 0; index < NETMIDI2_SESSIONS_MAX; ++index) {
        struct netmidi2_session *session =
            netmidi2_table_session(&rt->midi2, index);
        if (!session || session->state != NETMIDI2_ESTABLISHED)
            continue;
        netmidi2_writer_init(&packet, out, sizeof(out));
        if (netmidi2_send_ump(session, outgoing->words, outgoing->count,
                              (uint32_t)(now_ticks() / 10), &packet) == 0)
            midi2_send(rt, session->peer, &packet);
    }
    outgoing->count = 0;
}

/* A UMP is never split between two commands (section 7.1). */
static void midi2_collect(void *context, const uint32_t *words,
                          unsigned count)
{
    struct midi2_outgoing *outgoing = context;
    if (outgoing->count + count > NETMIDI2_MAX_UMP_WORDS)
        midi2_flush(outgoing);
    memcpy(outgoing->words + outgoing->count, words,
           count * sizeof(*words));
    outgoing->count += count;
}

static void midi2_send_midi(struct runtime *rt, const uint8_t *message,
                            size_t length)
{
    static struct midi2_outgoing outgoing;
    struct ump_from_midi1 from;
    size_t i;

    outgoing.rt = rt;
    outgoing.count = 0;
    ump_from_midi1_init(&from, 0);
    for (i = 0; i < length; ++i)
        ump_from_midi1_byte(&from, message[i], midi2_collect, &outgoing);
    midi2_flush(&outgoing);
}

/* One message from CAMD to one peer. */
static int send_midi_to(struct runtime *rt, int index, const uint8_t *message,
                        size_t length)
{
    struct stream *stream = &rt->stream[index];
    struct sockaddr_in data;
    uint8_t wire[1100];
    uint8_t segment[1002];
    size_t wire_length;
    size_t data_length;
    size_t offset;
    size_t chunk;
    size_t segment_length;
    uint8_t head;
    uint8_t tail;
    uint32_t timestamp = (uint32_t)now_ticks();
    uint16_t first_sequence;

    peer_address(&rt->peers.peer[index], 1, &data);
    if (message[0] != 0xf0) {
        if (rtpmidi_sender_packet(&stream->sender, stream->sequence, timestamp,
                                  rt->peers.local_ssrc, message, length,
                                  wire, sizeof(wire), &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &data) != 0)
            return -1;
        ++stream->sequence;
        stream->last_rtp_send = now_ticks();
        return 0;
    }
    first_sequence = stream->sequence;
    if (length <= 1002) {
        if (rtpmidi_encode_list(stream->sequence, timestamp,
                               rt->peers.local_ssrc, message, length,
                               wire, sizeof(wire), &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &data) != 0)
            return -1;
        ++stream->sequence;
        stream->last_rtp_send = now_ticks();
        rtpmidi_sender_record_sysex(&stream->sender, first_sequence, timestamp,
                                    message, length);
        return 0;
    }
    data_length = length - 2;
    offset = 0;
    while (offset < data_length) {
        chunk = data_length - offset;
        if (chunk > 1000)
            chunk = 1000;
        head = offset == 0 ? 0xf0 : 0xf7;
        tail = offset + chunk == data_length ? 0xf7 : 0xf0;
        segment[0] = head;
        memcpy(segment + 1, message + 1 + offset, chunk);
        segment[chunk + 1] = tail;
        segment_length = chunk + 2;
        if (rtpmidi_encode_list(stream->sequence, timestamp,
                               rt->peers.local_ssrc, segment,
                               segment_length, wire, sizeof(wire),
                               &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &data) != 0)
            return -1;
        ++stream->sequence;
        stream->last_rtp_send = now_ticks();
        offset += chunk;
    }
    rtpmidi_sender_record_sysex(&stream->sender, first_sequence, timestamp,
                                message, length);
    return 0;
}

/* What a CAMD program sends goes to every connected peer, each with its
 * own sequence numbers and its own journal. */
static int send_midi(void *context, const uint8_t *message, size_t length)
{
    struct runtime *rt = context;
    size_t i;
    int index;
    int result = -1;

    if (!message || !length)
        return -1;
    if (message[0] == 0xf0) {
        if (length < 2 || length > RTPMIDI_SYSEX_MAX ||
            message[length - 1] != 0xf7)
            return -1;
        for (i = 1; i + 1 < length; ++i)
            if (message[i] & 0x80)
                return -1;
    }
    for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index)
        if (rt->peers.peer[index].used &&
            rt->peers.peer[index].session.phase ==
                APPLEMIDI_SESSION_CONNECTED &&
            send_midi_to(rt, index, message, length) == 0)
            result = 0;
    if (rt->midi2_enabled) {
        midi2_send_midi(rt, message, length);
        result = 0;
    }
    return result;
}

static void send_guard(struct runtime *rt, int index, uint64_t now)
{
    struct stream *stream = &rt->stream[index];
    struct sockaddr_in data;
    uint8_t wire[1100];
    size_t wire_length;
    size_t journal_length;
    uint32_t timestamp = (uint32_t)now;
    if (rtpmidi_encode_list(stream->sequence, timestamp, rt->peers.local_ssrc,
                           NULL, 0, wire, sizeof(wire), &wire_length) != 0 ||
        rtpmidi_sender_journal(&stream->sender, stream->sequence, timestamp,
                          wire + wire_length, sizeof(wire) - wire_length,
                          &journal_length) != 0)
        return;
    wire[12] |= 0x40;
    wire_length += journal_length;
    peer_address(&rt->peers.peer[index], 1, &data);
    if (netmidi_network_send(&rt->network, 1, wire, wire_length,
                             &data) == 0) {
        ++stream->sequence;
        stream->last_rtp_send = now;
    }
}

static void send_note(struct runtime *rt, int index, uint8_t velocity)
{
    uint8_t message[3] = {0x90, 60, velocity};
    send_midi_to(rt, index, message, sizeof(message));
}

static void send_probe_sysex(struct runtime *rt, int index)
{
    static uint8_t large[2004];
    static const uint8_t small[] = {0xf0, 0x7d, 0x01, 0xf7};
    size_t i;
    large[0] = 0xf0;
    large[1] = 0x7d;
    for (i = 2; i + 1 < sizeof(large); ++i)
        large[i] = (uint8_t)(i & 0x7f);
    large[sizeof(large) - 1] = 0xf7;
    send_midi_to(rt, index, small, sizeof(small));
    send_midi_to(rt, index, large, sizeof(large));
}

/* Ends a peer from our side, with BY when a session had been reached. */
static void drop_peer(struct runtime *rt, int index, uint64_t now)
{
    struct applemidi_packet goodbye;
    struct sockaddr_in control;
    peer_address(&rt->peers.peer[index], 0, &control);
    stream_forget(rt, index, now);
    if (applemidi_peers_end(&rt->peers, index, &goodbye) == 0)
        send_apple_to(rt, 0, &control, &goodbye);
}

static void periodic_peer(struct runtime *rt, int index, uint64_t now)
{
    struct stream *stream = &rt->stream[index];
    struct applemidi_session *session = &rt->peers.peer[index].session;
    struct applemidi_packet packet;
    struct rtpmidi_queued_event event;
    int data_port;

    while (rtpmidi_queue_pop_due(&stream->queue, now, &event)) {
        if (event.sysex)
            rtpmidi_receiver_deliver_sysex(&stream->receiver, event.sysex,
                                           event.sysex_length);
        else
            rtpmidi_receiver_deliver(&stream->receiver, event.bytes,
                                     event.length);
        rtpmidi_queue_event_release(&event);
    }

    if (session->phase == APPLEMIDI_SESSION_INVITING_CONTROL ||
        session->phase == APPLEMIDI_SESSION_INVITING_DATA) {
        if (elapsed_ticks(now, stream->last_invite) >= 10000) {
            if (++stream->retries > 12) {
                puts("MIDIHub: invitation timed out");
                drop_peer(rt, index, now);
            } else if (applemidi_peers_retry(&rt->peers, index, &packet,
                                             &data_port) == 0) {
                send_apple(rt, index, data_port, &packet);
                stream->last_invite = now;
            }
        }
    } else if (session->phase == APPLEMIDI_SESSION_CONNECTED) {
        int probing = index == configured_index(rt);
        if (applemidi_clock_expired(&stream->clock, now)) {
            puts("MIDIHub: peer synchronization timed out");
            drop_peer(rt, index, now);
            return;
        }
        send_sync(rt, index, now);
        if (stream->sender.count &&
            elapsed_ticks(now, stream->last_rtp_send) >= 10000)
            send_guard(rt, index, now);
        if (!probing)
            return;
        if (rt->probe_sysex && stream->clock.synchronized &&
            rt->probe_state == 0) {
            send_probe_sysex(rt, index);
            rt->probe_state = 2;
        }
        if (rt->probe_note && stream->clock.synchronized &&
            rt->probe_state == 0) {
            send_note(rt, index, 100);
            rt->note_time = now;
            rt->probe_state = 1;
        } else if (rt->probe_state == 1 &&
                   elapsed_ticks(now, rt->note_time) >= 2500) {
            send_note(rt, index, 0);
            rt->probe_state = 2;
        }
    } else if (session->phase == APPLEMIDI_SESSION_WAITING_DATA &&
               elapsed_ticks(now, stream->last_activity) >= 120000) {
        puts("MIDIHub: data invitation timed out");
        drop_peer(rt, index, now);
    }
}

static void periodic(struct runtime *rt, uint64_t now)
{
    int index;

    /* The system's mDNS service may come up after us, or again. */
    {
        struct netmidi_discovered seen;
        int registered = netmidi_discovery_poll(&rt->discovery, now);
        if (registered > rt->discovered)
            puts("MIDIHub: advertised by the system mDNS service");
        if (registered >= 0)
            rt->discovered = registered;
        while (netmidi_discovery_next(&rt->discovery, &seen)) {
            int ours = strcmp(seen.name, rt->session_name) == 0;
            int wanted = rt->wanted[0] && strcmp(seen.name, rt->wanted) == 0;
            if (ours)
                continue;           /* our own registration, come back */
            if (seen.present)
                printf("MIDIHub: session \"%s\" found at %u.%u.%u.%u port %u\n",
                       seen.name, (unsigned int)(seen.address >> 24),
                       (unsigned int)((seen.address >> 16) & 255),
                       (unsigned int)((seen.address >> 8) & 255),
                       (unsigned int)(seen.address & 255),
                       (unsigned int)seen.port);
            else
                printf("MIDIHub: session \"%s\" is gone\n", seen.name);
            if (!wanted)
                continue;
            if (seen.present) {
                rt->configured = 1;
                rt->configured_address = seen.address;
                rt->configured_port = seen.port;
                if (!rt->wanted_present) {
                    rt->wanted_wait = 0;        /* newly there: at once */
                    rt->wanted_tried = 0;
                }
            }
            rt->wanted_present = seen.present;
        }
        /* The peer we want is advertised and we have no session with it:
           invite it, and again more slowly while it does not answer. */
        if (rt->wanted_present && configured_index(rt) < 0 &&
            (!rt->wanted_tried ||
             elapsed_ticks(now, rt->wanted_tried) >= rt->wanted_wait)) {
            printf("MIDIHub: inviting \"%s\"\n", rt->wanted);
            invite_configured(rt, now);
            rt->wanted_tried = now ? now : 1;
            rt->wanted_wait = rt->wanted_wait ? rt->wanted_wait * 2 : 50000;
            if (rt->wanted_wait > 600000)
                rt->wanted_wait = 600000;
        }
    }
    for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index)
        if (rt->peers.peer[index].used)
            periodic_peer(rt, index, now);
    if (rt->midi2_enabled)
        midi2_periodic(rt, now);
}

int main(int argc, char **argv)
{
    static struct runtime rt;
    struct in_addr peer_address_bytes;
    uint16_t local_port;
    uint16_t peer_port;
    uint16_t midi2_port = 0;
    const char *peer_name = NULL;
    struct netmidi_network_config config;
    const char *peer_ip;
    uint64_t now;
    uint64_t next_due;
    uint64_t wait_ticks;
    int index;
    int argi = 1;
    int remaining;
#ifdef __AROS__
    int config_result;
#endif
    unsigned int network_events;
    long wait_us;
    int result = 20;

    setvbuf(stdout, NULL, _IONBF, 0);

    netmidi_config_defaults(&config);
    if (argc > 1 && strcmp(argv[1], "--config") == 0) {
        if (argc < 3 || netmidi_config_load(argv[2], &config) != 0) {
            fputs("MIDIHub: cannot read configuration\n", stderr);
            return 20;
        }
        argi = 3;
    } else {
#ifdef __AROS__
        config_result = netmidi_config_load("ENV:MidiHub/Network", &config);
        if (config_result == 1)
            config_result = netmidi_config_load("ENVARC:MidiHub/Network", &config);
        if (config_result < 0) {
            fputs("MIDIHub: invalid network preferences\n", stderr);
            return 20;
        }
#endif
    }
    /* Network MIDI 2.0 is off unless a port is given for it. */
    if (argc - argi >= 2 && strcmp(argv[argi], "--midi2") == 0) {
        if (parse_port(argv[argi + 1], &midi2_port) != 0) {
            fputs("MIDIHub: --midi2 takes a UDP port\n", stderr);
            return 20;
        }
        argi += 2;
    }
    /* A peer to invite by the name its session is advertised under. */
    if (argc - argi >= 2 && strcmp(argv[argi], "--peer-name") == 0) {
        peer_name = argv[argi + 1];
        argi += 2;
    }
    remaining = argc - argi;
    local_port = config.local_port;
    peer_port = config.peer_port;
    peer_ip = config.peer_ip;
    if ((remaining != 0 && remaining != 1 && remaining != 3 &&
         remaining != 4) ||
        (remaining >= 1 && parse_port(argv[argi], &local_port) != 0) ||
        (remaining >= 3 &&
         parse_port(argv[argi + 2], &peer_port) != 0) ||
        (remaining == 4 && strcmp(argv[argi + 3], "--probe-note") != 0 &&
         strcmp(argv[argi + 3], "--probe-sysex") != 0)) {
        fputs("Usage: MIDIHub [--config file] [--midi2 port] [--peer-name session] [local-control-port [peer-ip peer-control-port [--probe-note|--probe-sysex]]]\n", stderr);
        return 20;
    }
    if (remaining >= 3)
        peer_ip = argv[argi + 1];
    memset(&rt, 0, sizeof(rt));
    netmidi_network_init(&rt.network);
    netmidi_discovery_init(&rt.discovery);
    snprintf(rt.session_name, sizeof(rt.session_name), "%s",
             config.session_name);
    rt.configured = *peer_ip != 0;
    rt.probe_note = remaining == 4 &&
                    strcmp(argv[argi + 3], "--probe-note") == 0;
    rt.probe_sysex = remaining == 4 &&
                     strcmp(argv[argi + 3], "--probe-sysex") == 0;

    if (netmidi_network_platform_open(&rt.network) != 0) {
        fputs("MIDIHub: bsdsocket.library unavailable\n", stderr);
        return 20;
    }
#ifndef __AROS__
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);
#endif
    if (rt.configured) {
#ifdef __AROS__
        peer_address_bytes.s_addr = inet_addr(peer_ip);
        if (peer_address_bytes.s_addr == INADDR_NONE) {
#else
        if (inet_pton(AF_INET, peer_ip, &peer_address_bytes) != 1) {
#endif
            fputs("MIDIHub: peer must be an IPv4 address\n", stderr);
            goto cleanup;
        }
        rt.configured_address = ntohl(peer_address_bytes.s_addr);
        rt.configured_port = peer_port;
    }
    if (netmidi_network_open_pair(&rt.network, local_port) != 0) {
        fputs("MIDIHub: cannot bind UDP port pair\n", stderr);
        goto cleanup;
    }
    if (midi2_port) {
        if (netmidi_network_open_midi2(&rt.network, midi2_port) != 0) {
            fputs("MIDIHub: cannot bind the Network MIDI 2.0 port\n", stderr);
            goto cleanup;
        }
        netmidi2_table_init(&rt.midi2, config.session_name, "aros-midihub",
                            midi2_sink, &rt);
        rt.midi2_enabled = 1;
    }
    if (netmidi_camd_open(&rt.camd) != 0) {
        fputs("MIDIHub: camd.library unavailable\n", stderr);
        goto cleanup;
    }
    now = now_ticks();
    if (applemidi_peers_init(&rt.peers, random32(now),
                        (const uint8_t *)config.session_name,
                        strlen(config.session_name)) != 0)
        goto cleanup;
    /* An AppleMIDI session is found under its control port. */
    netmidi_discovery_add(&rt.discovery, rt.session_name, "_apple-midi._udp",
                          local_port);
    netmidi_discovery_browse(&rt.discovery, "_apple-midi._udp");
    if (peer_name)
        snprintf(rt.wanted, sizeof(rt.wanted), "%s", peer_name);
    if (netmidi_discovery_poll(&rt.discovery, now) < 0)
        puts("MIDIHub: no system mDNS service; peers have to be given by address");
    if (rt.configured && invite_configured(&rt, now) != 0)
        goto cleanup;
    printf("MIDIHub: listening on UDP %u/%u\n", (unsigned int)local_port,
           (unsigned int)local_port + 1);
    fflush(stdout);

    for (;;) {
#ifndef __AROS__
        if (stopping)
            break;
#endif
        wait_us = 100000;
        for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index) {
            if (!rt.peers.peer[index].used ||
                !rtpmidi_queue_next_due(&rt.stream[index].queue, &next_due))
                continue;
            now = now_ticks();
            wait_ticks = next_due > now ? next_due - now : 0;
            if (wait_ticks < 1000 && (long)(wait_ticks * 100) < wait_us)
                wait_us = (long)(wait_ticks * 100);
        }
        netmidi_network_wait(&rt.network, -1, wait_us,
                             netmidi_camd_signal_bit(&rt.camd),
                             &network_events);
        if (network_events & NETMIDI_NETWORK_EVENT_STOP)
            break;
        if (network_events & NETMIDI_NETWORK_EVENT_CONTROL)
            receive_packet(&rt, 0);
        if (network_events & NETMIDI_NETWORK_EVENT_DATA)
            receive_packet(&rt, 1);
        if (network_events & NETMIDI_NETWORK_EVENT_MIDI2)
            midi2_receive(&rt);
        periodic(&rt, now_ticks());
        netmidi_camd_poll(&rt.camd, send_midi, &rt);
        /* asked to invite one peer by its address: with that session
           over, so are we. One wanted by name is waited for instead. */
        if (rt.configured && !rt.wanted[0] && configured_index(&rt) < 0)
            break;
    }
    result = 0;

cleanup:
    netmidi_discovery_close(&rt.discovery);
    for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index)
        if (rt.peers.peer[index].used)
            drop_peer(&rt, index, now_ticks());
    for (index = 0; rt.midi2_enabled && index < NETMIDI2_SESSIONS_MAX; ++index) {
        static uint8_t out[NETMIDI2_MAX_PACKET];
        struct netmidi2_writer packet;
        struct netmidi2_session *session =
            netmidi2_table_session(&rt.midi2, index);
        netmidi2_writer_init(&packet, out, sizeof(out));
        if (session && netmidi2_bye(session, NETMIDI2_BYE_USER_TERMINATED,
                                    (uint32_t)(now_ticks() / 10),
                                    &packet) == 0)
            midi2_send(&rt, session->peer, &packet);
    }
    netmidi_camd_close(&rt.camd);
    netmidi_network_close(&rt.network);
    return result;
}
