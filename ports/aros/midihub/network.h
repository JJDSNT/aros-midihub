#ifndef MIDIHUB_AROS_NETWORK_H
#define MIDIHUB_AROS_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef __AROS__
struct Library;
/* AROS bsdsocket inline stubs used by the embedded discovery fallback share
 * the library base opened by this adapter. */
extern struct Library *SocketBase;
#endif

struct netmidi_network_runtime {
    int control;
    int data;
    int platform_opened;
};

enum netmidi_network_event {
    NETMIDI_NETWORK_EVENT_CONTROL = 1u << 0,
    NETMIDI_NETWORK_EVENT_DATA = 1u << 1,
    NETMIDI_NETWORK_EVENT_AUXILIARY = 1u << 2,
    NETMIDI_NETWORK_EVENT_STOP = 1u << 3
};

void netmidi_network_init(struct netmidi_network_runtime *runtime);
int netmidi_network_platform_open(struct netmidi_network_runtime *runtime);
int netmidi_network_open_pair(struct netmidi_network_runtime *runtime,
                              uint16_t control_port);
void netmidi_network_close(struct netmidi_network_runtime *runtime);
void netmidi_network_close_socket(int fd);
int netmidi_network_send(const struct netmidi_network_runtime *runtime,
                         int data_port, const void *bytes, size_t length,
                         const struct sockaddr_in *destination);
int netmidi_network_receive(const struct netmidi_network_runtime *runtime,
                            int data_port, void *bytes, size_t capacity,
                            struct sockaddr_in *source,
                            socklen_t *source_length);
int netmidi_network_wait(const struct netmidi_network_runtime *runtime,
                         int auxiliary_fd, long timeout_us,
                         int wake_signal_bit, unsigned int *events);

#endif
