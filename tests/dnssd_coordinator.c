#include "coordinator.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 8u

struct fixture {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    struct dnssd_coordinator coordinator;
    unsigned ready;
    int go;
    int results[CLIENTS];
    struct dnssd_coord_owner owners[CLIENTS];
    struct dnssd_coord_handle sessions[CLIENTS];
    struct dnssd_coord_handle operations[CLIENTS];
};

struct client {
    struct fixture *fixture;
    unsigned number;
};

static int failures;

#define CHECK(condition, text) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s\n", text); \
    } \
} while (0)

static void *run_client(void *argument)
{
    struct client *client = argument;
    struct fixture *fixture = client->fixture;
    unsigned number = client->number;
    uint64_t key;
    uint32_t flags;

    pthread_mutex_lock(&fixture->mutex);
    ++fixture->ready;
    pthread_cond_broadcast(&fixture->condition);
    while (!fixture->go)
        pthread_cond_wait(&fixture->condition, &fixture->mutex);

    fixture->results[number] = dnssd_coord_open(
        &fixture->coordinator, fixture->owners[number],
        fixture->owners[number].task, &fixture->sessions[number]);
    if (fixture->results[number] == DNSSD_COORD_OK) {
        /* Four clients contend for key 10.  Two use distinct exclusive keys;
         * two non-exclusive browse-like operations may share key 10. */
        key = number < 4u ? 10u : (number < 6u ? 100u + number : 10u);
        flags = number < 6u ? DNSSD_COORD_EXCLUSIVE : 0u;
        fixture->results[number] = dnssd_coord_begin(
            &fixture->coordinator, fixture->sessions[number],
            fixture->owners[number], number < 6u ? 1u : 2u, key, flags,
            &fixture->operations[number]);
    }
    pthread_mutex_unlock(&fixture->mutex);
    return NULL;
}

int main(void)
{
    struct fixture fixture;
    struct client clients[CLIENTS];
    pthread_t threads[CLIENTS];
    struct dnssd_coord_stats before, after;
    struct dnssd_coord_handle old_session, old_operation;
    struct dnssd_coord_owner replacement = { 0xf00du, 0x900u };
    struct dnssd_coord_handle replacement_session, replacement_operation;
    uint32_t event = 0;
    unsigned i, accepted = 0, busy = 0, first = CLIENTS, second = CLIENTS;

    memset(&fixture, 0, sizeof(fixture));
    CHECK(pthread_mutex_init(&fixture.mutex, NULL) == 0,
          "initialize coordinator mutex");
    CHECK(pthread_cond_init(&fixture.condition, NULL) == 0,
          "initialize coordinator condition");
    dnssd_coord_init(&fixture.coordinator, 0x44u);

    for (i = 0; i < CLIENTS; ++i) {
        clients[i].fixture = &fixture;
        clients[i].number = i;
        fixture.owners[i].task = 0x1000u + i;
        fixture.owners[i].id = 0x200u + i;
        CHECK(pthread_create(&threads[i], NULL, run_client, &clients[i]) == 0,
              "create independent client thread");
    }
    pthread_mutex_lock(&fixture.mutex);
    while (fixture.ready != CLIENTS)
        pthread_cond_wait(&fixture.condition, &fixture.mutex);
    fixture.go = 1;
    pthread_cond_broadcast(&fixture.condition);
    pthread_mutex_unlock(&fixture.mutex);
    for (i = 0; i < CLIENTS; ++i)
        pthread_join(threads[i], NULL);

    for (i = 0; i < CLIENTS; ++i) {
        if (fixture.results[i] == DNSSD_COORD_OK) {
            ++accepted;
            if (first == CLIENTS)
                first = i;
            else if (second == CLIENTS)
                second = i;
        } else if (fixture.results[i] == DNSSD_COORD_BUSY) {
            ++busy;
        }
    }
    CHECK(accepted == 5u && busy == 3u,
          "one exclusive owner wins while independent operations proceed");
    CHECK(first != CLIENTS && second != CLIENTS,
          "at least two independent operations are active");

    CHECK(dnssd_coord_push(&fixture.coordinator,
                           fixture.operations[first], 0xa1u) ==
              DNSSD_COORD_OK &&
          dnssd_coord_push(&fixture.coordinator,
                           fixture.operations[second], 0xb2u) ==
              DNSSD_COORD_OK,
          "independent operation queues accept events");
    CHECK(dnssd_coord_read(&fixture.coordinator, fixture.sessions[first],
                           fixture.owners[first], fixture.operations[first],
                           &event) == DNSSD_COORD_OK && event == 0xa1u,
          "first client drains only its event");
    CHECK(dnssd_coord_read(&fixture.coordinator, fixture.sessions[second],
                           fixture.owners[second], fixture.operations[second],
                           &event) == DNSSD_COORD_OK && event == 0xb2u,
          "second client drains only its event");

    CHECK(dnssd_coord_cancel(&fixture.coordinator, fixture.sessions[first],
                             fixture.owners[second],
                             fixture.operations[first]) == DNSSD_COORD_STALE,
          "another owner cannot cancel an operation");
    CHECK(dnssd_coord_cancel(&fixture.coordinator, fixture.sessions[first],
                             fixture.owners[first],
                             fixture.operations[first]) == DNSSD_COORD_OK &&
          dnssd_coord_cancel(&fixture.coordinator, fixture.sessions[first],
                             fixture.owners[first],
                             fixture.operations[first]) == DNSSD_COORD_STALE,
          "exact cancellation is acknowledged and idempotently stale");

    dnssd_coord_get_stats(&fixture.coordinator, &before);
    CHECK(dnssd_coord_owner_removed(&fixture.coordinator,
                                    fixture.owners[second]) >= 2u,
          "task removal reclaims its session and active operation");
    dnssd_coord_get_stats(&fixture.coordinator, &after);
    CHECK(after.sessions + 1u == before.sessions &&
          after.notification_targets + 1u == before.notification_targets,
          "task removal clears stale notification targets");
    CHECK(dnssd_coord_read(&fixture.coordinator, fixture.sessions[second],
                           fixture.owners[second], fixture.operations[second],
                           &event) == DNSSD_COORD_STALE,
          "dead-owner handles become stale");

    CHECK(dnssd_coord_open(&fixture.coordinator, replacement,
                           replacement.task,
                           &replacement_session) == DNSSD_COORD_OK &&
          dnssd_coord_begin(&fixture.coordinator, replacement_session,
                            replacement, 7u, 0x777u,
                            DNSSD_COORD_EXCLUSIVE,
                            &replacement_operation) == DNSSD_COORD_OK,
          "capacity and ownership recover for a replacement task");
    for (i = 0; i < DNSSD_COORD_EVENT_QUEUE; ++i)
        CHECK(dnssd_coord_push(&fixture.coordinator, replacement_operation,
                               i + 1u) == DNSSD_COORD_OK,
              "bounded queue accepts capacity");
    CHECK(dnssd_coord_push(&fixture.coordinator, replacement_operation,
                           99u) == DNSSD_COORD_LOST &&
          dnssd_coord_read(&fixture.coordinator, replacement_session,
                           replacement, replacement_operation,
                           &event) == DNSSD_COORD_LOST &&
          dnssd_coord_read(&fixture.coordinator, replacement_session,
                           replacement, replacement_operation,
                           &event) == DNSSD_COORD_EMPTY,
          "overflow produces one lost marker and requires resynchronization");

    old_session = replacement_session;
    old_operation = replacement_operation;
    dnssd_coord_restart(&fixture.coordinator, 0x45u);
    CHECK(dnssd_coord_cancel(&fixture.coordinator, old_session, replacement,
                             old_operation) == DNSSD_COORD_STALE,
          "restart rejects old incarnation handles");
    CHECK(dnssd_coord_open(&fixture.coordinator, replacement,
                           replacement.task,
                           &replacement_session) == DNSSD_COORD_OK &&
          dnssd_coord_begin(&fixture.coordinator, replacement_session,
                            replacement, 7u, 0x777u,
                            DNSSD_COORD_EXCLUSIVE,
                            &replacement_operation) == DNSSD_COORD_OK,
          "client can recover after coordinator restart");

    pthread_cond_destroy(&fixture.condition);
    pthread_mutex_destroy(&fixture.mutex);
    if (failures == 0)
        printf("DNS-SD coordinator prototype: %u clients, 0 failures\n",
               CLIENTS);
    return failures != 0;
}
