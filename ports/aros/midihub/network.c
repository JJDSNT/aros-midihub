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
#include <proto/entropy.h>
#include <proto/exec.h>
#include <resources/entropy.h>
struct Library *SocketBase;
struct Library *EntropyBase;
#else
#include <fcntl.h>
#include <unistd.h>
#endif

int netmidi_network_random(void *buffer, size_t length)
{
#ifdef __AROS__
    /* the base the call's inline uses; resources are not closed */
    if (!EntropyBase)
        EntropyBase = OpenResource((CONST_STRPTR)ENTROPYNAME);
    if (!EntropyBase || !buffer)
        return -1;
    return GetEntropy(buffer, (ULONG)length) == (LONG)length ? 0 : -1;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got;
    if (fd < 0 || !buffer)
        return -1;
    got = read(fd, buffer, length);
    close(fd);
    return got == (ssize_t)length ? 0 : -1;
#endif
}

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
#ifdef NETMIDI_MIDI2
    runtime->midi2 = -1;
#endif
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

#ifdef NETMIDI_MIDI2
int netmidi_network_open_midi2(struct netmidi_network_runtime *runtime,
                               uint16_t port)
{
    if (!runtime || !runtime->platform_opened || !port)
        return -1;
    runtime->midi2 = open_udp(port);
    return runtime->midi2 < 0 ? -1 : 0;
}

int netmidi_network_midi2_send(const struct netmidi_network_runtime *runtime,
                               const void *bytes, size_t length,
                               const struct sockaddr_in *destination)
{
    if (!runtime || !bytes || !destination || runtime->midi2 < 0)
        return -1;
    return sendto(runtime->midi2, bytes, (int)length, 0,
                  (const struct sockaddr *)destination,
                  sizeof(*destination)) == (int)length ? 0 : -1;
}

int netmidi_network_midi2_receive(
    const struct netmidi_network_runtime *runtime, void *bytes,
    size_t capacity, struct sockaddr_in *source, socklen_t *source_length)
{
    if (!runtime || !bytes || !source || !source_length ||
        runtime->midi2 < 0)
        return -1;
    return recvfrom(runtime->midi2, bytes, (int)capacity, 0,
                    (struct sockaddr *)source, source_length);
}
#endif

void netmidi_network_close(struct netmidi_network_runtime *runtime)
{
    if (!runtime)
        return;
#ifdef NETMIDI_MIDI2
    netmidi_network_close_socket(runtime->midi2);
    runtime->midi2 = -1;
#endif
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
#ifdef NETMIDI_MIDI2
    if (runtime->midi2 >= 0) {
        FD_SET(runtime->midi2, &read_set);
        if (runtime->midi2 > maximum)
            maximum = runtime->midi2;
    }
#endif
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
#ifdef NETMIDI_MIDI2
        if (runtime->midi2 >= 0 && FD_ISSET(runtime->midi2, &read_set))
            *events |= NETMIDI_NETWORK_EVENT_MIDI2;
#endif
        if (auxiliary_fd >= 0 && FD_ISSET(auxiliary_fd, &read_set))
            *events |= NETMIDI_NETWORK_EVENT_AUXILIARY;
    }
    return ready < 0 ? -1 : 0;
}

int netmidi_network_hostname(char *name, size_t size)
{
    size_t i;

    if (!name || !size)
        return -1;
    name[0] = 0;
#ifdef __AROS__
    if (!SocketBase)
        return -1;
#endif
    if (gethostname(name, size) != 0) {
        name[0] = 0;
        return -1;
    }
    name[size - 1] = 0;
    for (i = 0; name[i]; ++i)
        if (name[i] == '.') {
            name[i] = 0;
            break;
        }
    return name[0] ? 0 : -1;
}
