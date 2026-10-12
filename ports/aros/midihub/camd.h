#ifndef MIDIHUB_AROS_CAMD_H
#define MIDIHUB_AROS_CAMD_H

#include "../camd_bridge.h"

/* CAMD-facing state owned by the Network MIDI runtime.  Keeping this wrapper
 * separate from the reusable bridge gives the resident process one boundary
 * for CAMD lifecycle and leaves room for one bridge per peer later. */
/* The service has the ports of two drivers, a peer to each port: those of
 * applemidi from 0, those of networkmidi2 from NETMIDI_CAMD_MIDI2. The
 * program that is a client of camd.library has one pair of clusters, which
 * every peer of either kind shares: all its messages are of port 0,
 * whatever port is named. */
#define NETMIDI_CAMD_PORTS 16       /* of each driver */
#define NETMIDI_CAMD_DRIVERS 2
#define NETMIDI_CAMD_MIDI2 NETMIDI_CAMD_PORTS

struct netmidi_camd_runtime {
    struct aros_camd_bridge bridge;
    int opened;
#ifdef NETMIDI_SERVICE
    /* The service reaches CAMD through the adapters of its two drivers
       (adapters.c), not as a client of camd.library. */
    void *adapters[NETMIDI_CAMD_DRIVERS];   /* struct NetMidiAdapters of
                                               each driver, once found */
    int signal_bit;
    int reported[NETMIDI_CAMD_DRIVERS];     /* told once of another layout */
    void *status;               /* struct NetMidiStatus, made public */
    /* the bytes CAMD clients sent to each port, gathered into messages */
    struct {
        uint8_t status;
        uint8_t message[3];
        uint8_t have;
        uint8_t need;
        int in_sysex;
        size_t sysex_length;
    } stream[NETMIDI_CAMD_PORTS * NETMIDI_CAMD_DRIVERS];
    uint8_t *sysex;             /* one message at a time, of any port */
    int sysex_port;
#endif
    int port_now;               /* during netmidi_camd_poll's output: the
                                   port the message was sent to */
};

int netmidi_camd_open(struct netmidi_camd_runtime *runtime);
void netmidi_camd_close(struct netmidi_camd_runtime *runtime);
int netmidi_camd_is_open(const struct netmidi_camd_runtime *runtime);
int netmidi_camd_signal_bit(const struct netmidi_camd_runtime *runtime);
void netmidi_camd_deliver(struct netmidi_camd_runtime *runtime,
                          const uint8_t *message, size_t length);
/* The same, to the port a peer was given. */
void netmidi_camd_deliver_port(struct netmidi_camd_runtime *runtime, int port,
                               const uint8_t *message, size_t length);
/* How many ports there are for peers to be given: 1 when they share. */
int netmidi_camd_ports(const struct netmidi_camd_runtime *runtime);
void netmidi_camd_deliver_sysex(struct netmidi_camd_runtime *runtime,
                                const uint8_t *message, size_t length);
void netmidi_camd_poll(struct netmidi_camd_runtime *runtime,
                       aros_camd_output output, void *context);

/* What has passed on a port, for whoever asks the service (R18). The
 * runtime counts; where there is a way to ask, netmidi_camd_publish makes
 * the counts public. */
struct netmidi_port_counts {
    int present;
    char name[64];
    unsigned long packets_in, packets_out, packets_lost;
    unsigned long messages_in, messages_out;
    unsigned long timed, late, sysex_dropped, journals_omitted, undefined;
};

/* counts has one entry to a port, numbered through as the ports are.
 * Where peers share one pair of clusters nothing is made public. */
void netmidi_camd_publish(struct netmidi_camd_runtime *runtime,
                          const char *session, unsigned long refused,
                          const struct netmidi_port_counts *counts,
                          int count);

#endif
