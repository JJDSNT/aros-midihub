#ifndef __AROS__
#define _DEFAULT_SOURCE 1
#endif

#include <rtpmidi/applemidi.h>
#include <midihub/config.h>
#include <midihub/mdns.h>
#include <rtpmidi/clock.h>
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

struct runtime {
    struct netmidi_network_runtime network;
    int mdns;
    struct sockaddr_in peer_control;
    struct sockaddr_in peer_data;
    int have_peer;
    int initiating;
    int probe_note;
    int probe_sysex;
    int probe_state;
    struct applemidi_clock clock;
    int have_received_sequence;
    uint16_t sequence;
    uint32_t received_sequence;
    uint64_t last_invite;
    uint64_t last_rtp_send;
    uint64_t note_time;
    uint64_t last_activity;
    unsigned int retries;
    uint8_t mdns_ip[4];
    char mdns_host[64];
    char mdns_session[64];
    uint16_t local_port;
    uint64_t last_mdns_announce;
    unsigned int mdns_announcements;
    struct applemidi_session session;
    struct rtpmidi_sysex_assembler sysex;
    struct rtpmidi_event_queue queue;
    struct rtpmidi_sender sender;
    struct rtpmidi_receiver receiver;
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

static int same_endpoint(const struct sockaddr_in *a,
                         const struct sockaddr_in *b)
{
    return a->sin_family == AF_INET && b->sin_family == AF_INET &&
           a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

static int send_apple(struct runtime *rt, int data_port,
                      const struct applemidi_packet *packet)
{
    uint8_t bytes[128];
    size_t length;
    const struct sockaddr_in *address = data_port ? &rt->peer_data :
                                                  &rt->peer_control;
    if (applemidi_encode(packet, bytes, sizeof(bytes), &length) != 0)
        return -1;
    return netmidi_network_send(&rt->network, data_port, bytes, length,
                                address);
}

static void send_sync(struct runtime *rt, uint64_t now)
{
    struct applemidi_packet sync;
    if (applemidi_clock_poll(&rt->clock, rt->session.local_ssrc, now, &sync))
        send_apple(rt, 1, &sync);
}

/* Everything kept for the stream of a session that is over. */
static void forget_stream(struct runtime *rt, uint64_t now)
{
    applemidi_clock_init(&rt->clock, rt->initiating, now);
    rt->have_received_sequence = 0;
    rtpmidi_sysex_reset(&rt->sysex);
    rtpmidi_queue_reset(&rt->queue);
    rtpmidi_receiver_release_notes(&rt->receiver);
    rtpmidi_receiver_reset_channels(&rt->receiver);
    rtpmidi_receiver_reset_system(&rt->receiver);
    rtpmidi_sender_reset(&rt->sender);
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

static int send_feedback(struct runtime *rt, uint16_t sequence)
{
    struct applemidi_packet feedback;
    int16_t advance;
    int fresh = 1;

    if (!rt->have_received_sequence) {
        rt->received_sequence = sequence;
        rt->have_received_sequence = 1;
    } else {
        advance = (int16_t)(sequence - (uint16_t)rt->received_sequence);
        if (advance <= 0)
            fresh = 0;
        else
            rt->received_sequence += (uint16_t)advance;
    }
    memset(&feedback, 0, sizeof(feedback));
    feedback.command = APPLEMIDI_RS;
    feedback.ssrc = rt->session.local_ssrc;
    feedback.feedback_sequence = rt->received_sequence;
    send_apple(rt, 0, &feedback);
    return fresh;
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

static void receive_sync(struct runtime *rt,
                         const struct applemidi_packet *incoming,
                         uint64_t now)
{
    struct applemidi_packet response;
    unsigned int completed = rt->clock.completed;
    if (rt->session.phase != APPLEMIDI_SESSION_CONNECTED ||
        incoming->ssrc != rt->session.peer_ssrc)
        return;
    if (applemidi_clock_receive(&rt->clock, rt->session.local_ssrc, incoming,
                                now, &response))
        send_apple(rt, 1, &response);
    if (rt->clock.completed != completed)
        puts("MIDIHub: clock exchange completed");
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
    struct rtpmidi_packet midi;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_system_state system_state;
    struct rtpmidi_journal_sysex sysex_state;
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;
    const uint8_t *sysex_message;
    size_t sysex_length;
    int response_port;
    int action;
    unsigned int restarts;
    unsigned int collisions;
    int sysex_result;
    int system_decoded = 0;
    int16_t sequence_advance = 0;
    uint16_t previous_sequence = 0;
    uint64_t now;
    size_t i;

    if (count <= 0 || source_length < sizeof(source) ||
        source.sin_family != AF_INET)
        return;
    if (rt->have_peer) {
        if (!same_endpoint(&source, data_port ? &rt->peer_data :
                                               &rt->peer_control))
            return;
    } else if (data_port || applemidi_decode(bytes, (size_t)count,
                                            &packet) != 0 ||
               packet.command != APPLEMIDI_IN ||
               rt->session.phase != APPLEMIDI_SESSION_IDLE ||
               ntohs(source.sin_port) == 65535) {
        return;
    } else {
        rt->peer_control = source;
        rt->peer_data = source;
        rt->peer_data.sin_port = htons((uint16_t)(ntohs(source.sin_port) + 1));
        rt->have_peer = 1;
    }

    now = now_ticks();
    rt->last_activity = now;
    if (applemidi_decode(bytes, (size_t)count, &packet) == 0) {
        if (packet.command == APPLEMIDI_CK) {
            if (data_port)
                receive_sync(rt, &packet, now);
            return;
        }
        if (packet.command == APPLEMIDI_RS) {
            if (!data_port && rt->session.phase == APPLEMIDI_SESSION_CONNECTED &&
                packet.ssrc == rt->session.peer_ssrc)
                rtpmidi_sender_ack(&rt->sender, packet.feedback_sequence);
            return;
        }
        restarts = rt->session.restarts;
        collisions = rt->session.ssrc_collisions;
        action = applemidi_session_receive(&rt->session, data_port, &packet,
                                    &response, &response_port);
        if (action == 1)
            send_apple(rt, response_port, &response);
        if (rt->session.restarts != restarts) {
            puts("MIDIHub: peer began again");
            forget_stream(rt, now);
        }
        if (rt->session.ssrc_collisions != collisions) {
            puts("MIDIHub: peer has our SSRC; taking another");
            if (rt->session.phase == APPLEMIDI_SESSION_IDLE) {
                applemidi_session_renew(&rt->session, random32(now));
                if (rt->initiating &&
                    applemidi_session_invite(&rt->session, random32(now),
                                             &response) == 0) {
                    send_apple(rt, 0, &response);
                    rt->last_invite = now;
                    rt->retries = 0;
                } else if (!rt->initiating) {
                    rt->have_peer = 0;
                }
            }
            return;
        }
        if (packet.command == APPLEMIDI_OK && action == 1 &&
            response.command == APPLEMIDI_IN) {
            rt->last_invite = now;
            rt->retries = 0;
        }
        if (rt->session.phase == APPLEMIDI_SESSION_CONNECTED &&
            packet.command == APPLEMIDI_OK && data_port && rt->initiating) {
            puts("MIDIHub: session connected");
            applemidi_clock_init(&rt->clock, 1, now);
            send_sync(rt, now);
        } else if (rt->session.phase == APPLEMIDI_SESSION_CONNECTED &&
                   packet.command == APPLEMIDI_IN && data_port) {
            puts("MIDIHub: session connected");
            applemidi_clock_init(&rt->clock, 0, now);
        } else if (packet.command == APPLEMIDI_BY &&
                   rt->session.phase == APPLEMIDI_SESSION_IDLE) {
            puts("MIDIHub: peer disconnected");
            rt->have_peer = 0;
            forget_stream(rt, now);
        }
        return;
    }
    if (!data_port || rt->session.phase != APPLEMIDI_SESSION_CONNECTED ||
        rtpmidi_decode(bytes, (size_t)count, &midi) != 0 ||
        midi.ssrc != rt->session.peer_ssrc)
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
    if (rt->have_received_sequence) {
        previous_sequence = (uint16_t)rt->received_sequence;
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
    if (!send_feedback(rt, midi.sequence))
        return; /* A duplicate or older packet must not replay MIDI events. */
    if (sequence_advance > 1 && midi.journal &&
        rtpmidi_journal_covers_gap(previous_sequence, midi.sequence,
                              journal.checkpoint)) {
        struct rtpmidi_receiver_clock clock;
        clock.synchronized = rt->clock.synchronized;
        clock.peer_to_local = rt->clock.peer_to_local;
        clock.now = now_ticks();
        rtpmidi_receiver_recover(&rt->receiver, &journal,
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
            sysex_result = rtpmidi_sysex_feed(&rt->sysex, &event, midi.sequence,
                                          &sysex_message, &sysex_length);
            if (sysex_result == 1) {
                uint64_t due;
                printf("MIDIHub: SysEx complete bytes=%lu\n",
                       (unsigned long)sysex_length);
                now = now_ticks();
                if (rt->clock.synchronized &&
                    rtpmidi_clock_due(rt->sysex.timestamp, now,
                                 rt->clock.peer_to_local, &due) == 0 && due > now) {
                    if (due - now > 100000 ||
                        rtpmidi_queue_push_sysex(&rt->queue, due, sysex_message,
                                            sysex_length) != 0)
                        puts("MIDIHub: future SysEx event discarded");
                    continue;
                }
                rtpmidi_receiver_deliver_sysex(&rt->receiver, sysex_message, sysex_length);
            } else if (sysex_result < 0)
                puts("MIDIHub: SysEx segment discarded");
        } else {
            uint64_t due;
            now = now_ticks();
            if (rt->clock.synchronized &&
                rtpmidi_clock_due(event.timestamp, now, rt->clock.peer_to_local,
                             &due) == 0 && due > now) {
                if (due - now > 100000 ||
                    rtpmidi_queue_push(&rt->queue, due, event.bytes,
                                  event.length) != 0)
                    puts("MIDIHub: future MIDI event discarded");
                continue;
            }
            rtpmidi_receiver_deliver(&rt->receiver, event.bytes, event.length);
        }
    }
    if (action < 0)
        puts("MIDIHub: unsupported or malformed MIDI command");
}

static int send_midi(void *context, const uint8_t *message, size_t length)
{
    struct runtime *rt = context;
    uint8_t wire[1100];
    uint8_t segment[1002];
    size_t wire_length;
    size_t data_length;
    size_t offset;
    size_t chunk;
    size_t segment_length;
    uint8_t head;
    uint8_t tail;
    size_t i;
    uint32_t timestamp;
    uint16_t first_sequence;

    if (rt->session.phase != APPLEMIDI_SESSION_CONNECTED || !message || !length)
        return -1;
    if (message[0] != 0xf0) {
        timestamp = (uint32_t)now_ticks();
        if (rtpmidi_sender_packet(&rt->sender, rt->sequence, timestamp,
                                  rt->session.local_ssrc, message, length,
                                  wire, sizeof(wire), &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &rt->peer_data) != 0)
            return -1;
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        return 0;
    }
    if (length < 2 || length > RTPMIDI_SYSEX_MAX || message[length - 1] != 0xf7)
        return -1;
    for (i = 1; i + 1 < length; ++i)
        if (message[i] & 0x80)
            return -1;
    timestamp = (uint32_t)now_ticks();
    first_sequence = rt->sequence;
    if (length <= 1002) {
        if (rtpmidi_encode_list(rt->sequence, timestamp,
                               rt->session.local_ssrc, message, length,
                               wire, sizeof(wire), &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &rt->peer_data) != 0)
            return -1;
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        rtpmidi_sender_record_sysex(&rt->sender, first_sequence, timestamp,
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
        if (rtpmidi_encode_list(rt->sequence, timestamp,
                               rt->session.local_ssrc, segment,
                               segment_length, wire, sizeof(wire),
                               &wire_length) != 0 ||
            netmidi_network_send(&rt->network, 1, wire, wire_length,
                                 &rt->peer_data) != 0)
            return -1;
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        offset += chunk;
    }
    rtpmidi_sender_record_sysex(&rt->sender, first_sequence, timestamp,
                                message, length);
    return 0;
}

static void send_guard(struct runtime *rt, uint64_t now)
{
    uint8_t wire[1100];
    size_t wire_length;
    size_t journal_length;
    uint32_t timestamp = (uint32_t)now;
    if (rtpmidi_encode_list(rt->sequence, timestamp, rt->session.local_ssrc,
                           NULL, 0, wire, sizeof(wire), &wire_length) != 0 ||
        rtpmidi_sender_journal(&rt->sender, rt->sequence, timestamp,
                          wire + wire_length, sizeof(wire) - wire_length,
                          &journal_length) != 0)
        return;
    wire[12] |= 0x40;
    wire_length += journal_length;
    if (netmidi_network_send(&rt->network, 1, wire, wire_length,
                             &rt->peer_data) == 0) {
        ++rt->sequence;
        rt->last_rtp_send = now;
    }
}

static void send_note(struct runtime *rt, uint8_t velocity)
{
    uint8_t message[3] = {0x90, 60, velocity};
    send_midi(rt, message, sizeof(message));
}

static void send_probe_sysex(struct runtime *rt)
{
    static uint8_t large[2004];
    static const uint8_t small[] = {0xf0, 0x7d, 0x01, 0xf7};
    size_t i;
    large[0] = 0xf0;
    large[1] = 0x7d;
    for (i = 2; i + 1 < sizeof(large); ++i)
        large[i] = (uint8_t)(i & 0x7f);
    large[sizeof(large) - 1] = 0xf7;
    send_midi(rt, small, sizeof(small));
    send_midi(rt, large, sizeof(large));
}

static void periodic(struct runtime *rt, uint64_t now)
{
    struct applemidi_packet packet;
    struct rtpmidi_queued_event event;
    int data_port;

    while (rtpmidi_queue_pop_due(&rt->queue, now, &event)) {
        if (event.sysex)
            rtpmidi_receiver_deliver_sysex(&rt->receiver, event.sysex, event.sysex_length);
        else
            rtpmidi_receiver_deliver(&rt->receiver, event.bytes, event.length);
        rtpmidi_queue_event_release(&event);
    }

    if (rt->mdns >= 0 &&
        (rt->mdns_announcements == 0 ||
         elapsed_ticks(now, rt->last_mdns_announce) >=
             (rt->mdns_announcements < 2 ? 10000u : 600000u))) {
        send_mdns(rt, NULL, 120, 0);
        rt->last_mdns_announce = now;
        if (rt->mdns_announcements < 2) ++rt->mdns_announcements;
    }

    if (rt->session.phase == APPLEMIDI_SESSION_INVITING_CONTROL ||
        rt->session.phase == APPLEMIDI_SESSION_INVITING_DATA) {
        if (elapsed_ticks(now, rt->last_invite) >= 10000) {
            if (++rt->retries > 12) {
                puts("MIDIHub: invitation timed out");
                rt->session.phase = APPLEMIDI_SESSION_IDLE;
                rt->have_peer = 0;
            } else if (applemidi_session_retry(&rt->session, &packet,
                                        &data_port) == 0) {
                send_apple(rt, data_port, &packet);
                rt->last_invite = now;
            }
        }
    } else if (rt->session.phase == APPLEMIDI_SESSION_CONNECTED) {
        if (applemidi_clock_expired(&rt->clock, now)) {
            struct applemidi_packet goodbye;
            puts("MIDIHub: peer synchronization timed out");
            if (applemidi_session_end(&rt->session, &goodbye) == 0)
                send_apple(rt, 0, &goodbye);
            rt->have_peer = 0;
            forget_stream(rt, now);
            return;
        }
        send_sync(rt, now);
        if (rt->sender.count &&
            elapsed_ticks(now, rt->last_rtp_send) >= 10000)
            send_guard(rt, now);
        if (rt->probe_sysex && rt->clock.synchronized && rt->probe_state == 0) {
            send_probe_sysex(rt);
            rt->probe_state = 2;
        }
        if (rt->probe_note && rt->clock.synchronized && rt->probe_state == 0) {
            send_note(rt, 100);
            rt->note_time = now;
            rt->probe_state = 1;
        } else if (rt->probe_state == 1 &&
                   elapsed_ticks(now, rt->note_time) >= 2500) {
            send_note(rt, 0);
            rt->probe_state = 2;
        }
    } else if (rt->session.phase == APPLEMIDI_SESSION_WAITING_DATA &&
               elapsed_ticks(now, rt->last_activity) >= 120000) {
        puts("MIDIHub: data invitation timed out");
        rt->session.phase = APPLEMIDI_SESSION_IDLE;
        rt->have_peer = 0;
        applemidi_clock_init(&rt->clock, rt->initiating, now);
        rt->have_received_sequence = 0;
        rtpmidi_receiver_forget_notes(&rt->receiver);
        rtpmidi_sysex_reset(&rt->sysex);
        rtpmidi_queue_reset(&rt->queue);
    }
}

int main(int argc, char **argv)
{
    static struct runtime rt;
    struct sockaddr_in peer;
    struct applemidi_packet invite;
    struct applemidi_packet goodbye;
    uint16_t local_port;
    uint16_t peer_port;
    struct netmidi_network_config config;
    const char *peer_ip;
    uint64_t now;
    uint64_t next_due;
    uint64_t wait_ticks;
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
    rtpmidi_receiver_init(&rt.receiver, &receiver_output, &rt, &rt.queue);
    netmidi_network_init(&rt.network);
    rt.mdns = -1;
    rt.local_port = local_port;
    strcpy(rt.mdns_session, config.session_name);
    rt.initiating = *peer_ip != 0;
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
    if (rt.initiating) {
        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_port = htons(peer_port);
#ifdef __AROS__
        peer.sin_addr.s_addr = inet_addr(peer_ip);
        if (peer.sin_addr.s_addr == INADDR_NONE) {
#else
        if (inet_pton(AF_INET, peer_ip, &peer.sin_addr) != 1) {
#endif
            fputs("MIDIHub: peer must be an IPv4 address\n", stderr);
            goto cleanup;
        }
        rt.peer_control = peer;
        peer.sin_port = htons((uint16_t)(peer_port + 1));
        rt.peer_data = peer;
        rt.have_peer = 1;
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
    if (applemidi_session_init(&rt.session, random32(now),
                        (const uint8_t *)config.session_name,
                        strlen(config.session_name)) != 0)
        goto cleanup;
    rt.sequence = (uint16_t)random32(now);
    snprintf(rt.mdns_host, sizeof(rt.mdns_host), "midihub-%04x-%08lx",
             (unsigned int)local_port,
             (unsigned long)rt.session.local_ssrc);
    rt.mdns = open_mdns(rt.mdns_ip);
    if (rt.mdns < 0)
        puts("MIDIHub: Bonjour discovery unavailable");
    if (rt.initiating) {
        if (applemidi_session_invite(&rt.session, random32(now),
                              &invite) != 0 || send_apple(&rt, 0, &invite) != 0)
            goto cleanup;
        rt.last_invite = now;
    }
    printf("MIDIHub: listening on UDP %u/%u\n", (unsigned int)local_port,
           (unsigned int)local_port + 1);
    fflush(stdout);

    for (;;) {
#ifndef __AROS__
        if (stopping)
            break;
#endif
        wait_us = 100000;
        if (rtpmidi_queue_next_due(&rt.queue, &next_due)) {
            now = now_ticks();
            wait_ticks = next_due > now ? next_due - now : 0;
            if (wait_ticks < 1000)
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
        if (rt.initiating && rt.session.phase == APPLEMIDI_SESSION_IDLE)
            break;
    }
    if (rt.session.phase != APPLEMIDI_SESSION_IDLE &&
        applemidi_session_end(&rt.session, &goodbye) == 0)
        send_apple(&rt, 0, &goodbye);
    result = 0;

cleanup:
    if (rt.mdns >= 0) {
        send_mdns(&rt, NULL, 0, 0);
        netmidi_network_close_socket(rt.mdns);
    }
    if (netmidi_camd_is_open(&rt.camd)) {
        rtpmidi_queue_reset(&rt.queue);
        rtpmidi_receiver_release_notes(&rt.receiver);
    }
    netmidi_camd_close(&rt.camd);
    netmidi_network_close(&rt.network);
    return result;
}
