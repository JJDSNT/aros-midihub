#ifndef __AROS__
#define _DEFAULT_SOURCE 1
#endif

#include "network.h"

#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#ifdef __AROS__
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <proto/bsdsocket.h>
#include <proto/exec.h>
struct Library *SocketBase;
#else
#include <unistd.h>
#endif

void netmidi_network_close_socket(int fd)
{
    if (fd < 0)
        return;
#ifdef __AROS__
    CloseSocket(fd);
#else
    close(fd);
#endif
}

void netmidi_network_init(struct netmidi_network_runtime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->control = -1;
    runtime->data = -1;
}

int netmidi_network_platform_open(struct netmidi_network_runtime *runtime)
{
    if (!runtime)
        return -1;
#ifdef __AROS__
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase)
        return -1;
#endif
    runtime->platform_opened = 1;
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
        netmidi_network_close_socket(fd);
        return -1;
    }
    return fd;
}

int netmidi_network_open_pair(struct netmidi_network_runtime *runtime,
                              uint16_t control_port)
{
    if (!runtime || !runtime->platform_opened || control_port == 65535)
        return -1;
    runtime->control = open_udp(control_port);
    runtime->data = open_udp((uint16_t)(control_port + 1));
    if (runtime->control < 0 || runtime->data < 0) {
        netmidi_network_close_socket(runtime->data);
        netmidi_network_close_socket(runtime->control);
        runtime->control = -1;
        runtime->data = -1;
        return -1;
    }
    return 0;
}

void netmidi_network_close(struct netmidi_network_runtime *runtime)
{
    if (!runtime)
        return;
    netmidi_network_close_socket(runtime->data);
    netmidi_network_close_socket(runtime->control);
    runtime->data = -1;
    runtime->control = -1;
#ifdef __AROS__
    if (runtime->platform_opened && SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
#endif
    runtime->platform_opened = 0;
}

int netmidi_network_send(const struct netmidi_network_runtime *runtime,
                         int data_port, const void *bytes, size_t length,
                         const struct sockaddr_in *destination)
{
    int fd;
    if (!runtime || !bytes || !destination)
        return -1;
    fd = data_port ? runtime->data : runtime->control;
    if (fd < 0)
        return -1;
    return sendto(fd, bytes, (int)length, 0,
                  (const struct sockaddr *)destination,
                  sizeof(*destination)) == (int)length ? 0 : -1;
}

int netmidi_network_receive(const struct netmidi_network_runtime *runtime,
                            int data_port, void *bytes, size_t capacity,
                            struct sockaddr_in *source,
                            socklen_t *source_length)
{
    int fd;
    if (!runtime || !bytes || !source || !source_length)
        return -1;
    fd = data_port ? runtime->data : runtime->control;
    if (fd < 0)
        return -1;
    return recvfrom(fd, bytes, (int)capacity, 0,
                    (struct sockaddr *)source, source_length);
}

int netmidi_network_wait(const struct netmidi_network_runtime *runtime,
                         int auxiliary_fd, long timeout_us,
                         int wake_signal_bit, unsigned int *events)
{
    struct timeval timeout;
    fd_set read_set;
    int maximum;
    int ready;
#ifdef __AROS__
    ULONG signal_mask = SIGBREAKF_CTRL_C;
#else
    (void)wake_signal_bit;
#endif

    if (!runtime || !events || runtime->control < 0 || runtime->data < 0)
        return -1;
    *events = 0;
    FD_ZERO(&read_set);
    FD_SET(runtime->control, &read_set);
    FD_SET(runtime->data, &read_set);
    maximum = runtime->control > runtime->data ? runtime->control :
                                                 runtime->data;
    if (auxiliary_fd >= 0) {
        FD_SET(auxiliary_fd, &read_set);
        if (auxiliary_fd > maximum)
            maximum = auxiliary_fd;
    }
    timeout.tv_sec = timeout_us / 1000000;
    timeout.tv_usec = timeout_us % 1000000;
#ifdef __AROS__
    if (wake_signal_bit >= 0)
        signal_mask |= 1UL << wake_signal_bit;
    ready = WaitSelect(maximum + 1, &read_set, NULL, NULL, &timeout,
                       &signal_mask);
    if (signal_mask & SIGBREAKF_CTRL_C)
        *events |= NETMIDI_NETWORK_EVENT_STOP;
#else
    ready = select(maximum + 1, &read_set, NULL, NULL, &timeout);
#endif
    if (ready > 0) {
        if (FD_ISSET(runtime->control, &read_set))
            *events |= NETMIDI_NETWORK_EVENT_CONTROL;
        if (FD_ISSET(runtime->data, &read_set))
            *events |= NETMIDI_NETWORK_EVENT_DATA;
        if (auxiliary_fd >= 0 && FD_ISSET(auxiliary_fd, &read_set))
            *events |= NETMIDI_NETWORK_EVENT_AUXILIARY;
    }
    return ready < 0 ? -1 : 0;
}
