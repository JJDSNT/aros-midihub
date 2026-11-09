/*
 * DNS-SD coordinator lifecycle prototype.
 *
 * This is deliberately not a public dnssd.library header.  It models the
 * bounded ownership rules that must be proven before the public ABI exists.
 */
#ifndef DNSSD_COORDINATOR_PROTOTYPE_H
#define DNSSD_COORDINATOR_PROTOTYPE_H

#include <stdint.h>

#define DNSSD_COORD_SESSIONS 16u
#define DNSSD_COORD_OPERATIONS 32u
#define DNSSD_COORD_EVENT_QUEUE 4u

#define DNSSD_COORD_EXCLUSIVE 0x00000001u

enum dnssd_coord_result {
    DNSSD_COORD_OK = 0,
    DNSSD_COORD_BUSY = -1,
    DNSSD_COORD_STALE = -2,
    DNSSD_COORD_NOSPACE = -3,
    DNSSD_COORD_EMPTY = -4,
    DNSSD_COORD_LOST = -5
};

struct dnssd_coord_owner {
    uintptr_t task;
    uint32_t id;
};

struct dnssd_coord_handle {
    uint32_t incarnation;
    uint32_t generation;
    uint16_t slot;
    uint16_t reserved;
};

struct dnssd_coord_stats {
    uint32_t sessions;
    uint32_t operations;
    uint32_t notification_targets;
    uint32_t lost_queues;
};

struct dnssd_coord_session {
    struct dnssd_coord_owner owner;
    uintptr_t notification_target;
    uint32_t generation;
    uint8_t active;
};

struct dnssd_coord_operation {
    uint64_t conflict_key;
    uint32_t generation;
    uint32_t kind;
    uint32_t flags;
    uint32_t events[DNSSD_COORD_EVENT_QUEUE];
    uint16_t session_slot;
    uint8_t head;
    uint8_t count;
    uint8_t lost;
    uint8_t active;
};

struct dnssd_coordinator {
    uint32_t incarnation;
    uint32_t next_generation;
    struct dnssd_coord_session sessions[DNSSD_COORD_SESSIONS];
    struct dnssd_coord_operation operations[DNSSD_COORD_OPERATIONS];
};

void dnssd_coord_init(struct dnssd_coordinator *coordinator,
                      uint32_t incarnation);
void dnssd_coord_restart(struct dnssd_coordinator *coordinator,
                         uint32_t incarnation);
int dnssd_coord_open(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_owner owner,
                     uintptr_t notification_target,
                     struct dnssd_coord_handle *session);
int dnssd_coord_begin(struct dnssd_coordinator *coordinator,
                      struct dnssd_coord_handle session,
                      struct dnssd_coord_owner owner,
                      uint32_t kind, uint64_t conflict_key, uint32_t flags,
                      struct dnssd_coord_handle *operation);
int dnssd_coord_cancel(struct dnssd_coordinator *coordinator,
                       struct dnssd_coord_handle session,
                       struct dnssd_coord_owner owner,
                       struct dnssd_coord_handle operation);
int dnssd_coord_close(struct dnssd_coordinator *coordinator,
                      struct dnssd_coord_handle session,
                      struct dnssd_coord_owner owner);
uint32_t dnssd_coord_owner_removed(struct dnssd_coordinator *coordinator,
                                   struct dnssd_coord_owner owner);
int dnssd_coord_push(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_handle operation, uint32_t event);
int dnssd_coord_read(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_handle session,
                     struct dnssd_coord_owner owner,
                     struct dnssd_coord_handle operation, uint32_t *event);
void dnssd_coord_get_stats(const struct dnssd_coordinator *coordinator,
                           struct dnssd_coord_stats *stats);

#endif
