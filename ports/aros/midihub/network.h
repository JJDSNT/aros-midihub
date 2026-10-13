/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#ifndef NETMIDI_NETWORK_H
#define NETMIDI_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef __AROS__
struct Library;
/* bsdsocket.library's base, opened by netmidi_network_platform_open: the
 * inline stubs of whatever is linked with this use the one. */
extern struct Library *SocketBase;
#endif

struct netmidi_network_runtime {
    int control;
    int data;
#ifdef NETMIDI_MIDI2
    int midi2;              /* Network MIDI 2.0's one port, or -1 */
#endif
    int platform_opened;
};

enum netmidi_network_event {
    NETMIDI_NETWORK_EVENT_CONTROL = 1u << 0,
    NETMIDI_NETWORK_EVENT_DATA = 1u << 1,
    NETMIDI_NETWORK_EVENT_AUXILIARY = 1u << 2,
    NETMIDI_NETWORK_EVENT_STOP = 1u << 3
#ifdef NETMIDI_MIDI2
    , NETMIDI_NETWORK_EVENT_MIDI2 = 1u << 4
#endif
};

void netmidi_network_init(struct netmidi_network_runtime *runtime);
/* This machine's name on the network, without its domain; after
 * netmidi_network_platform_open. Returns -1, with name empty, when it has
 * none. */
int netmidi_network_hostname(char *name, size_t size);
int netmidi_network_platform_open(struct netmidi_network_runtime *runtime);
int netmidi_network_open_pair(struct netmidi_network_runtime *runtime,
                              uint16_t control_port);
#ifdef NETMIDI_MIDI2
/* A third socket, for Network MIDI 2.0, which uses one port for sessions
 * and data alike. Optional: without it nothing else changes. */
int netmidi_network_open_midi2(struct netmidi_network_runtime *runtime,
                               uint16_t port);
int netmidi_network_midi2_send(const struct netmidi_network_runtime *runtime,
                               const void *bytes, size_t length,
                               const struct sockaddr_in *destination);
int netmidi_network_midi2_receive(
    const struct netmidi_network_runtime *runtime, void *bytes,
    size_t capacity, struct sockaddr_in *source, socklen_t *source_length);
#endif
void netmidi_network_close(struct netmidi_network_runtime *runtime);
void netmidi_network_close_socket(int fd);
int netmidi_network_send(const struct netmidi_network_runtime *runtime,
                         int data_port, const void *bytes, size_t length,
                         const struct sockaddr_in *destination);
int netmidi_network_receive(const struct netmidi_network_runtime *runtime,
                            int data_port, void *bytes, size_t capacity,
                            struct sockaddr_in *source,
                            socklen_t *source_length);
/* Fills the buffer from the system's random source: entropy.resource on
 * AROS. Returns -1 when there is none or it gave less than was asked. */
int netmidi_network_random(void *buffer, size_t length);
int netmidi_network_wait(const struct netmidi_network_runtime *runtime,
                         int auxiliary_fd, long timeout_us,
                         int wake_signal_bit, unsigned int *events);

#endif
