#ifndef __AROS__
#define _DEFAULT_SOURCE 1
#endif

#include <midihub/applemidi.h>
#include <midihub/config.h>
#include <midihub/mdns.h>
#include <midihub/rtpmidi.h>
#include <midihub/session.h>
#include <midihub/sender.h>
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
    int mdns;
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
    uint64_t last_peer_sync;
    uint64_t last_rtp_send;
    unsigned int sync_exchanges;
    int sync_outstanding;
    uint64_t note_time;
    uint64_t last_activity;
    unsigned int retries;
    uint8_t mdns_ip[4];
    char mdns_host[64];
    char mdns_session[64];
    uint16_t local_port;
    uint64_t last_mdns_announce;
    unsigned int mdns_announcements;
    struct mh_session session;
    struct mh_sysex_assembler sysex;
    struct mh_event_queue queue;
    struct mh_sender sender;
    uint8_t active_notes[16][128];
    uint8_t program[16];
    uint8_t program_known[16];
    uint8_t bank_msb[16];
    uint8_t bank_lsb[16];
    uint8_t bank_msb_known[16];
    uint8_t bank_lsb_known[16];
    uint16_t pitch[16];
    uint8_t controllers[16][128];
    uint8_t controller_known[16][128];
    uint8_t sustain_toggles[16];
    uint8_t controller_count[16][128];
    uint8_t channel_pressure[16];
    uint8_t channel_pressure_known[16];
    uint8_t poly_pressure[16][128];
    uint8_t poly_pressure_known[16][128];
    uint8_t reset_count;
    uint8_t tune_request_count;
    uint8_t active_sense_count;
    uint8_t song_select;
    uint8_t song_select_known;
    uint8_t sequencer_known;
    uint8_t sequencer_running;
    uint8_t sequencer_downbeat;
    uint8_t sequencer_start_at_zero;
    uint32_t sequencer_clock;
    uint8_t mtc_full_frame[4];
    uint8_t mtc_full_known;
    struct mh_camd_bridge camd;
    int camd_opened;
};

static void reset_channel_state(struct runtime *rt);

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
    multicast.sin_port = htons(MH_MDNS_PORT);
    multicast.sin_addr.s_addr = htonl(0xe00000fbUL);
    if (connect(probe, (const struct sockaddr *)&multicast,
                sizeof(multicast)) < 0 ||
        getsockname(probe, (struct sockaddr *)&local, &size) < 0) {
        mh_close(probe);
        return -1;
    }
    memcpy(address, &local.sin_addr.s_addr, 4);
    mh_close(probe);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse,
                   sizeof(reuse)) < 0)
        goto fail;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(MH_MDNS_PORT);
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
    mh_close(fd);
    return -1;
}

static void send_mdns(struct runtime *rt, const struct sockaddr_in *to,
                      uint32_t ttl, uint16_t query_id)
{
    struct sockaddr_in multicast;
    uint8_t bytes[512];
    size_t length;
    if (rt->mdns < 0 ||
        mh_mdns_build(rt->mdns_session, rt->mdns_host, rt->mdns_ip,
                      rt->local_port, ttl, query_id,
                      bytes, sizeof(bytes), &length) != 0)
        return;
    if (!to) {
        memset(&multicast, 0, sizeof(multicast));
        multicast.sin_family = AF_INET;
        multicast.sin_addr.s_addr = htonl(0xe00000fbUL);
        multicast.sin_port = htons(MH_MDNS_PORT);
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
        mh_mdns_query(packet, (size_t)count, rt->mdns_session,
                      rt->mdns_host, &unicast) != 1)
        return;
    if (ntohs(source.sin_port) != MH_MDNS_PORT) unicast = 1;
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
    if (send_apple(rt, 1, &sync) == 0) {
        rt->last_sync = now;
        rt->sync_outstanding = 1;
    }
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

static void deliver_short(struct runtime *rt, const uint8_t *bytes,
                          size_t length)
{
    uint8_t status = bytes[0];
    unsigned int channel = status & 0x0f;
    if (status == 0xff && length == 1) {
        rt->reset_count = (uint8_t)((rt->reset_count + 1) & 0x7f);
        rt->tune_request_count = 0;
        rt->active_sense_count = 0;
        rt->song_select_known = 0;
        rt->sequencer_known = 0;
        rt->sequencer_running = 0;
        rt->sequencer_downbeat = 0;
        rt->sequencer_start_at_zero = 0;
        rt->sequencer_clock = 0;
        rt->mtc_full_known = 0;
        memset(rt->active_notes, 0, sizeof(rt->active_notes));
        reset_channel_state(rt);
    } else if (status == 0xf6 && length == 1) {
        rt->tune_request_count =
            (uint8_t)((rt->tune_request_count + 1) & 0x7f);
    } else if (status == 0xfe && length == 1) {
        rt->active_sense_count =
            (uint8_t)((rt->active_sense_count + 1) & 0x7f);
    } else if (status == 0xf3 && length == 2) {
        rt->song_select = bytes[1];
        rt->song_select_known = 1;
    } else if (status == 0xf2 && length == 3) {
        rt->sequencer_known = 1;
        rt->sequencer_downbeat = 0;
        rt->sequencer_start_at_zero = 0;
        rt->sequencer_clock =
            ((((uint32_t)bytes[2] << 7) | bytes[1]) * 6) & 0x7ffff;
    } else if (status == 0xfa && length == 1) {
        rt->sequencer_known = 1;
        rt->sequencer_running = 1;
        rt->sequencer_downbeat = 0;
        rt->sequencer_start_at_zero = 1;
        rt->sequencer_clock = 0;
    } else if (status == 0xfb && length == 1) {
        rt->sequencer_known = 1;
        rt->sequencer_running = 1;
        rt->sequencer_start_at_zero = 0;
    } else if (status == 0xfc && length == 1) {
        rt->sequencer_known = 1;
        rt->sequencer_running = 0;
        rt->sequencer_start_at_zero = 0;
    } else if (status == 0xf8 && length == 1) {
        rt->sequencer_known = 1;
        rt->sequencer_start_at_zero = 0;
        if (rt->sequencer_running) {
            if (!rt->sequencer_downbeat)
                rt->sequencer_downbeat = 1;
            else
                rt->sequencer_clock = (rt->sequencer_clock + 1) & 0x7ffff;
        }
    }
    if (length == 3 && status >= 0x80 && status <= 0x9f) {
        rt->active_notes[channel][bytes[1]] =
            (status & 0xf0) == 0x90 ? bytes[2] : 0;
    } else if (length == 3 && (status & 0xf0) == 0xb0 &&
               (bytes[1] == 120 || bytes[1] >= 123)) {
        memset(rt->active_notes[channel], 0,
               sizeof(rt->active_notes[channel]));
    }
    if (length == 2 && (status & 0xf0) == 0xc0) {
        rt->program[channel] = bytes[1];
        rt->program_known[channel] = 1;
    } else if (length == 3 && (status & 0xf0) == 0xb0) {
        rt->controller_count[channel][bytes[1]] =
            (uint8_t)((rt->controller_count[channel][bytes[1]] + 1) & 0x3f);
        if (bytes[1] == 64 &&
            (rt->controllers[channel][64] >= 64) != (bytes[2] >= 64))
            rt->sustain_toggles[channel] =
                (uint8_t)((rt->sustain_toggles[channel] + 1) & 0x3f);
        rt->controllers[channel][bytes[1]] = bytes[2];
        rt->controller_known[channel][bytes[1]] = 1;
        if (bytes[1] == 0) {
            rt->bank_msb[channel] = bytes[2];
            rt->bank_msb_known[channel] = 1;
        } else if (bytes[1] == 32) {
            rt->bank_lsb[channel] = bytes[2];
            rt->bank_lsb_known[channel] = 1;
        } else if (bytes[1] == 121) {
            rt->pitch[channel] = 0x2000;
            rt->channel_pressure[channel] = 0;
            rt->channel_pressure_known[channel] = 1;
            memset(rt->poly_pressure[channel], 0,
                   sizeof(rt->poly_pressure[channel]));
            memset(rt->poly_pressure_known[channel], 1,
                   sizeof(rt->poly_pressure_known[channel]));
            if (rt->controllers[channel][64] >= 64) {
                rt->sustain_toggles[channel] =
                    (uint8_t)((rt->sustain_toggles[channel] + 1) & 0x3f);
                rt->controllers[channel][64] = 0;
                rt->controller_known[channel][64] = 1;
            }
        }
    } else if (length == 2 && (status & 0xf0) == 0xd0) {
        rt->channel_pressure[channel] = bytes[1];
        rt->channel_pressure_known[channel] = 1;
    } else if (length == 3 && (status & 0xf0) == 0xa0) {
        rt->poly_pressure[channel][bytes[1]] = bytes[2];
        rt->poly_pressure_known[channel][bytes[1]] = 1;
    } else if (length == 3 && (status & 0xf0) == 0xe0) {
        rt->pitch[channel] = (uint16_t)(bytes[1] | (bytes[2] << 7));
    }
    mh_camd_bridge_deliver(&rt->camd, bytes, length);
}

static void deliver_sysex(struct runtime *rt, const uint8_t *message,
                          size_t length)
{
    size_t i;
    if (length == 10 && message[0] == 0xf0 && message[1] == 0x7f &&
        message[3] == 0x01 && message[4] == 0x01 && message[9] == 0xf7) {
        for (i = 1; i < 9 && !(message[i] & 0x80); ++i) {}
        if (i == 9) {
            memcpy(rt->mtc_full_frame, message + 5, 4);
            rt->mtc_full_known = 1;
        }
    }
    mh_camd_bridge_deliver_sysex(&rt->camd, message, length);
}

static void reset_channel_state(struct runtime *rt)
{
    unsigned int channel;
    memset(rt->program_known, 0, sizeof(rt->program_known));
    memset(rt->bank_msb_known, 0, sizeof(rt->bank_msb_known));
    memset(rt->bank_lsb_known, 0, sizeof(rt->bank_lsb_known));
    memset(rt->controller_known, 0, sizeof(rt->controller_known));
    memset(rt->controllers, 0, sizeof(rt->controllers));
    memset(rt->sustain_toggles, 0, sizeof(rt->sustain_toggles));
    memset(rt->controller_count, 0, sizeof(rt->controller_count));
    memset(rt->channel_pressure_known, 0,
           sizeof(rt->channel_pressure_known));
    memset(rt->poly_pressure_known, 0, sizeof(rt->poly_pressure_known));
    for (channel = 0; channel < 16; ++channel)
        rt->pitch[channel] = 0x2000;
}

static void release_active_notes(struct runtime *rt)
{
    uint8_t message[3];
    unsigned int channel;
    unsigned int note;
    message[2] = 0;
    for (channel = 0; channel < 16; ++channel) {
        message[0] = (uint8_t)(0x80 | channel);
        for (note = 0; note < 128; ++note) {
            if (!rt->active_notes[channel][note])
                continue;
            message[1] = (uint8_t)note;
            deliver_short(rt, message, sizeof(message));
        }
    }
}

static void recover_channel_state(struct runtime *rt,
                                   const struct mh_journal *journal,
                                   int single_loss)
{
    struct mh_journal_channel_state state;
    uint8_t message[3];
    size_t i;
    unsigned int channel;
    int bank_changed;
    uint16_t pitch;

    if (single_loss && journal->single_packet_safe)
        return;
    for (i = 0; i < journal->channel_count; ++i) {
        const struct mh_journal_channel *entry = &journal->channels[i];
        if (single_loss && entry->single_packet_safe)
            continue;
        if (mh_journal_decode_channel_state(entry, &state) <= 0)
            continue;
        channel = entry->number;
        bank_changed = 0;
        if (state.has_program &&
            (!single_loss || !state.program_single_packet_safe)) {
            message[0] = (uint8_t)(0xb0 | channel);
            if (state.has_bank) {
                if (!rt->bank_msb_known[channel] ||
                    rt->bank_msb[channel] != state.bank_msb) {
                    message[1] = 0;
                    message[2] = state.bank_msb;
                    deliver_short(rt, message, 3);
                    bank_changed = 1;
                }
                if (!rt->bank_lsb_known[channel] ||
                    rt->bank_lsb[channel] != state.bank_lsb) {
                    message[1] = 32;
                    message[2] = state.bank_lsb;
                    deliver_short(rt, message, 3);
                    bank_changed = 1;
                }
            }
            if (bank_changed || !rt->program_known[channel] ||
                rt->program[channel] != state.program) {
                message[0] = (uint8_t)(0xc0 | channel);
                message[1] = state.program;
                deliver_short(rt, message, 2);
                printf("MIDIHub: recovered Program Change channel=%u program=%u\n",
                       channel, (unsigned int)state.program);
            }
        }
        pitch = (uint16_t)(state.pitch_lsb | (state.pitch_msb << 7));
        if (state.has_pitch &&
            (!single_loss || !state.pitch_single_packet_safe) &&
            rt->pitch[channel] != pitch) {
            message[0] = (uint8_t)(0xe0 | channel);
            message[1] = state.pitch_lsb;
            message[2] = state.pitch_msb;
            deliver_short(rt, message, 3);
            printf("MIDIHub: recovered Pitch Bend channel=%u value=%u\n",
                   channel, (unsigned int)pitch);
        }
    }
}

static void recover_controls(struct runtime *rt,
                              const struct mh_journal *journal,
                              int single_loss)
{
    struct mh_journal_controls controls;
    uint8_t message[3];
    size_t i;
    size_t j;
    unsigned int channel;
    if (single_loss && journal->single_packet_safe)
        return;
    for (i = 0; i < journal->channel_count; ++i) {
        const struct mh_journal_channel *entry = &journal->channels[i];
        if (single_loss && entry->single_packet_safe)
            continue;
        if (mh_journal_decode_controls(entry, &controls) <= 0)
            continue;
        channel = entry->number;
        message[0] = (uint8_t)(0xb0 | channel);
        for (j = 0; j < controls.count; ++j) {
            const struct mh_journal_control_log *log = &controls.logs[j];
            if (single_loss && log->single_packet_safe)
                continue;
            if (log->alternate) {
                if (log->number == 64 && !log->count_tool &&
                    rt->sustain_toggles[channel] != log->value) {
                    message[1] = 64;
                    if (rt->controllers[channel][64] >= 64) {
                        message[2] = 0;
                        deliver_short(rt, message, sizeof(message));
                    }
                    if (log->value & 1) {
                        message[2] = 127;
                        deliver_short(rt, message, sizeof(message));
                    }
                    rt->sustain_toggles[channel] = log->value;
                    printf("MIDIHub: recovered Sustain toggle channel=%u count=%u\n",
                           channel, (unsigned int)log->value);
                } else if (log->count_tool &&
                           (log->number == 120 || log->number == 123) &&
                           rt->controller_count[channel][log->number] !=
                           log->value) {
                    message[1] = log->number;
                    message[2] = 0;
                    deliver_short(rt, message, sizeof(message));
                    rt->controller_count[channel][log->number] = log->value;
                    printf("MIDIHub: recovered controller count channel=%u controller=%u count=%u\n",
                           channel, (unsigned int)log->number,
                           (unsigned int)log->value);
                }
                continue;
            }
            if (rt->controller_known[channel][log->number] &&
                rt->controllers[channel][log->number] == log->value)
                continue;
            message[1] = log->number;
            message[2] = log->value;
            deliver_short(rt, message, sizeof(message));
            printf("MIDIHub: recovered Control Change channel=%u controller=%u value=%u\n",
                   channel, (unsigned int)log->number,
                   (unsigned int)log->value);
        }
    }
}

static void recover_notes(struct runtime *rt,
                          const struct mh_journal *journal,
                          int single_loss, uint32_t timestamp)
{
    struct mh_journal_notes notes;
    uint8_t message[3];
    size_t i;
    size_t log_index;
    unsigned int octave;
    unsigned int bit;
    unsigned int note;
    size_t cancelled;
    int decoded;
    uint64_t due;
    uint64_t now;
    int timely;

    if (single_loss && journal->single_packet_safe)
        return;
    for (i = 0; i < journal->channel_count; ++i) {
        const struct mh_journal_channel *channel = &journal->channels[i];
        if (single_loss && channel->single_packet_safe)
            continue;
        decoded = mh_journal_decode_notes(channel, &notes);
        if (decoded <= 0)
            continue;
        message[0] = (uint8_t)(0x80 | channel->number);
        message[2] = 0;
        for (octave = 0; octave < 16 &&
                         (!single_loss || !notes.offbits_single_packet_safe);
             ++octave) {
            for (bit = 0; bit < 8; ++bit) {
                note = octave * 8 + bit;
                if (!(notes.offbits[octave] & (0x80u >> bit)))
                    continue;
                cancelled = mh_queue_cancel_note_on(&rt->queue,
                                                    channel->number,
                                                    (uint8_t)note);
                if (cancelled)
                    printf("MIDIHub: cancelled %lu future Note On event(s) channel=%u note=%u\n",
                           (unsigned long)cancelled,
                           (unsigned int)channel->number, note);
                if (!rt->active_notes[channel->number][note])
                    continue;
                message[1] = (uint8_t)note;
                deliver_short(rt, message, sizeof(message));
                printf("MIDIHub: recovered Note Off channel=%u note=%u\n",
                       (unsigned int)channel->number, note);
            }
        }
        now = now_ticks();
        timely = rt->sync_ready &&
                 mh_clock_due(timestamp, now, rt->peer_to_local, &due) == 0 &&
                 (due >= now || now - due <= 200);
        for (log_index = 0; log_index < notes.log_count; ++log_index) {
            const struct mh_journal_note_log *log = &notes.logs[log_index];
            if (single_loss && log->single_packet_safe)
                continue;
            if (rt->active_notes[channel->number][log->number] ==
                log->velocity ||
                mh_queue_has_note_on(&rt->queue, channel->number,
                                     log->number))
                continue;
            if (rt->active_notes[channel->number][log->number]) {
                message[0] = (uint8_t)(0x80 | channel->number);
                message[1] = log->number;
                message[2] = 0;
                deliver_short(rt, message, sizeof(message));
            }
            if (!log->simultaneous || !timely)
                continue;
            message[0] = (uint8_t)(0x90 | channel->number);
            message[1] = log->number;
            message[2] = log->velocity;
            if (due > now) {
                if (due - now > 100000 ||
                    mh_queue_push(&rt->queue, due, message,
                                  sizeof(message)) != 0)
                    continue;
            } else {
                deliver_short(rt, message, sizeof(message));
            }
            printf("MIDIHub: recovered Note On channel=%u note=%u velocity=%u\n",
                   (unsigned int)channel->number,
                   (unsigned int)log->number,
                   (unsigned int)log->velocity);
        }
    }
}

static void recover_aftertouch(struct runtime *rt,
                               const struct mh_journal *journal,
                               int single_loss)
{
    struct mh_journal_aftertouch aftertouch;
    uint8_t message[3];
    size_t i;
    size_t j;
    unsigned int channel;
    if (single_loss && journal->single_packet_safe) return;
    for (i = 0; i < journal->channel_count; ++i) {
        const struct mh_journal_channel *entry = &journal->channels[i];
        if (single_loss && entry->single_packet_safe) continue;
        if (mh_journal_decode_aftertouch(entry, &aftertouch) <= 0) continue;
        channel = entry->number;
        if (aftertouch.has_channel_pressure &&
            (!single_loss || !aftertouch.channel_single_packet_safe) &&
            (!rt->channel_pressure_known[channel] ||
             rt->channel_pressure[channel] != aftertouch.channel_pressure)) {
            message[0] = (uint8_t)(0xd0 | channel);
            message[1] = aftertouch.channel_pressure;
            deliver_short(rt, message, 2);
            printf("MIDIHub: recovered Channel Aftertouch channel=%u pressure=%u\n",
                   channel, (unsigned int)message[1]);
        }
        for (j = 0; j < aftertouch.poly_count; ++j) {
            const struct mh_journal_poly_pressure *log = &aftertouch.poly[j];
            if ((single_loss && log->single_packet_safe) ||
                log->reset_notes || !rt->active_notes[channel][log->number] ||
                (rt->poly_pressure_known[channel][log->number] &&
                 rt->poly_pressure[channel][log->number] == log->pressure))
                continue;
            message[0] = (uint8_t)(0xa0 | channel);
            message[1] = log->number;
            message[2] = log->pressure;
            deliver_short(rt, message, 3);
            printf("MIDIHub: recovered Poly Aftertouch channel=%u note=%u pressure=%u\n",
                   channel, (unsigned int)log->number,
                   (unsigned int)log->pressure);
        }
    }
}

static void recover_system(struct runtime *rt,
                           const struct mh_journal_system_state *state,
                           int single_loss)
{
    uint8_t message[3];
    uint32_t clock;
    uint32_t remainder;
    unsigned int i;
    int start_at_zero;
    if (state->has_reset &&
        (!single_loss || !state->reset_single_packet_safe) &&
        rt->reset_count != state->reset_count) {
        message[0] = 0xff;
        deliver_short(rt, message, 1);
        rt->reset_count = state->reset_count;
        puts("MIDIHub: recovered System Reset");
    }
    if (state->has_tune_request &&
        (!single_loss || !state->tune_single_packet_safe) &&
        rt->tune_request_count != state->tune_count) {
        message[0] = 0xf6;
        deliver_short(rt, message, 1);
        rt->tune_request_count = state->tune_count;
        puts("MIDIHub: recovered Tune Request");
    }
    if (state->has_song_select &&
        (!single_loss || !state->song_single_packet_safe) &&
        (!rt->song_select_known || rt->song_select != state->song)) {
        message[0] = 0xf3;
        message[1] = state->song;
        deliver_short(rt, message, 2);
        printf("MIDIHub: recovered Song Select song=%u\n",
               (unsigned int)state->song);
    }
    if (state->has_active_sense &&
        (!single_loss || !state->active_sense_single_packet_safe) &&
        rt->active_sense_count != state->active_sense_count) {
        message[0] = 0xfe;
        deliver_short(rt, message, 1);
        rt->active_sense_count = state->active_sense_count;
        puts("MIDIHub: recovered Active Sense");
    }
    if (state->has_mtc && state->mtc_has_complete &&
        !state->mtc_complete_quarter_frame &&
        (!single_loss || !state->mtc_single_packet_safe) &&
        (!rt->mtc_full_known ||
         memcmp(rt->mtc_full_frame, state->mtc_complete, 4) != 0)) {
        uint8_t full_frame[10] = {
            0xf0, 0x7f, 0x7f, 0x01, 0x01, 0, 0, 0, 0, 0xf7
        };
        memcpy(full_frame + 5, state->mtc_complete, 4);
        deliver_sysex(rt, full_frame, sizeof(full_frame));
        printf("MIDIHub: recovered MTC Full Frame %02x:%02x:%02x:%02x\n",
               (unsigned int)state->mtc_complete[0],
               (unsigned int)state->mtc_complete[1],
               (unsigned int)state->mtc_complete[2],
               (unsigned int)state->mtc_complete[3]);
    }
    if (!state->has_sequencer ||
        (single_loss && state->sequencer_single_packet_safe) ||
        state->has_time_tools)
        return;
    clock = state->has_clock ? state->clock : 0;
    start_at_zero = state->sequencer_running &&
                    !state->downbeat_played && !state->has_clock;
    if (rt->sequencer_known &&
        rt->sequencer_running == state->sequencer_running &&
        rt->sequencer_downbeat == state->downbeat_played &&
        rt->sequencer_clock == clock &&
        rt->sequencer_start_at_zero == start_at_zero)
        return;

    /* C=0, N=1, D=0 is the RFC 6295 encoding for Start at zero. */
    if (start_at_zero) {
        message[0] = 0xfa;
        deliver_short(rt, message, 1);
        puts("MIDIHub: recovered sequencer Start");
        return;
    }

    /* Song Position Pointer plus at most six Clocks recreates every position
     * representable by MIDI's 14-bit SPP command. */
    if (clock > 98303) {
        if (!state->sequencer_running && rt->sequencer_running) {
            message[0] = 0xfc;
            deliver_short(rt, message, 1);
            puts("MIDIHub: recovered sequencer Stop; "
                 "position exceeds MIDI SPP range");
        }
        return;
    }
    if (rt->sequencer_running) {
        message[0] = 0xfc;
        deliver_short(rt, message, 1);
    }
    message[0] = 0xf2;
    message[1] = (uint8_t)((clock / 6) & 0x7f);
    message[2] = (uint8_t)(((clock / 6) >> 7) & 0x7f);
    deliver_short(rt, message, 3);
    remainder = clock % 6;
    if (state->downbeat_played) {
        message[0] = 0xfb;
        deliver_short(rt, message, 1);
        message[0] = 0xf8;
        for (i = 0; i <= remainder; ++i)
            deliver_short(rt, message, 1);
        if (!state->sequencer_running) {
            message[0] = 0xfc;
            deliver_short(rt, message, 1);
        }
    } else if (state->sequencer_running) {
        message[0] = 0xfb;
        deliver_short(rt, message, 1);
    }
    printf("MIDIHub: recovered sequencer state running=%u clock=%lu downbeat=%u\n",
           (unsigned int)state->sequencer_running,
           (unsigned long)clock, (unsigned int)state->downbeat_played);
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
        rt->last_peer_sync = now;
        response = *incoming;
        response.ssrc = rt->session.local_ssrc;
        response.sync_count = 1;
        response.timestamps[1] = now;
        rt->sync_t1 = incoming->timestamps[0];
        rt->sync_t2 = now;
        send_apple(rt, 1, &response);
    } else if (incoming->sync_count == 1 && rt->initiating &&
               rt->sync_outstanding &&
               incoming->timestamps[0] == rt->last_sync) {
        response = *incoming;
        response.ssrc = rt->session.local_ssrc;
        response.sync_count = 2;
        response.timestamps[2] = now;
        send_apple(rt, 1, &response);
        if (mh_clock_offset(response.timestamps, 1,
                            &rt->peer_to_local) == 0) {
            rt->sync_ready = 1;
            rt->sync_outstanding = 0;
            if (rt->sync_exchanges < 3)
                ++rt->sync_exchanges;
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
    struct mh_journal journal;
    struct mh_journal_system_state system_state;
    struct mh_rtp_reader reader;
    struct mh_midi_event event;
    const uint8_t *sysex_message;
    size_t sysex_length;
    int response_port;
    int action;
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
        if (packet.command == MH_APPLE_RS) {
            if (!data_port && rt->session.phase == MH_SESSION_CONNECTED &&
                packet.ssrc == rt->session.peer_ssrc)
                mh_sender_ack(&rt->sender, packet.feedback_sequence);
            return;
        }
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
            rt->last_peer_sync = now;
        } else if (packet.command == MH_APPLE_BY &&
                   rt->session.phase == MH_SESSION_IDLE) {
            puts("MIDIHub: peer disconnected");
            rt->have_peer = 0;
            rt->sync_ready = 0;
            rt->peer_to_local = 0;
            rt->sync_t1 = rt->sync_t2 = 0;
            rt->sync_exchanges = 0;
            rt->sync_outstanding = 0;
            rt->have_received_sequence = 0;
            mh_sysex_reset(&rt->sysex);
            mh_queue_reset(&rt->queue);
            release_active_notes(rt);
            reset_channel_state(rt);
            mh_sender_reset(&rt->sender);
        }
        return;
    }
    if (!data_port || rt->session.phase != MH_SESSION_CONNECTED ||
        mh_rtp_decode(bytes, (size_t)count, &midi) != 0 ||
        midi.ssrc != rt->session.peer_ssrc)
        return;
    if (midi.journal &&
        mh_journal_decode(midi.journal, midi.journal_length,
                          &journal) != 0) {
        puts("MIDIHub: malformed recovery journal discarded");
        return;
    }
    if (midi.journal && journal.system) {
        system_decoded = mh_journal_decode_system(&journal, &system_state);
        if (system_decoded < 0) {
            puts("MIDIHub: malformed system recovery journal discarded");
            return;
        }
    }
    mh_rtp_reader_init(&reader, &midi);
    while ((action = mh_rtp_reader_next(&reader, &event)) == 1) {}
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
                   mh_journal_covers_gap(
                       previous_sequence, midi.sequence, journal.checkpoint) ?
                                             "covers gap" :
                                             "does not cover gap");
        }
    }
    if (!send_feedback(rt, midi.sequence))
        return; /* A duplicate or older packet must not replay MIDI events. */
    if (sequence_advance > 1 && midi.journal &&
        mh_journal_covers_gap(previous_sequence, midi.sequence,
                              journal.checkpoint)) {
        if (system_decoded > 0 &&
            !(sequence_advance == 2 && journal.single_packet_safe))
            recover_system(rt, &system_state, sequence_advance == 2);
        recover_channel_state(rt, &journal, sequence_advance == 2);
        recover_notes(rt, &journal, sequence_advance == 2,
                      midi.timestamp);
        recover_controls(rt, &journal, sequence_advance == 2);
        recover_aftertouch(rt, &journal, sequence_advance == 2);
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
    mh_rtp_reader_init(&reader, &midi);
    while ((action = mh_rtp_reader_next(&reader, &event)) == 1) {
        if (event.sysex) {
            sysex_result = mh_sysex_feed(&rt->sysex, &event, midi.sequence,
                                          &sysex_message, &sysex_length);
            if (sysex_result == 1) {
                uint64_t due;
                printf("MIDIHub: SysEx complete bytes=%lu\n",
                       (unsigned long)sysex_length);
                now = now_ticks();
                if (rt->sync_ready &&
                    mh_clock_due(rt->sysex.timestamp, now,
                                 rt->peer_to_local, &due) == 0 && due > now) {
                    if (due - now > 100000 ||
                        mh_queue_push_sysex(&rt->queue, due, sysex_message,
                                            sysex_length) != 0)
                        puts("MIDIHub: future SysEx event discarded");
                    continue;
                }
                deliver_sysex(rt, sysex_message, sysex_length);
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
            deliver_short(rt, event.bytes, event.length);
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
    size_t journal_length;
    size_t data_length;
    size_t offset;
    size_t chunk;
    size_t segment_length;
    uint8_t head;
    uint8_t tail;
    size_t i;
    uint32_t timestamp;
    uint16_t first_sequence;

    if (rt->session.phase != MH_SESSION_CONNECTED || !message || !length)
        return -1;
    if (message[0] != 0xf0) {
        timestamp = (uint32_t)now_ticks();
        if (mh_rtp_encode_short(rt->sequence, timestamp,
                                rt->session.local_ssrc, message, length,
                                wire, sizeof(wire), &wire_length) != 0)
            return -1;
        if (mh_sender_supported(message, length)) {
            if (mh_sender_journal(&rt->sender, rt->sequence, timestamp,
                                  wire + wire_length,
                                  sizeof(wire) - wire_length,
                                  &journal_length) != 0)
                return -1;
            wire[12] |= 0x40;
            wire_length += journal_length;
        }
        if (sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        if (mh_sender_supported(message, length))
            mh_sender_record(&rt->sender, rt->sequence, timestamp,
                              message, length);
        else
            mh_sender_clear_history(&rt->sender);
        rt->last_rtp_send = now_ticks();
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        return 0;
    }
    if (length < 2 || length > MH_SYSEX_MAX || message[length - 1] != 0xf7)
        return -1;
    for (i = 1; i + 1 < length; ++i)
        if (message[i] & 0x80)
            return -1;
    mh_sender_clear_history(&rt->sender);
    timestamp = (uint32_t)now_ticks();
    first_sequence = rt->sequence;
    if (length <= 1002) {
        if (mh_rtp_encode_list(rt->sequence, timestamp,
                               rt->session.local_ssrc, message, length,
                               wire, sizeof(wire), &wire_length) != 0 ||
            sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        mh_sender_record_sysex(&rt->sender, first_sequence, timestamp,
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
        if (mh_rtp_encode_list(rt->sequence, timestamp,
                               rt->session.local_ssrc, segment,
                               segment_length, wire, sizeof(wire),
                               &wire_length) != 0 ||
            sendto(rt->data, wire, (int)wire_length, 0,
                   (const struct sockaddr *)&rt->peer_data,
                   sizeof(rt->peer_data)) != (int)wire_length)
            return -1;
        ++rt->sequence;
        rt->last_rtp_send = now_ticks();
        offset += chunk;
    }
    mh_sender_record_sysex(&rt->sender, first_sequence, timestamp,
                           message, length);
    return 0;
}

static void send_guard(struct runtime *rt, uint64_t now)
{
    uint8_t wire[1100];
    size_t wire_length;
    size_t journal_length;
    uint32_t timestamp = (uint32_t)now;
    if (mh_rtp_encode_list(rt->sequence, timestamp, rt->session.local_ssrc,
                           NULL, 0, wire, sizeof(wire), &wire_length) != 0 ||
        mh_sender_journal(&rt->sender, rt->sequence, timestamp,
                          wire + wire_length, sizeof(wire) - wire_length,
                          &journal_length) != 0)
        return;
    wire[12] |= 0x40;
    wire_length += journal_length;
    if (sendto(rt->data, wire, (int)wire_length, 0,
               (const struct sockaddr *)&rt->peer_data,
               sizeof(rt->peer_data)) == (int)wire_length) {
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
    struct mh_apple_packet packet;
    struct mh_queued_event event;
    int data_port;

    while (mh_queue_pop_due(&rt->queue, now, &event)) {
        if (event.sysex)
            deliver_sysex(rt, event.sysex, event.sysex_length);
        else
            deliver_short(rt, event.bytes, event.length);
        mh_queue_event_release(&event);
    }

    if (rt->mdns >= 0 &&
        (rt->mdns_announcements == 0 ||
         elapsed_ticks(now, rt->last_mdns_announce) >=
             (rt->mdns_announcements < 2 ? 10000u : 600000u))) {
        send_mdns(rt, NULL, 120, 0);
        rt->last_mdns_announce = now;
        if (rt->mdns_announcements < 2) ++rt->mdns_announcements;
    }

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
        if (!rt->initiating &&
            elapsed_ticks(now, rt->last_peer_sync) >= 1200000) {
            struct mh_apple_packet goodbye;
            puts("MIDIHub: peer synchronization timed out");
            if (mh_session_end(&rt->session, &goodbye) == 0)
                send_apple(rt, 0, &goodbye);
            rt->have_peer = 0;
            rt->sync_ready = 0;
            rt->peer_to_local = 0;
            rt->sync_t1 = rt->sync_t2 = 0;
            rt->have_received_sequence = 0;
            mh_sysex_reset(&rt->sysex);
            mh_queue_reset(&rt->queue);
            release_active_notes(rt);
            reset_channel_state(rt);
            mh_sender_reset(&rt->sender);
            return;
        }
        if (rt->initiating &&
            elapsed_ticks(now, rt->last_sync) >=
                (rt->sync_exchanges < 3 ? 10000u : 500000u))
            send_sync(rt, now);
        if (rt->sender.count &&
            elapsed_ticks(now, rt->last_rtp_send) >= 10000)
            send_guard(rt, now);
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
        rt->sync_exchanges = 0;
        rt->sync_outstanding = 0;
        rt->have_received_sequence = 0;
        memset(rt->active_notes, 0, sizeof(rt->active_notes));
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
    reset_channel_state(&rt);
    rt.control = rt.data = rt.mdns = -1;
    rt.local_port = local_port;
    strcpy(rt.mdns_session, config.session_name);
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
    snprintf(rt.mdns_host, sizeof(rt.mdns_host), "midihub-%04x-%08lx",
             (unsigned int)local_port,
             (unsigned long)rt.session.local_ssrc);
    rt.mdns = open_mdns(rt.mdns_ip);
    if (rt.mdns < 0)
        puts("MIDIHub: Bonjour discovery unavailable");
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
        if (rt.mdns >= 0) FD_SET(rt.mdns, &read_set);
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
        ready = WaitSelect((rt.mdns > rt.control && rt.mdns > rt.data ?
                            rt.mdns : rt.control > rt.data ? rt.control :
                            rt.data) + 1,
                           &read_set, NULL, NULL, &timeout, &signal_mask);
        if (signal_mask & SIGBREAKF_CTRL_C)
            break;
#else
        ready = select((rt.mdns > rt.control && rt.mdns > rt.data ?
                        rt.mdns : rt.control > rt.data ? rt.control :
                        rt.data) + 1,
                       &read_set, NULL, NULL, &timeout);
#endif
        if (ready > 0) {
            if (FD_ISSET(rt.control, &read_set))
                receive_packet(&rt, 0);
            if (FD_ISSET(rt.data, &read_set))
                receive_packet(&rt, 1);
            if (rt.mdns >= 0 && FD_ISSET(rt.mdns, &read_set))
                receive_mdns(&rt);
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
    if (rt.mdns >= 0) {
        send_mdns(&rt, NULL, 0, 0);
        mh_close(rt.mdns);
    }
    if (rt.camd_opened) {
        mh_queue_reset(&rt.queue);
        release_active_notes(&rt);
    }
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
