#include "../prototypes/camd/endpoint_registry.h"
#include "../prototypes/camd/native_event_queue.h"
#include "../prototypes/camd/provider_contract.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x43414d44u, 0, 0, value } };
    return id;
}

struct registry_provider_context {
    struct CAMDEndpointRegistry *registry;
    struct CAMDHandleV1 provider;
    unsigned int shutdown_calls;
    unsigned int ready_calls;
    unsigned int snapshot_reentries;
    unsigned int opens;
    unsigned int closes;
    unsigned int midi1_sends;
    unsigned int sysex_sends;
    unsigned int ump_sends;
    unsigned int receive_starts;
    unsigned int receive_stops;
    unsigned int controls;
    unsigned int received_ump;
    enum CAMDRegistryResult reentrant_retire_result;
    const struct CAMDProviderReceiveSinkV1 *active_sink;
    int reenter_retire_on_send;
    int short_capacity;
    int ready;
};

struct registry_provider_session {
    struct registry_provider_context *provider;
    struct CAMDNativeEventQueue *queue;
    struct CAMDMIDI1EventV1 *midi1_scratch;
    struct CAMDUMPEventV1 *ump_scratch;
    uint8_t *sysex_scratch;
    uint32_t capacity;
    uint32_t max_sysex_bytes;
    uint32_t format;
};

static pthread_mutex_t receive_test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t receive_test_condition = PTHREAD_COND_INITIALIZER;
static int receive_test_block;
static int receive_test_entered;
static int receive_test_release;

static void provider_reenter_snapshot(
    struct registry_provider_context *context);

static enum CAMDProviderResult registry_provider_open(
    void *context, const struct CAMDProviderOpenRequestV1 *request,
    struct CAMDProviderOpenResultV1 *result)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session;
    struct CAMDNativeQueueConfigV1 config;

    assert(request != NULL);
    session = calloc(1, sizeof(*session));
    if (!session)
        return CAMD_PROVIDER_CALLBACK_FAILED;
    session->provider = provider;
    session->capacity = request->QueueCapacity;
    session->format = request->DataFormat;
    session->max_sysex_bytes = request->DataFormat == CAMD_PROVIDER_FORMAT_MIDI1
                                   ? 64
                                   : 0;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.DataFormat = request->DataFormat;
    config.Capacity = request->QueueCapacity;
    config.MaxSysExBytes = session->max_sysex_bytes;
    if (camd_native_queue_create(&config, &session->queue) !=
        CAMD_NATIVE_QUEUE_OK)
        goto fail;
    if (request->DataFormat == CAMD_PROVIDER_FORMAT_MIDI1) {
        session->midi1_scratch = calloc(request->QueueCapacity,
                                         sizeof(*session->midi1_scratch));
        session->sysex_scratch = malloc(session->max_sysex_bytes);
        if (!session->midi1_scratch || !session->sysex_scratch)
            goto fail;
    } else {
        session->ump_scratch = calloc(request->QueueCapacity,
                                      sizeof(*session->ump_scratch));
        if (!session->ump_scratch)
            goto fail;
    }
    ++provider->opens;
    provider_reenter_snapshot(provider);
    result->SessionContext = session;
    result->EffectiveQueueCapacity = provider->short_capacity
                                         ? request->QueueCapacity - 1
                                         : request->QueueCapacity;
    result->MaxSysExBytes = session->max_sysex_bytes;
    return CAMD_PROVIDER_OK;

fail:
    camd_native_queue_destroy(session->queue);
    free(session->midi1_scratch);
    free(session->ump_scratch);
    free(session->sysex_scratch);
    free(session);
    return CAMD_PROVIDER_CALLBACK_FAILED;
}

static enum CAMDProviderResult registry_provider_close(void *context,
                                                        void *session_context)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;

    assert(session != NULL && session->provider == provider);
    ++provider->closes;
    provider_reenter_snapshot(provider);
    camd_native_queue_destroy(session->queue);
    free(session->midi1_scratch);
    free(session->ump_scratch);
    free(session->sysex_scratch);
    free(session);
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult queue_provider_result(
    enum CAMDNativeQueueResult result)
{
    if (result == CAMD_NATIVE_QUEUE_OK)
        return CAMD_PROVIDER_OK;
    if (result == CAMD_NATIVE_QUEUE_FULL)
        return CAMD_PROVIDER_QUEUE_FULL;
    if (result == CAMD_NATIVE_QUEUE_TOO_LARGE)
        return CAMD_PROVIDER_TOO_LARGE;
    return CAMD_PROVIDER_CALLBACK_FAILED;
}

static enum CAMDProviderResult registry_provider_send_ump(
    void *context, void *session_context,
    const struct CAMDUMPEventV1 *events, size_t event_count)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;
    enum CAMDNativeQueueResult queue_result;

    assert(session != NULL && session->provider == provider);
    assert(events != NULL && event_count == 1);
    ++provider->ump_sends;
    provider_reenter_snapshot(provider);
    if (provider->reenter_retire_on_send)
        provider->reentrant_retire_result =
            camd_registry_provider_begin_retire(provider->registry,
                                                provider->provider);
    queue_result = camd_native_queue_enqueue_ump(session->queue, events,
                                                  event_count);
    return queue_provider_result(queue_result);
}

static enum CAMDProviderResult registry_provider_send_midi1(
    void *context, void *session_context,
    const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;

    assert(session != NULL && session->provider == provider);
    assert(events != NULL && event_count == 1);
    ++provider->midi1_sends;
    provider_reenter_snapshot(provider);
    return queue_provider_result(camd_native_queue_enqueue_midi1(
        session->queue, events, event_count));
}

static enum CAMDProviderResult registry_provider_send_sysex(
    void *context, void *session_context, const uint8_t *bytes,
    size_t byte_count, uint32_t time_high, uint32_t time_low,
    uint32_t clock_domain, uint32_t flags)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;

    assert(session != NULL && session->provider == provider);
    assert(bytes != NULL && byte_count >= 2);
    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    ++provider->sysex_sends;
    provider_reenter_snapshot(provider);
    return queue_provider_result(camd_native_queue_enqueue_midi1_sysex(
        session->queue, bytes, byte_count, time_high, time_low, clock_domain,
        flags));
}

static enum CAMDProviderResult registry_provider_drain(void *context,
                                                        void *session_context)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;
    struct CAMDNativeQueueHeadV1 head;

    assert(session != NULL && session->provider == provider);
    ++provider->controls;
    provider_reenter_snapshot(provider);
    memset(&head, 0, sizeof(head));
    head.Size = sizeof(head);
    head.Version = 1;
    while (camd_native_queue_peek(session->queue, &head) ==
           CAMD_NATIVE_QUEUE_OK) {
        size_t count = 0;

        if (head.Kind == CAMD_NATIVE_QUEUE_ITEM_MIDI1) {
            assert(camd_native_queue_dequeue_midi1(
                       session->queue, session->midi1_scratch,
                       session->capacity, &count) == CAMD_NATIVE_QUEUE_OK);
        } else if (head.Kind == CAMD_NATIVE_QUEUE_ITEM_MIDI1_SYSEX) {
            struct CAMDNativeQueueSysExInfoV1 info;

            memset(&info, 0, sizeof(info));
            info.Size = sizeof(info);
            info.Version = 1;
            assert(camd_native_queue_dequeue_midi1_sysex(
                       session->queue, session->sysex_scratch,
                       session->max_sysex_bytes, &info) ==
                   CAMD_NATIVE_QUEUE_OK);
        } else {
            assert(head.Kind == CAMD_NATIVE_QUEUE_ITEM_UMP);
            assert(camd_native_queue_dequeue_ump(
                       session->queue, session->ump_scratch,
                       session->capacity, &count) == CAMD_NATIVE_QUEUE_OK);
        }
    }
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult registry_provider_cancel(void *context,
                                                         void *session_context)
{
    struct registry_provider_context *provider = context;
    struct registry_provider_session *session = session_context;
    size_t cancelled;

    assert(session != NULL && session->provider == provider);
    ++provider->controls;
    provider_reenter_snapshot(provider);
    assert(camd_native_queue_cancel(session->queue, &cancelled) ==
           CAMD_NATIVE_QUEUE_OK);
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult registry_provider_receive_ump(
    void *argument, const struct CAMDUMPEventV1 *events, size_t event_count)
{
    struct registry_provider_context *context = argument;

    assert(events != NULL && event_count == 1);
    ++context->received_ump;
    pthread_mutex_lock(&receive_test_lock);
    if (receive_test_block) {
        receive_test_entered = 1;
        pthread_cond_broadcast(&receive_test_condition);
        while (!receive_test_release)
            pthread_cond_wait(&receive_test_condition, &receive_test_lock);
    }
    pthread_mutex_unlock(&receive_test_lock);
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult registry_provider_start_receive(
    void *argument, void *session_context,
    const struct CAMDProviderReceiveSinkV1 *sink)
{
    struct registry_provider_context *context = argument;
    struct registry_provider_session *session = session_context;
    struct CAMDUMPEventV1 event, delivered;
    size_t count;

    assert(session != NULL && session->provider == context);
    assert(sink != NULL && sink->SubmitUMP != NULL);
    assert(sink->SubmitMIDI1 == NULL && sink->SubmitMIDI1SysEx == NULL);
    ++context->receive_starts;
    context->active_sink = sink;
    provider_reenter_snapshot(context);
    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = 1;
    event.Words[0] = 0x20903c64u;
    assert(camd_native_queue_enqueue_ump(session->queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_dequeue_ump(session->queue, &delivered, 1,
                                         &count) == CAMD_NATIVE_QUEUE_OK);
    assert(count == 1);
    return sink->SubmitUMP(sink->Context, &delivered, 1);
}

static enum CAMDProviderResult registry_provider_stop_receive(
    void *argument, void *session_context)
{
    struct registry_provider_context *context = argument;
    struct registry_provider_session *session = session_context;

    assert(session != NULL && session->provider == context);
    ++context->receive_stops;
    context->active_sink = NULL;
    provider_reenter_snapshot(context);
    return CAMD_PROVIDER_OK;
}

struct receive_thread_context {
    const struct CAMDProviderReceiveSinkV1 *sink;
    enum CAMDProviderResult result;
};

static void *submit_receive_thread(void *argument)
{
    struct receive_thread_context *context = argument;
    struct CAMDUMPEventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = 1;
    event.Words[0] = 0x20903c64u;
    context->result = context->sink->SubmitUMP(context->sink->Context,
                                               &event, 1);
    return NULL;
}

static void provider_reenter_snapshot(struct registry_provider_context *context)
{
    struct CAMDEndpointSnapshot *snapshot = NULL;

    assert(camd_registry_snapshot(context->registry, &snapshot) ==
           CAMD_REGISTRY_OK);
    camd_snapshot_destroy(snapshot);
    ++context->snapshot_reentries;
}

static enum CAMDProviderResult registry_provider_shutdown(void *argument)
{
    struct registry_provider_context *context = argument;

    ++context->shutdown_calls;
    provider_reenter_snapshot(context);
    return CAMD_PROVIDER_OK;
}

static int registry_provider_ready(void *argument)
{
    struct registry_provider_context *context = argument;

    ++context->ready_calls;
    provider_reenter_snapshot(context);
    return context->ready;
}

static struct CAMDHandleV1 register_test_provider(
    struct CAMDEndpointRegistry *registry,
    struct registry_provider_context *context)
{
    struct CAMDProviderOpsV1 operations;
    struct CAMDProviderDescriptorV1 descriptor;
    struct CAMDHandleV1 provider;

    memset(context, 0, sizeof(*context));
    context->registry = registry;
    context->ready = 1;
    memset(&operations, 0, sizeof(operations));
    operations.Size = sizeof(operations);
    operations.Version = 1;
    operations.Open = registry_provider_open;
    operations.Close = registry_provider_close;
    operations.SendMIDI1 = registry_provider_send_midi1;
    operations.SendMIDI1SysEx = registry_provider_send_sysex;
    operations.SendUMP = registry_provider_send_ump;
    operations.StartReceive = registry_provider_start_receive;
    operations.StopReceive = registry_provider_stop_receive;
    operations.Drain = registry_provider_drain;
    operations.Cancel = registry_provider_cancel;
    operations.BeginShutdown = registry_provider_shutdown;
    operations.ShutdownReady = registry_provider_ready;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(0x1000u);
    descriptor.NativePaths = CAMD_PROVIDER_PATH_ALL;
    descriptor.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    descriptor.Context = context;
    descriptor.Ops = &operations;
    assert(camd_registry_provider_register(registry, &descriptor, &provider) ==
           CAMD_REGISTRY_OK);
    context->provider = provider;
    return provider;
}

static void retire_test_provider(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider,
    struct registry_provider_context *context)
{
    assert(camd_registry_provider_begin_retire(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(context->shutdown_calls == 1);
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(context->ready_calls == 1);
}

static struct CAMDEndpointInfoV1 make_endpoint(uint32_t value,
                                               const char *name)
{
    struct CAMDEndpointInfoV1 endpoint;

    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.Size = sizeof(endpoint);
    endpoint.Version = 1;
    endpoint.ID = make_id(value);
    endpoint.ProviderID = make_id(0x1000u);
    endpoint.State = CAMD_ENDPOINT_REGISTERED;
    endpoint.IdentityKind = 1;
    endpoint.NativeDataFormats = CAMD_DATA_FORMAT_UMP;
    strncpy(endpoint.Name, name, sizeof(endpoint.Name) - 1);
    strncpy(endpoint.ProductInstance, "test-instance",
            sizeof(endpoint.ProductInstance) - 1);
    strncpy(endpoint.Transport, "software", sizeof(endpoint.Transport) - 1);
    return endpoint;
}

static struct CAMDGroupInfoV1 make_group(
    const struct CAMDEndpointInfoV1 *endpoint, uint32_t group)
{
    struct CAMDGroupInfoV1 info;

    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    info.Version = 1;
    info.EndpointID = endpoint->ID;
    info.Group = group;
    strcpy(info.Name, "Group");
    return info;
}

static struct CAMDFunctionBlockInfoV1 make_block(
    const struct CAMDEndpointInfoV1 *endpoint, uint32_t number,
    uint32_t first_group, uint32_t group_count)
{
    struct CAMDFunctionBlockInfoV1 info;

    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    info.Version = 1;
    info.EndpointID = endpoint->ID;
    info.Number = number;
    info.FirstGroup = first_group;
    info.GroupCount = group_count;
    strcpy(info.Name, "Function Block");
    return info;
}

static const struct CAMDEndpointInfoV1 *snapshot_first(
    const struct CAMDEndpointSnapshot *snapshot)
{
    const struct CAMDEndpointInfoV1 *endpoint;
    const struct CAMDGroupInfoV1 *groups;
    const struct CAMDFunctionBlockInfoV1 *blocks;
    size_t group_count, block_count;

    assert(camd_snapshot_endpoint(snapshot, 0, &endpoint, &groups,
                                  &group_count, &blocks,
                                  &block_count) == CAMD_REGISTRY_OK);
    assert(group_count == 1);
    assert(block_count == 1);
    assert(groups[0].Group == 0);
    assert(blocks[0].FirstGroup == 0);
    return endpoint;
}

struct concurrent_context {
    struct CAMDEndpointRegistry *registry;
    struct CAMDEndpointIDV1 id;
    struct CAMDHandleV1 provider;
    int failed;
};

static void *concurrent_reader(void *argument)
{
    struct concurrent_context *context = argument;
    unsigned int i;

    for (i = 0; i < 5000 && !context->failed; ++i) {
        struct CAMDEndpointSnapshot *snapshot = NULL;
        struct CAMDHandleV1 lease;
        const struct CAMDEndpointInfoV1 *endpoint;
        const struct CAMDGroupInfoV1 *groups;
        const struct CAMDFunctionBlockInfoV1 *blocks;
        size_t group_count, block_count;

        if (camd_registry_acquire(context->registry, &context->id, &lease) !=
                CAMD_REGISTRY_OK ||
            camd_registry_snapshot(context->registry, &snapshot) !=
                CAMD_REGISTRY_OK ||
            camd_snapshot_endpoint_count(snapshot) != 1 ||
            camd_snapshot_endpoint(snapshot, 0, &endpoint, &groups,
                                   &group_count, &blocks, &block_count) !=
                CAMD_REGISTRY_OK ||
            memcmp(&endpoint->ID, &context->id, sizeof(context->id)) != 0 ||
            group_count != 1 ||
            block_count != 1 || groups[0].Group != 0 ||
            blocks[0].FirstGroup != 0 ||
            camd_registry_release(context->registry, lease) !=
                CAMD_REGISTRY_OK)
            context->failed = 1;
        camd_snapshot_destroy(snapshot);
    }
    return NULL;
}

static void *concurrent_writer(void *argument)
{
    struct concurrent_context *context = argument;
    unsigned int i;

    for (i = 0; i < 5000 && !context->failed; ++i) {
        if (camd_registry_set_state(context->registry, context->provider,
                                    CAMD_ENDPOINT_OFFLINE) != CAMD_REGISTRY_OK ||
            camd_registry_set_state(context->registry, context->provider,
                                    CAMD_ENDPOINT_AVAILABLE) != CAMD_REGISTRY_OK)
            context->failed = 1;
    }
    return NULL;
}

static void test_concurrency(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct registry_provider_context provider_context;
    struct CAMDHandleV1 provider_owner;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(3, "Concurrent");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct concurrent_context contexts[5];
    pthread_t threads[5];
    size_t i;

    assert(registry != NULL);
    provider_owner = register_test_provider(registry, &provider_context);
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &contexts[0].provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, contexts[0].provider,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, contexts[0].provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    for (i = 0; i < 5; ++i) {
        contexts[i].registry = registry;
        contexts[i].id = endpoint.ID;
        contexts[i].provider = contexts[0].provider;
        contexts[i].failed = 0;
    }
    for (i = 0; i < 4; ++i)
        assert(pthread_create(&threads[i], NULL, concurrent_reader,
                              &contexts[i]) == 0);
    assert(pthread_create(&threads[4], NULL, concurrent_writer,
                          &contexts[4]) == 0);
    for (i = 0; i < 5; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(!contexts[i].failed);
    }
    assert(camd_registry_retire(registry, contexts[0].provider) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, contexts[0].provider) ==
           CAMD_REGISTRY_OK);
    retire_test_provider(registry, provider_owner, &provider_context);
    camd_registry_destroy(registry);
}

static void expect_watch_event(struct CAMDEndpointWatch *watch,
                               uint32_t type,
                               const struct CAMDEndpointIDV1 *id,
                               struct CAMDGenerationV1 *previous)
{
    struct CAMDEndpointWatchEventV1 event;

    assert(camd_endpoint_watch_read(watch, &event) == CAMD_REGISTRY_OK);
    assert(event.Size == sizeof(event));
    assert(event.Version == 1);
    assert(event.Type == type);
    assert(event.Generation.high > previous->high ||
           (event.Generation.high == previous->high &&
            event.Generation.low > previous->low));
    if (type == CAMD_ENDPOINT_EVENT_LOST) {
        struct CAMDEndpointIDV1 zero = { { 0, 0, 0, 0 } };
        assert(memcmp(&event.EndpointID, &zero, sizeof(zero)) == 0);
    } else {
        assert(id != NULL);
        assert(memcmp(&event.EndpointID, id, sizeof(*id)) == 0);
    }
    *previous = event.Generation;
}

static void count_notify(void *context)
{
    ++*(unsigned int *)context;
}

static void test_watches(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct registry_provider_context provider_context;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(4, "Watched");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct CAMDEndpointWatch *watch;
    struct CAMDEndpointSnapshot *snapshot;
    struct CAMDHandleV1 provider_owner, provider, client;
    struct CAMDGenerationV1 generation;
    struct CAMDEndpointWatchEventV1 event;
    unsigned int notified = 0;

    assert(registry != NULL);
    provider_owner = register_test_provider(registry, &provider_context);
    assert(sizeof(struct CAMDEndpointWatchEventV1) == 36);
    assert(camd_registry_watch_start(registry, 8, &watch, &generation) ==
           CAMD_REGISTRY_OK);
    assert(generation.high == 0 && generation.low == 0);
    assert(camd_endpoint_watch_read(watch, &event) == CAMD_REGISTRY_EMPTY);
    assert(camd_endpoint_watch_set_notify(watch, count_notify,
                                          &notified) == CAMD_REGISTRY_OK);

    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_OK);
    assert(notified == 1);
    assert(camd_endpoint_watch_set_notify(watch, NULL, NULL) ==
           CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_ADDED, &endpoint.ID,
                       &generation);
    strcpy(endpoint.Name, "Watched update");
    assert(camd_registry_replace(registry, provider, &endpoint, &group, 1,
                                 &block, 1) == CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_UPDATED, &endpoint.ID,
                       &generation);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_UPDATED, &endpoint.ID,
                       &generation);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_UPDATED, &endpoint.ID,
                       &generation);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_OFFLINE) == CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_OFFLINE, &endpoint.ID,
                       &generation);
    assert(camd_registry_acquire(registry, &endpoint.ID, &client) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_retire(registry, provider) == CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_RETIRED, &endpoint.ID,
                       &generation);
    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(snapshot) == 0);
    camd_snapshot_destroy(snapshot);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_OK);
    camd_endpoint_watch_end(watch);

    endpoint = make_endpoint(5, "Overflow");
    group = make_group(&endpoint, 0);
    block = make_block(&endpoint, 0, 0, 1);
    assert(camd_registry_watch_start(registry, 2, &watch, &generation) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_LOST, NULL, &generation);
    assert(camd_endpoint_watch_read(watch, &event) == CAMD_REGISTRY_EMPTY);
    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(snapshot) == 1);
    assert(camd_snapshot_generation(snapshot).high > generation.high ||
           (camd_snapshot_generation(snapshot).high == generation.high &&
            camd_snapshot_generation(snapshot).low >= generation.low));
    camd_snapshot_destroy(snapshot);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_OFFLINE) == CAMD_REGISTRY_OK);
    expect_watch_event(watch, CAMD_ENDPOINT_EVENT_OFFLINE, &endpoint.ID,
                       &generation);
    camd_endpoint_watch_end(watch);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_retire(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_OK);
    retire_test_provider(registry, provider_owner, &provider_context);
    camd_registry_destroy(registry);
}

static void test_provider_lifecycle(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct registry_provider_context context;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(6, "Provider owned");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct CAMDEndpointSnapshot *snapshot;
    struct CAMDHandleV1 provider, replacement, endpoint_owner, client;

    assert(registry != NULL);
    provider = register_test_provider(registry, &context);
    assert(camd_registry_publish(registry, provider, &endpoint, &group, 1,
                                 &block, 1, &endpoint_owner) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_acquire(registry, &endpoint.ID, &client) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_STATE);
    assert(camd_registry_provider_begin_retire(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(context.shutdown_calls == 1);
    assert(context.snapshot_reentries == 1);
    assert(camd_registry_acquire(registry, &endpoint.ID, &client) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(snapshot) == 0);
    camd_snapshot_destroy(snapshot);
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_BUSY);
    assert(context.ready_calls == 0);
    assert(camd_registry_release(registry, endpoint_owner) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_OK);

    context.ready = 0;
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_BUSY);
    assert(context.ready_calls == 1);
    context.ready = 1;
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(context.ready_calls == 2);
    assert(context.snapshot_reentries == 3);
    assert(camd_registry_provider_begin_retire(registry, provider) ==
           CAMD_REGISTRY_STALE);
    replacement = register_test_provider(registry, &context);
    assert(replacement.slot == provider.slot);
    assert(replacement.generation != provider.generation);
    retire_test_provider(registry, replacement, &context);
    camd_registry_destroy(registry);
}

static void test_registry_sessions(void)
{
    static const uint8_t sysex[] = { 0xf0, 0x7d, 0x01, 0xf7 };
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct registry_provider_context context;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(7, "Session endpoint");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDRegistrySessionInfoV1 session_info;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDMIDI1EventV1 midi1_event;
    struct CAMDUMPEventV1 event;
    struct CAMDHandleV1 provider, endpoint_owner, output, input, midi1;
    struct receive_thread_context receive_thread;
    pthread_t thread;
    uint8_t oversized_sysex[65];
    unsigned int i;

    assert(registry != NULL);
    endpoint.NativeDataFormats = CAMD_DATA_FORMAT_ALL;
    provider = register_test_provider(registry, &context);
    assert(camd_registry_publish(registry, provider, &endpoint, &group, 1,
                                 &block, 1, &endpoint_owner) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, endpoint_owner,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, endpoint_owner,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = endpoint.ID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI2;
    request.QueueCapacity = 8;
    context.short_capacity = 1;
    assert(camd_registry_session_open(registry, &request, &output) ==
           CAMD_REGISTRY_CALLBACK_FAILED);
    assert(context.opens == 1 && context.closes == 1);
    context.short_capacity = 0;
    assert(camd_registry_session_open(registry, &request, &output) ==
           CAMD_REGISTRY_OK);
    assert(context.opens == 2);
    memset(&session_info, 0, sizeof(session_info));
    session_info.Size = sizeof(session_info);
    session_info.Version = 1;
    assert(camd_registry_session_info(registry, output, &session_info) ==
           CAMD_REGISTRY_OK);
    assert(session_info.Direction == CAMD_PROVIDER_DIRECTION_OUTPUT);
    assert(session_info.DataFormat == CAMD_PROVIDER_FORMAT_UMP);
    assert(session_info.Protocol == CAMD_PROVIDER_PROTOCOL_MIDI2);
    assert(session_info.RequestedQueueCapacity == 8);
    assert(session_info.EffectiveQueueCapacity == 8);
    assert(session_info.MaxSysExBytes == 0);

    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    assert(camd_registry_session_open(registry, &request, &midi1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_info(registry, midi1, &session_info) ==
           CAMD_REGISTRY_OK);
    assert(session_info.DataFormat == CAMD_PROVIDER_FORMAT_MIDI1);
    assert(session_info.EffectiveQueueCapacity == 8);
    assert(session_info.MaxSysExBytes == 64);
    memset(&midi1_event, 0, sizeof(midi1_event));
    midi1_event.Size = sizeof(midi1_event);
    midi1_event.Version = 1;
    midi1_event.Length = 3;
    midi1_event.Bytes[0] = 0x90;
    midi1_event.Bytes[1] = 60;
    midi1_event.Bytes[2] = 100;
    assert(camd_registry_session_send_midi1(registry, midi1, &midi1_event, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1_sysex(
               registry, midi1, sysex, sizeof(sysex), 0, 1, 2, 0) ==
           CAMD_REGISTRY_OK);
    assert(context.midi1_sends == 1 && context.sysex_sends == 1);
    memset(oversized_sysex, 0x01, sizeof(oversized_sysex));
    oversized_sysex[0] = 0xf0;
    oversized_sysex[sizeof(oversized_sysex) - 1] = 0xf7;
    assert(camd_registry_session_send_midi1_sysex(
               registry, midi1, oversized_sysex, sizeof(oversized_sysex),
               0, 1, 2, 0) == CAMD_REGISTRY_TOO_LARGE);

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = 1;
    event.Words[0] = 0x20903c64u;
    assert(camd_registry_session_send_ump(registry, midi1, &event, 1) ==
           CAMD_REGISTRY_UNSUPPORTED);
    assert(context.ump_sends == 0);
    assert(camd_registry_session_close(registry, midi1) == CAMD_REGISTRY_OK);
    request.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI2;
    context.reenter_retire_on_send = 1;
    assert(camd_registry_session_send_ump(registry, output, &event, 1) ==
           CAMD_REGISTRY_OK);
    assert(context.ump_sends == 1);
    assert(context.reentrant_retire_result == CAMD_REGISTRY_BUSY);
    context.reenter_retire_on_send = 0;
    for (i = 1; i < request.QueueCapacity; ++i)
        assert(camd_registry_session_send_ump(registry, output, &event, 1) ==
               CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_ump(registry, output, &event, 1) ==
           CAMD_REGISTRY_QUEUE_FULL);
    assert(camd_registry_session_drain(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_ump(registry, output, &event, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_cancel(registry, output) == CAMD_REGISTRY_OK);
    assert(context.controls == 2);

    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_registry_session_open(registry, &request, &input) ==
           CAMD_REGISTRY_OK);
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &context;
    sink.SubmitUMP = registry_provider_receive_ump;
    assert(camd_registry_session_start_receive(registry, input, &sink) ==
           CAMD_REGISTRY_OK);
    assert(context.receive_starts == 1 && context.received_ump == 1);
    assert(camd_registry_session_close(registry, input) == CAMD_REGISTRY_STATE);

    pthread_mutex_lock(&receive_test_lock);
    receive_test_block = 1;
    receive_test_entered = 0;
    receive_test_release = 0;
    pthread_mutex_unlock(&receive_test_lock);
    receive_thread.sink = context.active_sink;
    receive_thread.result = CAMD_PROVIDER_STATE;
    assert(pthread_create(&thread, NULL, submit_receive_thread,
                          &receive_thread) == 0);
    pthread_mutex_lock(&receive_test_lock);
    while (!receive_test_entered)
        pthread_cond_wait(&receive_test_condition, &receive_test_lock);
    pthread_mutex_unlock(&receive_test_lock);
    assert(camd_registry_provider_begin_retire(registry, provider) ==
           CAMD_REGISTRY_BUSY);
    assert(camd_registry_session_stop_receive(registry, input) ==
           CAMD_REGISTRY_BUSY);
    pthread_mutex_lock(&receive_test_lock);
    receive_test_release = 1;
    pthread_cond_broadcast(&receive_test_condition);
    pthread_mutex_unlock(&receive_test_lock);
    assert(pthread_join(thread, NULL) == 0);
    assert(receive_thread.result == CAMD_PROVIDER_OK);
    assert(camd_registry_session_stop_receive(registry, input) ==
           CAMD_REGISTRY_OK);
    pthread_mutex_lock(&receive_test_lock);
    receive_test_block = 0;
    pthread_mutex_unlock(&receive_test_lock);
    assert(context.receive_stops == 1);
    assert(context.received_ump == 2);
    assert(camd_registry_session_close(registry, input) == CAMD_REGISTRY_OK);

    assert(camd_registry_provider_begin_retire(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_ump(registry, output, &event, 1) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_BUSY);
    assert(camd_registry_session_drain(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_cancel(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, output) == CAMD_REGISTRY_STALE);
    assert(context.closes == 4);
    assert(camd_registry_release(registry, endpoint_owner) == CAMD_REGISTRY_OK);
    assert(camd_registry_provider_release(registry, provider) ==
           CAMD_REGISTRY_OK);
    assert(context.snapshot_reentries >= 10);
    camd_registry_destroy(registry);
}

int main(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct registry_provider_context provider_context;
    struct CAMDEndpointSnapshot *before, *after;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(1, "Original");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct CAMDHandleV1 provider_owner, provider, client, replacement;
    struct CAMDGenerationV1 generation;
    const struct CAMDEndpointInfoV1 *copy;

    assert(registry != NULL);
    assert(sizeof(struct CAMDHandleV1) == 8);
    assert(sizeof(struct CAMDEndpointInfoV1) == 360);
    assert(sizeof(struct CAMDGroupInfoV1) == 100);
    assert(sizeof(struct CAMDFunctionBlockInfoV1) == 104);
    provider_owner = register_test_provider(registry, &provider_context);

    replacement = provider_owner;
    ++replacement.generation;
    assert(camd_registry_publish(registry, replacement, &endpoint, &group, 1,
                                 &block, 1, &provider) == CAMD_REGISTRY_STALE);
    endpoint.ProviderID = make_id(0x1001u);
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_INVALID);
    endpoint.ProviderID = make_id(0x1000u);
    endpoint.NativeDataFormats = 0;
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_INVALID);
    endpoint.NativeDataFormats = CAMD_DATA_FORMAT_UMP;
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &replacement) == CAMD_REGISTRY_DUPLICATE);
    assert(camd_registry_snapshot(registry, &before) == CAMD_REGISTRY_OK);
    generation = camd_snapshot_generation(before);
    copy = snapshot_first(before);
    assert(strcmp(copy->Name, "Original") == 0);

    endpoint = *copy;
    strcpy(endpoint.Name, "Updated");
    group = make_group(&endpoint, 0);
    block = make_block(&endpoint, 0, 0, 1);
    assert(camd_registry_replace(registry, provider, &endpoint, &group, 1,
                                 &block, 1) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(before)->Name, "Original") == 0);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(after)->Name, "Updated") == 0);
    assert(camd_snapshot_generation(after).low > generation.low);
    generation = camd_snapshot_generation(after);
    camd_snapshot_destroy(after);

    block.GroupCount = 17;
    assert(camd_registry_replace(registry, provider, &endpoint, &group, 1,
                                 &block, 1) == CAMD_REGISTRY_INVALID);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(after)->Name, "Updated") == 0);
    assert(camd_snapshot_generation(after).high == generation.high);
    assert(camd_snapshot_generation(after).low == generation.low);
    camd_snapshot_destroy(after);

    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_STATE);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_acquire(registry, &endpoint.ID, &client) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_retire(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_acquire(registry, &endpoint.ID, &replacement) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(after) == 0);
    camd_snapshot_destroy(after);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_STALE);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(after) == 0);
    camd_snapshot_destroy(after);

    endpoint = make_endpoint(2, "Replacement");
    group = make_group(&endpoint, 0);
    block = make_block(&endpoint, 0, 0, 1);
    assert(camd_registry_publish(registry, provider_owner, &endpoint, &group,
                                 1, &block, 1,
                                 &replacement) == CAMD_REGISTRY_OK);
    assert(replacement.slot == provider.slot);
    assert(replacement.generation != provider.generation);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_STALE);

    assert(camd_registry_retire(registry, replacement) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, replacement) == CAMD_REGISTRY_OK);
    retire_test_provider(registry, provider_owner, &provider_context);
    camd_snapshot_destroy(before);
    camd_registry_destroy(registry);
    test_concurrency();
    test_watches();
    test_provider_lifecycle();
    test_registry_sessions();
    puts("CAMD private endpoint core OK");
    return 0;
}
