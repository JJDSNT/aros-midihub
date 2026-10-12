#ifndef MIDIHUB_AROS_DISCOVERY_H
#define MIDIHUB_AROS_DISCOVERY_H

#include <stdint.h>

/* The program's name on the network, through the system's mDNS service:
 * bonami.library and mdnsd on AROS. The program holds no socket on port
 * 5353 and answers no question itself; the service does, for every
 * program on the machine. Where there is no such service (the host build
 * used for tests) nothing is advertised and the calls say so. */

#define NETMIDI_DISCOVERY_SERVICES 2

struct netmidi_discovery {
    void *library;                  /* bonami.library, opened */
    int engine;                     /* the service answered */
    struct {
        const char *name;
        const char *type;
        uint16_t port;
        void *reference;            /* the registration, or NULL */
    } service[NETMIDI_DISCOVERY_SERVICES];
    int count;
    uint64_t last_attempt;
};

void netmidi_discovery_init(struct netmidi_discovery *discovery);
/* Asks for an instance of type on port to be advertised under name. The
 * strings have to last as long as the program. Nothing is sent here. */
int netmidi_discovery_add(struct netmidi_discovery *discovery,
                          const char *name, const char *type, uint16_t port);
/* Registers what is not yet registered. The service may start after the
 * program, or stop and start again with the network, so this is called
 * regularly; it tries at most once in ten seconds. now is in
 * ten-thousandths of a second. Returns the number of services registered
 * now, -1 where there is no service to ask. */
int netmidi_discovery_poll(struct netmidi_discovery *discovery,
                           uint64_t now);
/* Withdraws everything, with the goodbye the service sends for it. */
void netmidi_discovery_close(struct netmidi_discovery *discovery);

#endif
