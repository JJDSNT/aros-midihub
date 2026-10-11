#include "../prototypes/camd/app_endpoint.h"
#include "../prototypes/camd/legacy_driver_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct consumer {
    unsigned int events;
    unsigned int sysex;
    uint8_t last;
    int full;
};

static unsigned int notified;

static void count_notify(void *context)
{
    (void)context;
    ++notified;
}

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x41505045u, 0x4e445054u, 0, value } };

    return id;
}

static struct CAMDMIDI1EventV1 note(uint8_t key)
{
    struct CAMDMIDI1EventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.Length = 3;
    event.Bytes[0] = 0x90;
    event.Bytes[1] = key;
    event.Bytes[2] = 0x40;
    return event;
}

static enum CAMDProviderResult consume_midi1(
    void *context, const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct consumer *consumer = context;

    if (consumer->full)
        return CAMD_PROVIDER_QUEUE_FULL;
    consumer->events += (unsigned int)event_count;
    consumer->last = events[event_count - 1].Bytes[1];
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult consume_sysex(
    void *context, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    struct consumer *consumer = context;

    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    assert(bytes[0] == 0xf0 && bytes[byte_count - 1] == 0xf7);
    ++consumer->sysex;
    return CAMD_PROVIDER_OK;
}

int main(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDAppEndpoint *app = NULL;
    struct CAMDLegacyDriverAdapter *adapter = NULL;
    struct CAMDAppEndpointConfigV1 config;
    struct CAMDLegacyPortDescriptorV1 port;
    struct CAMDLegacyDriverDescriptorV1 descriptor;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDEndpointSnapshot *snapshot;
    const struct CAMDEndpointInfoV1 *info;
    const struct CAMDGroupInfoV1 *groups;
    const struct CAMDFunctionBlockInfoV1 *blocks;
    struct CAMDMIDI1EventV1 events[3], taken;
    struct CAMDHandleV1 first, second, input, refused;
    struct consumer consumer;
    const uint8_t sysex[] = { 0xf0, 0x7d, 0x01, 0x02, 0xf7 };
    uint8_t buffer[8];
    size_t group_count, block_count, sysex_count = 99;
    uint32_t dropped = 99;

    assert(registry);
    memset(&consumer, 0, sizeof(consumer));
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    config.MaxQueueCapacity = 4;
    config.MaxSysExBytes = 8;
    config.Notify = count_notify;
    assert(camd_app_endpoint_create(&config, &app) == CAMD_PROVIDER_INVALID);
    config.MaxSessions = 3;
    assert(camd_app_endpoint_create(&config, &app) == CAMD_PROVIDER_OK);

    memset(&port, 0, sizeof(port));
    port.Size = sizeof(port);
    port.Version = 1;
    port.EndpointID = make_id(1);
    port.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    strcpy(port.Name, "Program port");
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(2);
    descriptor.IdentityKind = 2;
    descriptor.ProtocolCapabilities = 1;
    descriptor.BackendContext = camd_app_endpoint_context(app);
    descriptor.BackendOps = camd_app_endpoint_ops(app);
    descriptor.Ports = &port;
    descriptor.PortCount = 1;
    descriptor.Transport = "a transport name that is far too long";
    assert(camd_legacy_driver_adapter_create(registry, &descriptor,
                                             &adapter) ==
           CAMD_REGISTRY_INVALID);
    descriptor.Transport = "camd-app";
    assert(camd_legacy_driver_adapter_create(registry, &descriptor,
                                             &adapter) == CAMD_REGISTRY_OK);
    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint(snapshot, 0, &info, &groups, &group_count,
                                  &blocks, &block_count) ==
           CAMD_REGISTRY_OK);
    assert(strcmp(info->Transport, "camd-app") == 0 &&
           info->State == CAMD_ENDPOINT_AVAILABLE);
    camd_snapshot_destroy(snapshot);

    /* Nothing waits before a client sends. */
    assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                  &sysex_count) == CAMD_PROVIDER_QUEUE_FULL &&
           sysex_count == 0);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = port.EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    request.QueueCapacity = 8;
    assert(camd_registry_session_open(registry, &request, &refused) ==
           CAMD_REGISTRY_UNSUPPORTED);
    request.QueueCapacity = 2;
    assert(camd_registry_session_open(registry, &request, &first) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_open(registry, &request, &second) ==
           CAMD_REGISTRY_OK);

    /* Each session has its own two records; the publisher takes in turn. */
    events[0] = note(10);
    events[1] = note(11);
    events[2] = note(12);
    assert(camd_registry_session_send_midi1(registry, first, events, 3) ==
           CAMD_REGISTRY_QUEUE_FULL);
    assert(notified == 0);
    assert(camd_registry_session_send_midi1(registry, first, events, 2) ==
           CAMD_REGISTRY_OK);
    assert(notified == 1);
    events[0] = note(20);
    assert(camd_registry_session_send_midi1(registry, second, events, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1_sysex(
               registry, second, sysex, sizeof(sysex), 0, 0, 0, 0) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_drain(registry, first) ==
           CAMD_REGISTRY_QUEUE_FULL);

    assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                  &sysex_count) == CAMD_PROVIDER_OK);
    {
        uint8_t one = taken.Bytes[1];

        assert(sysex_count == 0 && (one == 10 || one == 20));
        assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                      &sysex_count) == CAMD_PROVIDER_OK);
        /* The other session's first message, not the same session again. */
        assert(sysex_count == 0 && taken.Bytes[1] == (one == 10 ? 20 : 10));
    }
    /* In turn again: the second session's SysEx comes before the first
     * session's other message, and stays while it does not fit. */
    assert(camd_app_endpoint_take(app, &taken, buffer, 2, &sysex_count) ==
           CAMD_PROVIDER_TOO_LARGE && sysex_count == sizeof(sysex));
    assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                  &sysex_count) == CAMD_PROVIDER_OK &&
           sysex_count == sizeof(sysex) &&
           memcmp(buffer, sysex, sizeof(sysex)) == 0);
    assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                  &sysex_count) == CAMD_PROVIDER_OK &&
           sysex_count == 0 && taken.Bytes[1] == 11);
    assert(camd_registry_session_drain(registry, first) == CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                  &sysex_count) == CAMD_PROVIDER_QUEUE_FULL);

    /* What the publisher emits reaches receiving input sessions only. */
    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_registry_session_open(registry, &request, &input) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_open(registry, &request, &refused) ==
           CAMD_REGISTRY_STATE);
    events[0] = note(30);
    assert(camd_app_endpoint_emit_midi1(app, events, 1, &dropped) ==
           CAMD_PROVIDER_OK && dropped == 0 && consumer.events == 0);
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &consumer;
    sink.SubmitMIDI1 = consume_midi1;
    sink.SubmitMIDI1SysEx = consume_sysex;
    assert(camd_registry_session_start_receive(registry, input, &sink) ==
           CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_emit_midi1(app, events, 1, &dropped) ==
           CAMD_PROVIDER_OK && dropped == 0);
    assert(consumer.events == 1 && consumer.last == 30);
    assert(camd_app_endpoint_emit_sysex(app, sysex, sizeof(sysex), NULL) ==
           CAMD_PROVIDER_OK && consumer.sysex == 1);
    consumer.full = 1;
    assert(camd_app_endpoint_emit_midi1(app, events, 1, &dropped) ==
           CAMD_PROVIDER_OK && dropped == 1);
    consumer.full = 0;
    events[0].Length = 0;
    assert(camd_app_endpoint_emit_midi1(app, events, 1, NULL) ==
           CAMD_PROVIDER_INVALID);
    events[0] = note(31);

    /* Offline: sessions stay but carry nothing, and none opens. */
    assert(camd_legacy_driver_adapter_set_state(
               adapter, 0, CAMD_ENDPOINT_OFFLINE) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1(registry, first, events, 1) ==
           CAMD_REGISTRY_STATE);
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    assert(camd_registry_session_open(registry, &request, &refused) ==
           CAMD_REGISTRY_STATE);
    assert(camd_legacy_driver_adapter_set_state(
               adapter, 0, CAMD_ENDPOINT_AVAILABLE) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1(registry, first, events, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_set_state(adapter, 1,
                                                CAMD_ENDPOINT_OFFLINE) ==
           CAMD_REGISTRY_INVALID);

    /* The publisher goes while clients still hold sessions. */
    notified = 0;
    camd_app_endpoint_detach(app);
    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_set_state(
               adapter, 0, CAMD_ENDPOINT_OFFLINE) == CAMD_REGISTRY_RETIRED);
    assert(camd_registry_session_send_midi1(registry, first, events, 1) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_registry_session_open(registry, &request, &refused) !=
           CAMD_REGISTRY_OK);
    assert(notified == 0);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_BUSY);
    assert(camd_app_endpoint_destroy(app) == CAMD_PROVIDER_STATE);
    assert(camd_registry_session_stop_receive(registry, input) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, input) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, first) == CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_BUSY);
    assert(camd_registry_session_close(registry, second) == CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_destroy(app) == CAMD_PROVIDER_OK);

    camd_registry_destroy(registry);
    puts("CAMD published endpoint OK");
    return 0;
}
