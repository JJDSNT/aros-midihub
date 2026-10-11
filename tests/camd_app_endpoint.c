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

static unsigned int ump_seen;
static uint32_t ump_last;

static enum CAMDProviderResult consume_ump(
    void *context, const struct CAMDUMPEventV1 *events, size_t event_count)
{
    (void)context;
    ump_seen += (unsigned int)event_count;
    ump_last = events[event_count - 1].Words[1];
    return CAMD_PROVIDER_OK;
}

static struct CAMDUMPEventV1 ump(uint32_t word0, uint32_t word1,
                                 uint32_t words)
{
    struct CAMDUMPEventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = words;
    event.Words[0] = word0;
    event.Words[1] = word1;
    return event;
}

/* A UMP endpoint carries complete packets and nothing else. */
static void test_ump(void)
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
    struct CAMDGroupInfoV1 group[2];
    struct CAMDFunctionBlockInfoV1 block;
    struct CAMDUMPEventV1 events[3], taken;
    struct CAMDMIDI1EventV1 midi1 = note(1);
    struct CAMDHandleV1 output, input, refused;
    size_t group_count, block_count;

    assert(registry);
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    config.MaxQueueCapacity = 4;
    config.MaxSysExBytes = 8;
    config.MaxSessions = 4;
    config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    config.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI2;
    /* MIDI 2.0 protocol needs the UMP format. */
    assert(camd_app_endpoint_create(&config, &app) == CAMD_PROVIDER_INVALID);
    config.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    assert(camd_app_endpoint_create(&config, &app) == CAMD_PROVIDER_OK);

    memset(&port, 0, sizeof(port));
    port.Size = sizeof(port);
    port.Version = 1;
    port.EndpointID = make_id(11);
    port.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    strcpy(port.Name, "Program UMP port");
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(12);
    descriptor.IdentityKind = 2;
    descriptor.ProtocolCapabilities = 2;
    descriptor.BackendContext = camd_app_endpoint_context(app);
    descriptor.BackendOps = camd_app_endpoint_ops(app);
    descriptor.Ports = &port;
    descriptor.PortCount = 1;
    descriptor.Transport = "camd-app";
    descriptor.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    descriptor.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI2;
    assert(camd_legacy_driver_adapter_create(registry, &descriptor,
                                             &adapter) == CAMD_REGISTRY_OK);

    /* Two Groups and a Function Block over both, replaced as a whole. */
    memset(group, 0, sizeof(group));
    group[0].Size = group[1].Size = sizeof(group[0]);
    group[0].Version = group[1].Version = 1;
    group[0].Group = 0;
    group[1].Group = 1;
    strcpy(group[0].Name, "Main");
    strcpy(group[1].Name, "Aux");
    memset(&block, 0, sizeof(block));
    block.Size = sizeof(block);
    block.Version = 1;
    block.Number = 0;
    block.FirstGroup = 0;
    block.GroupCount = 2;
    strcpy(block.Name, "Synth");
    assert(camd_legacy_driver_adapter_set_topology(adapter, 0, group, 2,
                                                   &block, 1) ==
           CAMD_REGISTRY_OK);
    block.FirstGroup = 15;
    assert(camd_legacy_driver_adapter_set_topology(adapter, 0, group, 2,
                                                   &block, 1) ==
           CAMD_REGISTRY_INVALID);
    group[1].Group = 0;
    assert(camd_legacy_driver_adapter_set_topology(adapter, 0, group, 2,
                                                   NULL, 0) ==
           CAMD_REGISTRY_DUPLICATE);
    /* A refused replacement leaves the earlier topology. */
    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint(snapshot, 0, &info, &groups, &group_count,
                                  &blocks, &block_count) ==
           CAMD_REGISTRY_OK);
    assert(info->NativeDataFormats == CAMD_DATA_FORMAT_UMP &&
           info->CurrentProtocol == CAMD_PROTOCOL_MIDI2);
    assert(group_count == 2 && block_count == 1 && groups[1].Group == 1 &&
           blocks[0].GroupCount == 2 &&
           memcmp(&groups[0].EndpointID, &port.EndpointID,
                  sizeof(port.EndpointID)) == 0);
    camd_snapshot_destroy(snapshot);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = port.EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.QueueCapacity = 2;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    /* Nothing converts: a MIDI 1.0 session does not open here. */
    assert(camd_registry_session_open(registry, &request, &refused) ==
           CAMD_REGISTRY_UNSUPPORTED);
    request.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    assert(camd_registry_session_open(registry, &request, &refused) ==
           CAMD_REGISTRY_UNSUPPORTED);
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI2;
    assert(camd_registry_session_open(registry, &request, &output) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1(registry, output, &midi1, 1) ==
           CAMD_REGISTRY_UNSUPPORTED);

    /* MIDI 2.0 note on is two words; one or three is not a message. */
    events[0] = ump(0x40903c00u, 0x80000000u, 1);
    assert(camd_registry_session_send_ump(registry, output, events, 1) ==
           CAMD_REGISTRY_INVALID);
    events[0] = ump(0x40903c00u, 0x80000000u, 2);
    events[1] = ump(0x40803c00u, 0x11111111u, 2);
    events[2] = ump(0x40903d00u, 0x22222222u, 2);
    assert(camd_registry_session_send_ump(registry, output, events, 3) ==
           CAMD_REGISTRY_QUEUE_FULL);
    assert(camd_registry_session_send_ump(registry, output, events, 2) ==
           CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_take(app, &midi1, NULL, 0, &group_count) ==
           CAMD_PROVIDER_INVALID);
    assert(camd_app_endpoint_take_ump(app, &taken) == CAMD_PROVIDER_OK &&
           taken.WordCount == 2 && taken.Words[0] == 0x40903c00u &&
           taken.Words[1] == 0x80000000u);
    assert(camd_app_endpoint_take_ump(app, &taken) == CAMD_PROVIDER_OK &&
           taken.Words[1] == 0x11111111u);
    assert(camd_app_endpoint_take_ump(app, &taken) ==
           CAMD_PROVIDER_QUEUE_FULL);

    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_registry_session_open(registry, &request, &input) ==
           CAMD_REGISTRY_OK);
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.SubmitMIDI1 = consume_midi1;
    sink.SubmitMIDI1SysEx = consume_sysex;
    /* A UMP session's sink has to take UMP. */
    assert(camd_registry_session_start_receive(registry, input, &sink) ==
           CAMD_REGISTRY_INVALID);
    sink.SubmitUMP = consume_ump;
    assert(camd_registry_session_start_receive(registry, input, &sink) ==
           CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_emit_ump(app, &events[2], 1, NULL) ==
           CAMD_PROVIDER_OK && ump_seen == 1 && ump_last == 0x22222222u);
    /* A 128-bit stream message and a 32-bit utility message pass too. */
    events[0] = ump(0xf0000000u, 0, 4);
    events[1] = ump(0x00000000u, 0, 1);
    assert(camd_app_endpoint_emit_ump(app, events, 2, NULL) ==
           CAMD_PROVIDER_OK && ump_seen == 3);
    events[0].WordCount = 3;
    assert(camd_app_endpoint_emit_ump(app, events, 1, NULL) ==
           CAMD_PROVIDER_INVALID);
    assert(camd_app_endpoint_emit_midi1(app, &midi1, 1, NULL) ==
           CAMD_PROVIDER_INVALID);
    assert(camd_app_endpoint_inject_midi1(app, &midi1) ==
           CAMD_PROVIDER_INVALID);

    assert(camd_registry_session_stop_receive(registry, input) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, input) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_set_topology(adapter, 0, group, 1,
                                                   NULL, 0) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(camd_app_endpoint_destroy(app) == CAMD_PROVIDER_OK);
    camd_registry_destroy(registry);
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

    {
        struct CAMDSessionStatsV1 stats;

        memset(&stats, 0xff, sizeof(stats));
        assert(camd_registry_session_stats(registry, first, &stats) ==
               CAMD_REGISTRY_OK);
        /* Two queued, then three refused together. */
        assert(stats.Size == sizeof(stats) && stats.Sent == 2 &&
               stats.Rejected == 3 && stats.Received == 0);
        assert(camd_registry_session_stats(registry, input, &stats) ==
               CAMD_REGISTRY_OK);
        /* A message and SysEx delivered, one message lost. */
        assert(stats.Received == 2 && stats.Dropped == 1 && stats.Sent == 0);
        assert(camd_registry_session_stats(registry, refused, &stats) ==
               CAMD_REGISTRY_STALE);
    }

    /* The legacy side: injected messages are taken like a session's, and the
     * projection sink gets what the publisher emits. */
    {
        struct consumer legacy;
        struct CAMDProviderReceiveSinkV1 projection = sink;

        memset(&legacy, 0, sizeof(legacy));
        projection.Context = &legacy;
        events[0] = note(40);
        assert(camd_app_endpoint_inject_midi1(app, events) ==
               CAMD_PROVIDER_OK);
        assert(camd_app_endpoint_inject_sysex(app, sysex, sizeof(sysex)) ==
               CAMD_PROVIDER_OK);
        assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                      &sysex_count) == CAMD_PROVIDER_OK &&
               sysex_count == 0 && taken.Bytes[1] == 40);
        assert(camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                      &sysex_count) == CAMD_PROVIDER_OK &&
               sysex_count == sizeof(sysex));
        events[0] = note(41);
        events[1] = note(42);
        events[2] = note(43);
        assert(camd_app_endpoint_inject_midi1(app, &events[0]) ==
                   CAMD_PROVIDER_OK &&
               camd_app_endpoint_inject_midi1(app, &events[1]) ==
                   CAMD_PROVIDER_OK &&
               camd_app_endpoint_inject_midi1(app, &events[2]) ==
                   CAMD_PROVIDER_OK &&
               camd_app_endpoint_inject_midi1(app, &events[0]) ==
                   CAMD_PROVIDER_OK &&
               camd_app_endpoint_inject_midi1(app, &events[0]) ==
                   CAMD_PROVIDER_QUEUE_FULL);
        while (camd_app_endpoint_take(app, &taken, buffer, sizeof(buffer),
                                      &sysex_count) == CAMD_PROVIDER_OK)
            ;
        projection.SubmitMIDI1 = NULL;
        assert(camd_app_endpoint_set_projection(app, &projection) ==
               CAMD_PROVIDER_INVALID);
        projection.SubmitMIDI1 = consume_midi1;
        assert(camd_app_endpoint_set_projection(app, &projection) ==
               CAMD_PROVIDER_OK);
        events[0] = note(50);
        assert(camd_app_endpoint_emit_midi1(app, events, 1, &dropped) ==
               CAMD_PROVIDER_OK && dropped == 0);
        assert(legacy.events == 1 && legacy.last == 50 &&
               consumer.last == 50);
        assert(camd_app_endpoint_emit_sysex(app, sysex, sizeof(sysex),
                                            NULL) == CAMD_PROVIDER_OK &&
               legacy.sysex == 1);
        assert(camd_app_endpoint_set_projection(app, NULL) ==
               CAMD_PROVIDER_OK);
        assert(camd_app_endpoint_emit_midi1(app, events, 1, NULL) ==
               CAMD_PROVIDER_OK && legacy.events == 1);
    }
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
    test_ump();
    puts("CAMD published endpoint OK");
    return 0;
}
