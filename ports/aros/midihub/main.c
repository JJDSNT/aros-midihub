#ifndef __AROS__
#define _DEFAULT_SOURCE 1
#endif

#include <rtpmidi/applemidi.h>
#include <midihub/config.h>
#include <midihub/mdns.h>
#include <rtpmidi/clock.h>
#include <rtpmidi/peers.h>
#include <rtpmidi/receiver.h>
#include <rtpmidi/rtpmidi.h>
#include <rtpmidi/session.h>
#include <rtpmidi/sender.h>
#include <rtpmidi/timing.h>
#include "camd.h"
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
    int mdns;
    int configured;             /* a peer was named to invite */
    uint32_t configured_address;
    uint16_t configured_port;
    int probe_note;
    int probe_sysex;
    int probe_state;
    uint64_t note_time;
    uint8_t mdns_ip[4];
    char mdns_host[64];
    char mdns_session[64];
    uint16_t local_port;
    uint64_t last_mdns_announce;
    unsigned int mdns_announcements;
    struct applemidi_peers peers;
    struct stream stream[APPLEMIDI_PEERS_MAX];
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

static int open_mdns(uint8_t address[4])
{
    struct sockaddr_in local;
    struct sockaddr_in multicast;
    struct ip_mreq membership;
    socklen_t size = sizeof(local);
    unsigned char ttl = 255;
    int reuse = 1;
    int probe = -1;
    int fd = -1;

    probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe < 0) return -1;
    memset(&multicast, 0, sizeof(multicast));
    multicast.sin_family = AF_INET;
    multicast.sin_port = htons(NETMIDI_MDNS_PORT);
    multicast.sin_addr.s_addr = htonl(0xe00000fbUL);
    if (connect(probe, (const struct sockaddr *)&multicast,
                sizeof(multicast)) < 0 ||
        getsockname(probe, (struct sockaddr *)&local, &size) < 0) {
        netmidi_network_close_socket(probe);
        return -1;
    }
    memcpy(address, &local.sin_addr.s_addr, 4);
    netmidi_network_close_socket(probe);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse,
                   sizeof(reuse)) < 0)
        goto fail;
#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void *)&reuse,
                   sizeof(reuse)) < 0)
        goto fail;
#endif
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(NETMIDI_MDNS_PORT);
    if (bind(fd, (const struct sockaddr *)&local, sizeof(local)) < 0)
        goto fail;
    memset(&membership, 0, sizeof(membership));
    membership.imr_multiaddr.s_addr = multicast.sin_addr.s_addr;
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (const void *)&membership, sizeof(membership)) < 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
                   (const void *)&ttl, sizeof(ttl)) < 0)
        goto fail;
    return fd;
fail:
    netmidi_network_close_socket(fd);
    return -1;
}

static void send_mdns(struct runtime *rt, const struct sockaddr_in *to,
                      uint32_t ttl, uint16_t query_id)
{
    struct sockaddr_in multicast;
    uint8_t bytes[512];
    size_t length;
    if (rt->mdns < 0 ||
        netmidi_mdns_build(rt->mdns_session, rt->mdns_host, rt->mdns_ip,
                      rt->local_port, ttl, query_id,
                      bytes, sizeof(bytes), &length) != 0)
        return;
    if (!to) {
        memset(&multicast, 0, sizeof(multicast));
        multicast.sin_family = AF_INET;
        multicast.sin_addr.s_addr = htonl(0xe00000fbUL);
        multicast.sin_port = htons(NETMIDI_MDNS_PORT);
        to = &multicast;
    }
    sendto(rt->mdns, bytes, (int)length, 0,
           (const struct sockaddr *)to, sizeof(*to));
}

static void receive_mdns(struct runtime *rt)
{
    struct sockaddr_in source;
    socklen_t source_length = sizeof(source);
    uint8_t packet[1500];
    int unicast = 0;
    int count = recvfrom(rt->mdns, packet, sizeof(packet), 0,
                         (struct sockaddr *)&source, &source_length);
    if (count <= 0 || source_length < sizeof(source) ||
        source.sin_family != AF_INET ||
        netmidi_mdns_query(packet, (size_t)count, rt->mdns_session,
                      rt->mdns_host, &unicast) != 1)
        return;
    if (ntohs(source.sin_port) != NETMIDI_MDNS_PORT) unicast = 1;
    send_mdns(rt, unicast ? &source : NULL, 120,
              unicast ? (uint16_t)((packet[0] << 8) | packet[1]) : 0);
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

    if (rt->mdns >= 0 &&
        (rt->mdns_announcements == 0 ||
         elapsed_ticks(now, rt->last_mdns_announce) >=
             (rt->mdns_announcements < 2 ? 10000u : 600000u))) {
        send_mdns(rt, NULL, 120, 0);
        rt->last_mdns_announce = now;
        if (rt->mdns_announcements < 2) ++rt->mdns_announcements;
    }
    for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index)
        if (rt->peers.peer[index].used)
            periodic_peer(rt, index, now);
}

int main(int argc, char **argv)
{
    static struct runtime rt;
    struct in_addr peer_address_bytes;
    uint16_t local_port;
    uint16_t peer_port;
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
        fputs("Usage: MIDIHub [--config file] [local-control-port [peer-ip peer-control-port [--probe-note|--probe-sysex]]]\n", stderr);
        return 20;
    }
    if (remaining >= 3)
        peer_ip = argv[argi + 1];
    memset(&rt, 0, sizeof(rt));
    netmidi_network_init(&rt.network);
    rt.mdns = -1;
    rt.local_port = local_port;
    strcpy(rt.mdns_session, config.session_name);
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
    if (netmidi_camd_open(&rt.camd) != 0) {
        fputs("MIDIHub: camd.library unavailable\n", stderr);
        goto cleanup;
    }
    now = now_ticks();
    if (applemidi_peers_init(&rt.peers, random32(now),
                        (const uint8_t *)config.session_name,
                        strlen(config.session_name)) != 0)
        goto cleanup;
    snprintf(rt.mdns_host, sizeof(rt.mdns_host), "midihub-%04x-%08lx",
             (unsigned int)local_port,
             (unsigned long)rt.peers.local_ssrc);
    rt.mdns = open_mdns(rt.mdns_ip);
    if (rt.mdns < 0)
        puts("MIDIHub: Bonjour discovery unavailable");
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
        netmidi_network_wait(&rt.network, rt.mdns, wait_us,
                             netmidi_camd_signal_bit(&rt.camd),
                             &network_events);
        if (network_events & NETMIDI_NETWORK_EVENT_STOP)
            break;
        if (network_events & NETMIDI_NETWORK_EVENT_CONTROL)
            receive_packet(&rt, 0);
        if (network_events & NETMIDI_NETWORK_EVENT_DATA)
            receive_packet(&rt, 1);
        if (network_events & NETMIDI_NETWORK_EVENT_AUXILIARY)
            receive_mdns(&rt);
        periodic(&rt, now_ticks());
        netmidi_camd_poll(&rt.camd, send_midi, &rt);
        /* asked to invite one peer: with that session over, so are we */
        if (rt.configured && configured_index(&rt) < 0)
            break;
    }
    result = 0;

cleanup:
    if (rt.mdns >= 0) {
        send_mdns(&rt, NULL, 0, 0);
        netmidi_network_close_socket(rt.mdns);
    }
    for (index = 0; index < APPLEMIDI_PEERS_MAX; ++index)
        if (rt.peers.peer[index].used)
            drop_peer(&rt, index, now_ticks());
    netmidi_camd_close(&rt.camd);
    netmidi_network_close(&rt.network);
    return result;
}
