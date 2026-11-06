#include "midici/midici.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 8

struct fixture {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    struct midici_context context;
    unsigned ready;
    int go;
    enum midici_result results[CLIENTS];
    uint8_t request_ids[CLIENTS];
};

struct client {
    struct fixture *fixture;
    unsigned number;
};

static void *run_client(void *argument)
{
    struct client *client = argument;
    struct fixture *fixture = client->fixture;

    pthread_mutex_lock(&fixture->mutex);
    ++fixture->ready;
    pthread_cond_broadcast(&fixture->condition);
    while (!fixture->go)
        pthread_cond_wait(&fixture->condition, &fixture->mutex);

    /* This mutex models the coordinator task's single serialized engine. */
    fixture->results[client->number] = midici_transaction_begin(
        &fixture->context, 0x0123456u, 0x34u, 100u, 3000u,
        &fixture->request_ids[client->number]);
    pthread_mutex_unlock(&fixture->mutex);
    return NULL;
}

int main(void)
{
    struct fixture fixture;
    struct client clients[CLIENTS];
    pthread_t threads[CLIENTS];
    unsigned i, accepted = 0, busy = 0;
    int failed = 0;

    memset(&fixture, 0, sizeof(fixture));
    if (pthread_mutex_init(&fixture.mutex, NULL) != 0 ||
        pthread_cond_init(&fixture.condition, NULL) != 0 ||
        midici_context_init(&fixture.context, 0x2468ace0u) != MIDICI_OK) {
        fprintf(stderr, "FAIL initialize concurrency fixture\n");
        return 1;
    }
    for (i = 0; i < CLIENTS; ++i) {
        clients[i].fixture = &fixture;
        clients[i].number = i;
        if (pthread_create(&threads[i], NULL, run_client, &clients[i]) != 0) {
            fprintf(stderr, "FAIL create client %u\n", i);
            return 1;
        }
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
        if (fixture.results[i] == MIDICI_OK)
            ++accepted;
        else if (fixture.results[i] == MIDICI_BUSY)
            ++busy;
        else
            failed = 1;
    }
    if (accepted != 1u || busy != CLIENTS - 1u)
        failed = 1;

    midici_context_cancel_remote(&fixture.context, 0x0123456u);
    pthread_cond_destroy(&fixture.condition);
    pthread_mutex_destroy(&fixture.mutex);
    if (failed)
        fprintf(stderr, "FAIL concurrency accepted=%u busy=%u\n", accepted, busy);
    else
        printf("midici concurrency: one owner, %u busy client(s)\n", busy);
    return failed;
}
