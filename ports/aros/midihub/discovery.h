#ifndef MIDIHUB_AROS_DISCOVERY_H
#define MIDIHUB_AROS_DISCOVERY_H

#include <stdint.h>

/* The program's name on the network, through the system's mDNS service:
 * bonami.library and mdnsd on AROS. The program holds no socket on port
 * 5353 and answers no question itself; the service does, for every
 * program on the machine. Where there is no such service (the host build
 * used for tests) nothing is advertised and the calls say so. */

#define NETMIDI_DISCOVERY_SERVICES 2
#define NETMIDI_DISCOVERY_EVENTS 16

/* A service instance someone else advertises, as it came or went. */
struct netmidi_discovered {
    char name[64];
    uint32_t address;               /* IPv4, host order; 0 when not known */
    uint16_t port;
    int present;                    /* 1 it is there, 0 it left */
};

struct netmidi_discovery {
    void *library;                  /* bonami.library, opened */
    int engine;                     /* the service answered */
    struct {
        const char *name;
        const char *type;
        uint16_t port;
        const char *text;           /* its text record, or NULL */
        uint32_t seconds;           /* its records' time to live, 0 for
                                       the service's own choice */
        void *reference;            /* the registration, or NULL */
    } service[NETMIDI_DISCOVERY_SERVICES];
    int count;
    uint64_t last_attempt;
    const char *browse_type;        /* what to look for, or NULL */
    void *browse_reference;         /* the search, or NULL */
    void *browse_hook;
    /* written by the service's process, read by ours */
    struct netmidi_discovered event[NETMIDI_DISCOVERY_EVENTS];
    volatile unsigned long written;
    unsigned long read;
};

void netmidi_discovery_init(struct netmidi_discovery *discovery);
/* Asks for an instance of type on port to be advertised under name. The
 * strings have to last as long as the program. Nothing is sent here. */
int netmidi_discovery_add(struct netmidi_discovery *discovery,
                          const char *name, const char *type, uint16_t port);
/* The same with a text record and a time to live for the instance's
 * records. text is the items with a space between, a space or backslash
 * inside an item written with a backslash before it; netmidi_discovery_item
 * writes one so. */
int netmidi_discovery_add_text(struct netmidi_discovery *discovery,
                               const char *name, const char *type,
                               uint16_t port, const char *text,
                               uint32_t seconds);
/* Appends "key=value" to text as one item. Returns -1, with text as it
 * was, when it does not fit in size. */
int netmidi_discovery_item(char *text, unsigned long size, const char *key,
                           const char *value);
/* Registers what is not yet registered. The service may start after the
 * program, or stop and start again with the network, so this is called
 * regularly; it tries at most once in ten seconds. now is in
 * ten-thousandths of a second. Returns the number of services registered
 * now, -1 where there is no service to ask. */
int netmidi_discovery_poll(struct netmidi_discovery *discovery,
                           uint64_t now);
/* Asks for instances of type to be looked for. Like a registration, the
 * search begins at the next poll at which the service answers. */
int netmidi_discovery_browse(struct netmidi_discovery *discovery,
                             const char *type);
/* The next instance that appeared or left, 1, or 0 when there is none
 * waiting. An instance is reported as present only once its address and
 * port are known, and may be reported so more than once. Our own
 * registrations come back here too; the caller knows its own name. */
int netmidi_discovery_next(struct netmidi_discovery *discovery,
                           struct netmidi_discovered *event);
/* Withdraws everything, with the goodbye the service sends for it. */
void netmidi_discovery_close(struct netmidi_discovery *discovery);

#endif
