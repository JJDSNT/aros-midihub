#include "coordinator.h"

#include <string.h>

static int same_owner(struct dnssd_coord_owner a,
                      struct dnssd_coord_owner b)
{
    return a.task != 0 && a.task == b.task && a.id != 0 && a.id == b.id;
}

static uint32_t next_generation(struct dnssd_coordinator *coordinator)
{
    uint32_t generation = ++coordinator->next_generation;

    if (generation == 0)
        generation = ++coordinator->next_generation;
    return generation;
}

static struct dnssd_coord_session *get_session(
    struct dnssd_coordinator *coordinator, struct dnssd_coord_handle handle,
    struct dnssd_coord_owner owner)
{
    struct dnssd_coord_session *session;

    if (handle.incarnation != coordinator->incarnation || handle.slot == 0 ||
        handle.slot > DNSSD_COORD_SESSIONS)
        return NULL;
    session = &coordinator->sessions[handle.slot - 1u];
    if (!session->active || session->generation != handle.generation ||
        !same_owner(session->owner, owner))
        return NULL;
    return session;
}

static struct dnssd_coord_operation *get_operation(
    struct dnssd_coordinator *coordinator, struct dnssd_coord_handle handle)
{
    struct dnssd_coord_operation *operation;

    if (handle.incarnation != coordinator->incarnation || handle.slot == 0 ||
        handle.slot > DNSSD_COORD_OPERATIONS)
        return NULL;
    operation = &coordinator->operations[handle.slot - 1u];
    if (!operation->active || operation->generation != handle.generation)
        return NULL;
    return operation;
}

static void clear_operation(struct dnssd_coord_operation *operation)
{
    uint32_t generation = operation->generation;

    memset(operation, 0, sizeof(*operation));
    operation->generation = generation;
}

static uint32_t clear_session_operations(
    struct dnssd_coordinator *coordinator, uint16_t session_slot)
{
    uint32_t index, removed = 0;

    for (index = 0; index < DNSSD_COORD_OPERATIONS; ++index) {
        struct dnssd_coord_operation *operation =
            &coordinator->operations[index];
        if (operation->active && operation->session_slot == session_slot) {
            clear_operation(operation);
            ++removed;
        }
    }
    return removed;
}

void dnssd_coord_init(struct dnssd_coordinator *coordinator,
                      uint32_t incarnation)
{
    memset(coordinator, 0, sizeof(*coordinator));
    coordinator->incarnation = incarnation != 0 ? incarnation : 1u;
}

void dnssd_coord_restart(struct dnssd_coordinator *coordinator,
                         uint32_t incarnation)
{
    uint32_t next = incarnation;

    if (next == 0 || next == coordinator->incarnation) {
        next = coordinator->incarnation + 1u;
        if (next == 0)
            next = 1u;
    }
    dnssd_coord_init(coordinator, next);
}

int dnssd_coord_open(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_owner owner,
                     uintptr_t notification_target,
                     struct dnssd_coord_handle *session_handle)
{
    uint32_t index;

    if (owner.task == 0 || owner.id == 0 || session_handle == NULL)
        return DNSSD_COORD_STALE;
    for (index = 0; index < DNSSD_COORD_SESSIONS; ++index)
        if (coordinator->sessions[index].active &&
            same_owner(coordinator->sessions[index].owner, owner))
            return DNSSD_COORD_BUSY;
    for (index = 0; index < DNSSD_COORD_SESSIONS; ++index) {
        struct dnssd_coord_session *session = &coordinator->sessions[index];
        if (!session->active) {
            memset(session, 0, sizeof(*session));
            session->owner = owner;
            session->notification_target = notification_target;
            session->generation = next_generation(coordinator);
            session->active = 1;
            session_handle->incarnation = coordinator->incarnation;
            session_handle->generation = session->generation;
            session_handle->slot = (uint16_t)(index + 1u);
            session_handle->reserved = 0;
            return DNSSD_COORD_OK;
        }
    }
    return DNSSD_COORD_NOSPACE;
}

int dnssd_coord_begin(struct dnssd_coordinator *coordinator,
                      struct dnssd_coord_handle session_handle,
                      struct dnssd_coord_owner owner,
                      uint32_t kind, uint64_t conflict_key, uint32_t flags,
                      struct dnssd_coord_handle *operation_handle)
{
    uint32_t index;

    if (get_session(coordinator, session_handle, owner) == NULL ||
        operation_handle == NULL)
        return DNSSD_COORD_STALE;
    if ((flags & DNSSD_COORD_EXCLUSIVE) != 0) {
        for (index = 0; index < DNSSD_COORD_OPERATIONS; ++index) {
            const struct dnssd_coord_operation *operation =
                &coordinator->operations[index];
            if (operation->active &&
                (operation->flags & DNSSD_COORD_EXCLUSIVE) != 0 &&
                operation->kind == kind &&
                operation->conflict_key == conflict_key)
                return DNSSD_COORD_BUSY;
        }
    }
    for (index = 0; index < DNSSD_COORD_OPERATIONS; ++index) {
        struct dnssd_coord_operation *operation =
            &coordinator->operations[index];
        if (!operation->active) {
            memset(operation, 0, sizeof(*operation));
            operation->session_slot = session_handle.slot;
            operation->kind = kind;
            operation->conflict_key = conflict_key;
            operation->flags = flags;
            operation->generation = next_generation(coordinator);
            operation->active = 1;
            operation_handle->incarnation = coordinator->incarnation;
            operation_handle->generation = operation->generation;
            operation_handle->slot = (uint16_t)(index + 1u);
            operation_handle->reserved = 0;
            return DNSSD_COORD_OK;
        }
    }
    return DNSSD_COORD_NOSPACE;
}

int dnssd_coord_cancel(struct dnssd_coordinator *coordinator,
                       struct dnssd_coord_handle session_handle,
                       struct dnssd_coord_owner owner,
                       struct dnssd_coord_handle operation_handle)
{
    struct dnssd_coord_operation *operation;

    if (get_session(coordinator, session_handle, owner) == NULL)
        return DNSSD_COORD_STALE;
    operation = get_operation(coordinator, operation_handle);
    if (operation == NULL || operation->session_slot != session_handle.slot)
        return DNSSD_COORD_STALE;
    clear_operation(operation);
    return DNSSD_COORD_OK;
}

int dnssd_coord_close(struct dnssd_coordinator *coordinator,
                      struct dnssd_coord_handle session_handle,
                      struct dnssd_coord_owner owner)
{
    struct dnssd_coord_session *session =
        get_session(coordinator, session_handle, owner);

    if (session == NULL)
        return DNSSD_COORD_STALE;
    clear_session_operations(coordinator, session_handle.slot);
    session->notification_target = 0;
    session->active = 0;
    return DNSSD_COORD_OK;
}

uint32_t dnssd_coord_owner_removed(struct dnssd_coordinator *coordinator,
                                   struct dnssd_coord_owner owner)
{
    uint32_t index, removed = 0;

    for (index = 0; index < DNSSD_COORD_SESSIONS; ++index) {
        struct dnssd_coord_session *session = &coordinator->sessions[index];
        if (session->active && same_owner(session->owner, owner)) {
            removed += clear_session_operations(coordinator,
                                                (uint16_t)(index + 1u));
            session->notification_target = 0;
            session->active = 0;
            ++removed;
        }
    }
    return removed;
}

int dnssd_coord_push(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_handle operation_handle,
                     uint32_t event)
{
    struct dnssd_coord_operation *operation =
        get_operation(coordinator, operation_handle);
    uint8_t tail;

    if (operation == NULL)
        return DNSSD_COORD_STALE;
    if (operation->lost)
        return DNSSD_COORD_LOST;
    if (operation->count == DNSSD_COORD_EVENT_QUEUE) {
        operation->head = 0;
        operation->count = 0;
        operation->lost = 1;
        return DNSSD_COORD_LOST;
    }
    tail = (uint8_t)((operation->head + operation->count) %
                     DNSSD_COORD_EVENT_QUEUE);
    operation->events[tail] = event;
    ++operation->count;
    return DNSSD_COORD_OK;
}

int dnssd_coord_read(struct dnssd_coordinator *coordinator,
                     struct dnssd_coord_handle session_handle,
                     struct dnssd_coord_owner owner,
                     struct dnssd_coord_handle operation_handle,
                     uint32_t *event)
{
    struct dnssd_coord_operation *operation;

    if (event == NULL ||
        get_session(coordinator, session_handle, owner) == NULL)
        return DNSSD_COORD_STALE;
    operation = get_operation(coordinator, operation_handle);
    if (operation == NULL || operation->session_slot != session_handle.slot)
        return DNSSD_COORD_STALE;
    if (operation->lost) {
        operation->lost = 0;
        return DNSSD_COORD_LOST;
    }
    if (operation->count == 0)
        return DNSSD_COORD_EMPTY;
    *event = operation->events[operation->head];
    operation->head = (uint8_t)((operation->head + 1u) %
                                DNSSD_COORD_EVENT_QUEUE);
    --operation->count;
    return DNSSD_COORD_OK;
}

void dnssd_coord_get_stats(const struct dnssd_coordinator *coordinator,
                           struct dnssd_coord_stats *stats)
{
    uint32_t index;

    memset(stats, 0, sizeof(*stats));
    for (index = 0; index < DNSSD_COORD_SESSIONS; ++index) {
        const struct dnssd_coord_session *session =
            &coordinator->sessions[index];
        if (session->active) {
            ++stats->sessions;
            if (session->notification_target != 0)
                ++stats->notification_targets;
        }
    }
    for (index = 0; index < DNSSD_COORD_OPERATIONS; ++index) {
        const struct dnssd_coord_operation *operation =
            &coordinator->operations[index];
        if (operation->active) {
            ++stats->operations;
            if (operation->lost)
                ++stats->lost_queues;
        }
    }
}
