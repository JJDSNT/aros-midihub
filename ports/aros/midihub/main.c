#include <midihub/applemidi.h>
#include <midihub/config.h>
#include <midihub/rtpmidi.h>
#include <midihub/session.h>
#include <midihub/timing.h>
#include "camd_bridge.h"

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
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
struct Library *SocketBase;
#define mh_close CloseSocket
#else
#include <signal.h>
#include <sys/select.h>
#include <unistd.h>
#define mh_close close
static volatile sig_atomic_t stopping;
static void stop_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}
#endif

struct runtime {
    int control;
    int data;
    struct sockaddr_in peer_control;
    struct sockaddr_in peer_data;
    int have_peer;
    int initiating;
    int probe_note;
    int probe_sysex;
    int probe_state;
    int sync_ready;
    int64_t peer_to_local;
    uint64_t sync_t1;
    uint64_t sync_t2;
    int have_received_sequence;
    uint16_t sequence;
    uint32_t received_sequence;
    uint64_t last_invite;
    uint64_t last_sync;
    uint64_t note_time;
    uint64_t last_activity;
    unsigned int retries;
    struct mh_session session;
    struct mh_sysex_assembler sysex;
    struct mh_event_queue queue;
    struct mh_camd_bridge camd;
    int camd_opened;
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

static int open_udp(uint16_t port)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        mh_close(fd);
        return -1;
    }
    return fd;
}

static int same_endpoint(const struct sockaddr_in *a,
                         const struct sockaddr_in *b)
{
    return a->sin_family == AF_INET && b->sin_family == AF_INET &&
           a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

static int send_apple(struct runtime *rt, int data_port,
                      const struct mh_apple_packet *packet)
{
    uint8_t bytes[128];
    size_t length;
    const struct sockaddr_in *address = data_port ? &rt->peer_data :
                                                  &rt->peer_control;
    int fd = data_port ? rt->data : rt->control;
    if (mh_apple_encode(packet, bytes, sizeof(bytes), &length) != 0)
        return -1;
    return sendto(fd, bytes, (int)length, 0,
                  (const struct sockaddr *)address, sizeof(*address)) ==
           (int)length ? 0 : -1;
}

static void send_sync(struct runtime *rt, uint64_t now)
{
    struct mh_apple_packet sync;
    memset(&sync, 0, sizeof(sync));
    sync.command = MH_APPLE_CK;
    sync.ssrc = rt->session.local_ssrc;
    sync.timestamps[0] = now;
    if (send_apple(rt, 1, &sync) == 0)
        rt->last_sync = now;
}

static int send_feedback(struct runtime *rt, uint16_t sequence)
{
    struct mh_apple_packet feedback;
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
    feedback.command = MH_APPLE_RS;
    feedback.ssrc = rt->session.local_ssrc;
    feedback.feedback_sequence = rt->received_sequence;
    send_apple(rt, 0, &feedback);
    return fresh;
}

static void receive_sync(struct runtime *rt,
                         const struct mh_apple_packet *incoming,
                         uint64_t now)
{
    struct mh_apple_packet response;
    if (rt->session.phase != MH_SESSION_CONNECTED ||
        incoming->ssrc != rt->session.peer_ssrc)
        return;
    if (incoming->sync_count == 0 && !rt->initiating) {
        response = *incoming;
        response.ssrc = rt->session.local_ssrc;
        response.sync_count = 1;
        response.timestamps[1] = now;
        rt->sync_t1 = incoming->timestamps[0];
        rt->sync_t2 = now;
        send_apple(rt, 1, &response);
    } else if (incoming->sync_count == 1 && rt->initiating &&
               incoming->timestamps[0] == rt->last_sync) {
        response = *incoming;
        response.ssrc = rt->session.local_ssrc;
        response.sync_count = 2;
        response.timestamps[2] = now;
        send_apple(rt, 1, &response);
        if (mh_clock_offset(response.timestamps, 1,
                            &rt->peer_to_local) == 0) {
            rt->sync_ready = 1;
            puts("MIDIHub: clock exchange completed");
        }
    } else if (incoming->sync_count == 2 && !rt->initiating &&
               incoming->timestamps[0] == rt->sync_t1 &&
               incoming->timestamps[1] == rt->sync_t2 && rt->sync_t2 &&
               mh_clock_offset(incoming->timestamps, 0,
                               &rt->peer_to_local) == 0) {
        rt->sync_ready = 1;
        puts("MIDIHub: clock exchange completed");
    }
}

static void receive_packet(struct runtime *rt, int data_port)
{
    struct sockaddr_in source;
    socklen_t source_length = sizeof(source);
    uint8_t bytes[1500];
    int count = recvfrom(data_port ? rt->data : rt->control, bytes,
                         sizeof(bytes), 0, (struct sockaddr *)&source,
                         &source_length);
    struct mh_apple_packet packet;
    struct mh_apple_packet response;
    struct mh_rtp_packet midi;
    struct mh_rtp_reader reader;
    struct mh_midi_event event;
    const uint8_t *sysex_message;
    size_t sysex_length;
    int response_port;
    int action;
    int sysex_result;
    uint64_t now;
    size_t i;

    if (count <= 0 || source_length < sizeof(source) ||
        source.sin_family != AF_INET)
        return;
    if (rt->have_peer) {
        if (!same_endpoint(&source, data_port ? &rt->peer_data :
                                               &rt->peer_control))
            return;
    } else if (data_port || mh_apple_decode(bytes, (size_t)count,
                                            &packet) != 0 ||
               packet.command != MH_APPLE_IN ||
               rt->session.phase != MH_SESSION_IDLE ||
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
    if (mh_apple_decode(bytes, (size_t)count, &packet) == 0) {
        if (packet.command == MH_APPLE_CK) {
            if (data_port)
                receive_sync(rt, &packet, now);
            return;
        }
        if (packet.command == MH_APPLE_RS)
            return; /* No outgoing recovery journal to prune yet. */
        action = mh_session_receive(&rt->session, data_port, &packet,
                                    &response, &response_port);
        if (action == 1)
            send_apple(rt, response_port, &response);
        if (packet.command == MH_APPLE_OK && action == 1 &&
            response.command == MH_APPLE_IN) {
            rt->last_invite = now;
            rt->retries = 0;
        }
        if (rt->session.phase == MH_SESSION_CONNECTED &&
            packet.command == MH_APPLE_OK && data_port && rt->initiating) {
            puts("MIDIHub: session connected");
            send_sync(rt, now);
        } else if (rt->session.phase == MH_SESSION_CONNECTED &&
                   packet.command == MH_APPLE_IN && data_port) {
            puts("MIDIHub: session connected");
        } else if (packet.command == MH_APPLE_BY &&
                   rt->session.phase == MH_SESSION_IDLE) {
            puts("MIDIHub: peer disconnected");
            rt->have_peer = 0;
            rt->sync_ready = 0;
            rt->peer_to_local = 0;
            rt->sync_t1 = rt->sync_t2 = 0;
            rt->have_received_sequence = 0;
            mh_sysex_reset(&rt->sysex);
            mh_queue_reset(&rt->queue);
        }
        return;
    }
    if (!data_port || rt->session.phase != MH_SESSION_CONNECTED ||
        mh_rtp_decode(bytes, (size_t)count, &midi) != 0 ||
        midi.ssrc != rt->session.peer_ssrc)
        return;
    mh_rtp_reader_init(&reader, &midi);
    while ((action = mh_rtp_reader_next(&reader, &event)) == 1) {}
    if (action < 0) {
        puts("MIDIHub: unsupported or malformed MIDI command");
        return;
    }
    if (!send_feedback(rt, midi.sequence))
        return; /* A duplicate or older packet must not replay MIDI events. */
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
    mh_rtp_reader_init(&reader, &midi);
    while ((action = mh_rtp_reader_next(&reader, &event)) == 1) {
        if (event.sysex) {
            sysex_result = mh_sysex_feed(&rt->sysex, &event, midi.sequence,
                                          &sysex_message, &sysex_length);
            if (sysex_result == 1) {
                printf("MIDIHub: SysEx complete bytes=%lu\n",
                       (unsigned long)sysex_length);
                mh_camd_bridge_deliver_sysex(&rt->camd, sysex_message,
                                             sysex_length);
            } else if (sysex_result < 0)
                puts("MIDIHub: SysEx segment discarded");
        } else {
            uint64_t due;
            now = now_ticks();
            if (rt->sync_ready &&
                mh_clock_due(event.timestamp, now, rt->peer_to_local,
                             &due) == 0 && due > now) {
                if (due - now > 100000 ||
                    mh_queue_push(&rt->queue, due, event.bytes,
                                  event.length) != 0)
                    puts("MIDIHub: future MIDI event discarded");
                continue;
            }
            mh_camd_bridge_deliver(&rt->camd, event.bytes, event.length);
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

    if (rt->session.phase != MH_SESSION_CONNECTED || !message || !length)
        return -1;
    if (message[0] != 0xf0) {
        if (mh_rtp_encode_short(rt->sequence, (uint32_t)now_ticks(),
                                rt->session.local_ssrc, message, length,
                                wire, sizeof(wire), &wire_length) != 0)
            return -1;
        if (sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        ++rt->sequence;
        return 0;
    }
    if (length < 2 || length > MH_SYSEX_MAX || message[length - 1] != 0xf7)
        return -1;
    for (i = 1; i + 1 < length; ++i)
        if (message[i] & 0x80)
            return -1;
    if (length <= 1002) {
        if (mh_rtp_encode_list(rt->sequence, (uint32_t)now_ticks(),
                               rt->session.local_ssrc, message, length,
                               wire, sizeof(wire), &wire_length) != 0 ||
            sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        ++rt->sequence;
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
        if (mh_rtp_encode_list(rt->sequence, (uint32_t)now_ticks(),
                               rt->session.local_ssrc, segment,
                               segment_length, wire, sizeof(wire),
                               &wire_length) != 0 ||
            sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        ++rt->sequence;
        offset += chunk;
    }
    return 0;
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
    struct mh_apple_packet packet;
    struct mh_queued_event event;
    int data_port;

    while (mh_queue_pop_due(&rt->queue, now, &event))
        mh_camd_bridge_deliver(&rt->camd, event.bytes, event.length);

    if (rt->session.phase == MH_SESSION_INVITING_CONTROL ||
        rt->session.phase == MH_SESSION_INVITING_DATA) {
        if (elapsed_ticks(now, rt->last_invite) >= 10000) {
            if (++rt->retries > 12) {
                puts("MIDIHub: invitation timed out");
                rt->session.phase = MH_SESSION_IDLE;
                rt->have_peer = 0;
            } else if (mh_session_retry(&rt->session, &packet,
                                        &data_port) == 0) {
                send_apple(rt, data_port, &packet);
                rt->last_invite = now;
            }
        }
    } else if (rt->session.phase == MH_SESSION_CONNECTED) {
        if (rt->initiating &&
            elapsed_ticks(now, rt->last_sync) >= 500000)
            send_sync(rt, now);
        if (rt->probe_sysex && rt->sync_ready && rt->probe_state == 0) {
            send_probe_sysex(rt);
            rt->probe_state = 2;
        }
        if (rt->probe_note && rt->sync_ready && rt->probe_state == 0) {
            send_note(rt, 100);
            rt->note_time = now;
            rt->probe_state = 1;
        } else if (rt->probe_state == 1 &&
                   elapsed_ticks(now, rt->note_time) >= 2500) {
            send_note(rt, 0);
            rt->probe_state = 2;
        }
    } else if (rt->session.phase == MH_SESSION_WAITING_DATA &&
               elapsed_ticks(now, rt->last_activity) >= 120000) {
        puts("MIDIHub: data invitation timed out");
        rt->session.phase = MH_SESSION_IDLE;
        rt->have_peer = 0;
        rt->sync_ready = 0;
        rt->peer_to_local = 0;
        rt->sync_t1 = rt->sync_t2 = 0;
        rt->have_received_sequence = 0;
        mh_sysex_reset(&rt->sysex);
        mh_queue_reset(&rt->queue);
    }
}

int main(int argc, char **argv)
{
    static struct runtime rt;
    struct sockaddr_in peer;
    struct mh_apple_packet invite;
    struct mh_apple_packet goodbye;
    struct timeval timeout;
    fd_set read_set;
    uint16_t local_port;
    uint16_t peer_port;
    struct mh_network_config config;
    const char *peer_ip;
    uint64_t now;
    uint64_t next_due;
    uint64_t wait_ticks;
    unsigned int seed;
    int argi = 1;
    int remaining;
#ifdef __AROS__
    int config_result;
#endif
    int ready;
    int result = 20;
#ifdef __AROS__
    ULONG signal_mask;
#endif

    setvbuf(stdout, NULL, _IONBF, 0);

    mh_config_defaults(&config);
    if (argc > 1 && strcmp(argv[1], "--config") == 0) {
        if (argc < 3 || mh_config_load(argv[2], &config) != 0) {
            fputs("MIDIHub: cannot read configuration\n", stderr);
            return 20;
        }
        argi = 3;
    } else {
#ifdef __AROS__
        config_result = mh_config_load("ENV:MidiHub/Network", &config);
        if (config_result == 1)
            config_result = mh_config_load("ENVARC:MidiHub/Network", &config);
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
    rt.control = rt.data = -1;
    rt.initiating = *peer_ip != 0;
    rt.probe_note = remaining == 4 &&
                    strcmp(argv[argi + 3], "--probe-note") == 0;
    rt.probe_sysex = remaining == 4 &&
                     strcmp(argv[argi + 3], "--probe-sysex") == 0;

#ifdef __AROS__
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        fputs("MIDIHub: bsdsocket.library unavailable\n", stderr);
        return 20;
    }
#else
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
    rt.control = open_udp(local_port);
    rt.data = open_udp((uint16_t)(local_port + 1));
    if (rt.control < 0 || rt.data < 0) {
        fputs("MIDIHub: cannot bind UDP port pair\n", stderr);
        goto cleanup;
    }
    if (mh_camd_bridge_open(&rt.camd) != 0) {
        fputs("MIDIHub: camd.library unavailable\n", stderr);
        goto cleanup;
    }
    rt.camd_opened = 1;
    now = now_ticks();
    seed = (unsigned int)(now ^ (now >> 32) ^ (unsigned int)local_port);
    srand(seed);
    if (mh_session_init(&rt.session,
                        ((uint32_t)rand() << 16) ^ (uint32_t)rand(),
                        (const uint8_t *)config.session_name,
                        strlen(config.session_name)) != 0)
        goto cleanup;
    rt.sequence = (uint16_t)rand();
    if (rt.initiating) {
        if (mh_session_invite(&rt.session,
                              ((uint32_t)rand() << 16) ^ (uint32_t)rand(),
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
        FD_ZERO(&read_set);
        FD_SET(rt.control, &read_set);
        FD_SET(rt.data, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        if (mh_queue_next_due(&rt.queue, &next_due)) {
            now = now_ticks();
            wait_ticks = next_due > now ? next_due - now : 0;
            if (wait_ticks < 1000)
                timeout.tv_usec = (long)(wait_ticks * 100);
        }
#ifdef __AROS__
        signal_mask = SIGBREAKF_CTRL_C | (1UL << rt.camd.signal_bit);
        ready = WaitSelect((rt.control > rt.data ? rt.control : rt.data) + 1,
                           &read_set, NULL, NULL, &timeout, &signal_mask);
        if (signal_mask & SIGBREAKF_CTRL_C)
            break;
#else
        ready = select((rt.control > rt.data ? rt.control : rt.data) + 1,
                       &read_set, NULL, NULL, &timeout);
#endif
        if (ready > 0) {
            if (FD_ISSET(rt.control, &read_set))
                receive_packet(&rt, 0);
            if (FD_ISSET(rt.data, &read_set))
                receive_packet(&rt, 1);
        }
        periodic(&rt, now_ticks());
        mh_camd_bridge_poll(&rt.camd, send_midi, &rt);
        if (rt.initiating && rt.session.phase == MH_SESSION_IDLE)
            break;
    }
    if (rt.session.phase != MH_SESSION_IDLE &&
        mh_session_end(&rt.session, &goodbye) == 0)
        send_apple(&rt, 0, &goodbye);
    result = 0;

cleanup:
    if (rt.camd_opened)
        mh_camd_bridge_close(&rt.camd);
    if (rt.data >= 0)
        mh_close(rt.data);
    if (rt.control >= 0)
        mh_close(rt.control);
#ifdef __AROS__
    CloseLibrary(SocketBase);
#endif
    return result;
}
