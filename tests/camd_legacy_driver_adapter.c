#include "../prototypes/camd/legacy_driver_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct legacy_backend {
    unsigned int opens;
    unsigned int closes;
    unsigned int midi1_batches;
    unsigned int sysex_batches;
    unsigned int ump_batches;
    unsigned int receive_starts;
    unsigned int receive_stops;
    unsigned int drains;
    unsigned int cancels;
    unsigned int shutdowns;
    unsigned int direct_legacy_batches;
    struct CAMDProviderReceiveSinkV1 sink;
};

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x4c454741u, 0x43590000u, 0, value } };

    return id;
}

static enum CAMDProviderResult backend_open(
    void *context, const struct CAMDProviderOpenRequestV1 *request,
    struct CAMDProviderOpenResultV1 *result)
{
    struct legacy_backend *backend = context;

    assert(request->DataFormat == CAMD_PROVIDER_FORMAT_MIDI1);
    assert(request->Protocol == CAMD_PROVIDER_PROTOCOL_MIDI1);
    ++backend->opens;
    result->SessionContext = backend;
    result->EffectiveQueueCapacity = request->QueueCapacity;
    result->MaxSysExBytes = 1024;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_close(void *context,
                                              void *session_context)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    ++backend->closes;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_send_midi1(
    void *context, void *session_context,
    const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    assert(events != NULL && event_count == 1);
    assert(events[0].Bytes[0] == 0x90);
    ++backend->midi1_batches;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_send_sysex(
    void *context, void *session_context, const uint8_t *bytes,
    size_t byte_count, uint32_t time_high, uint32_t time_low,
    uint32_t clock_domain, uint32_t flags)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    assert(byte_count == 3 && bytes[0] == 0xf0 && bytes[2] == 0xf7);
    assert(time_high == 1 && time_low == 2 && clock_domain == 3 && flags == 4);
    ++backend->sysex_batches;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_start_receive(
    void *context, void *session_context,
    const struct CAMDProviderReceiveSinkV1 *sink)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    assert(sink->SubmitMIDI1 != NULL && sink->SubmitMIDI1SysEx != NULL);
    assert(sink->SubmitUMP == NULL);
    backend->sink = *sink;
    ++backend->receive_starts;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_stop_receive(void *context,
                                                     void *session_context)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    memset(&backend->sink, 0, sizeof(backend->sink));
    ++backend->receive_stops;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_drain(void *context,
                                             void *session_context)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    ++backend->drains;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_cancel(void *context,
                                              void *session_context)
{
    struct legacy_backend *backend = context;

    assert(session_context == backend);
    ++backend->cancels;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult backend_shutdown(void *context)
{
    struct legacy_backend *backend = context;

    ++backend->shutdowns;
    return CAMD_PROVIDER_OK;
}

static int backend_shutdown_ready(void *context)
{
    (void)context;
    return 1;
}

static void legacy_cluster_send(struct legacy_backend *backend,
                                const struct CAMDMIDI1EventV1 *event)
{
    assert(event->Bytes[0] == 0x90);
    ++backend->direct_legacy_batches;
}

struct consumer {
    unsigned int midi1_batches;
};

static enum CAMDProviderResult consumer_midi1(
    void *context, const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct consumer *consumer = context;

    assert(events != NULL && event_count == 1);
    ++consumer->midi1_batches;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult consumer_sysex(
    void *context, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    (void)context;
    (void)bytes;
    (void)byte_count;
    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    return CAMD_PROVIDER_OK;
}

int main(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDLegacyDriverAdapter *adapter = NULL;
    struct CAMDProviderOpsV1 backend_ops;
    struct CAMDLegacyPortDescriptorV1 ports[2];
    struct CAMDLegacyDriverDescriptorV1 descriptor;
    struct CAMDEndpointSnapshot *snapshot;
    const struct CAMDEndpointInfoV1 *endpoint;
    const struct CAMDGroupInfoV1 *groups;
    const struct CAMDFunctionBlockInfoV1 *blocks;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDMIDI1EventV1 event;
    struct CAMDUMPEventV1 ump;
    struct CAMDHandleV1 output, input;
    struct legacy_backend backend;
    struct consumer consumer;
    size_t group_count, block_count;
    const uint8_t sysex[] = { 0xf0, 0x01, 0xf7 };

    assert(registry != NULL);
    memset(&backend, 0, sizeof(backend));
    memset(&backend_ops, 0, sizeof(backend_ops));
    backend_ops.Size = sizeof(backend_ops);
    backend_ops.Version = 1;
    backend_ops.Open = backend_open;
    backend_ops.Close = backend_close;
    backend_ops.SendMIDI1 = backend_send_midi1;
    backend_ops.SendMIDI1SysEx = backend_send_sysex;
    backend_ops.StartReceive = backend_start_receive;
    backend_ops.StopReceive = backend_stop_receive;
    backend_ops.Drain = backend_drain;
    backend_ops.Cancel = backend_cancel;
    backend_ops.BeginShutdown = backend_shutdown;
    backend_ops.ShutdownReady = backend_shutdown_ready;

    memset(ports, 0, sizeof(ports));
    ports[0].Size = ports[1].Size = sizeof(ports[0]);
    ports[0].Version = ports[1].Version = 1;
    ports[0].EndpointID = make_id(1);
    ports[1].EndpointID = make_id(2);
    ports[0].Directions = CAMD_PROVIDER_DIRECTION_OUTPUT;
    ports[1].Directions = CAMD_PROVIDER_DIRECTION_INPUT;
    strcpy(ports[0].Name, "Legacy Driver out.0");
    strcpy(ports[1].Name, "Legacy Driver in.0");
    strcpy(ports[0].ProductInstance, "Legacy Driver:0:out");
    strcpy(ports[1].ProductInstance, "Legacy Driver:0:in");

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(100);
    descriptor.IdentityKind = 2;
    descriptor.ProtocolCapabilities = 1;
    descriptor.BackendContext = &backend;
    descriptor.BackendOps = &backend_ops;
    descriptor.Ports = ports;
    descriptor.PortCount = 2;

    ports[1].EndpointID = ports[0].EndpointID;
    assert(camd_legacy_driver_adapter_create(registry, &descriptor, &adapter) ==
           CAMD_REGISTRY_INVALID);
    assert(adapter == NULL);
    ports[1].EndpointID = make_id(2);
    assert(camd_legacy_driver_adapter_create(registry, &descriptor, &adapter) ==
           CAMD_REGISTRY_OK);

    assert(camd_registry_snapshot(registry, &snapshot) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(snapshot) == 2);
    assert(camd_snapshot_endpoint(snapshot, 0, &endpoint, &groups,
                                  &group_count, &blocks, &block_count) ==
           CAMD_REGISTRY_OK);
    assert(endpoint->NativeDataFormats == CAMD_DATA_FORMAT_MIDI1);
    assert(endpoint->CurrentProtocol == CAMD_PROVIDER_PROTOCOL_MIDI1);
    assert(strcmp(endpoint->Transport, "camd-legacy") == 0);
    assert(group_count == 0 && block_count == 0);
    camd_snapshot_destroy(snapshot);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = ports[0].EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    request.QueueCapacity = 8;
    assert(camd_registry_session_open(registry, &request, &output) ==
           CAMD_REGISTRY_OK);

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.Length = 3;
    event.Bytes[0] = 0x90;
    event.Bytes[1] = 60;
    event.Bytes[2] = 100;
    legacy_cluster_send(&backend, &event);
    assert(backend.direct_legacy_batches == 1 && backend.midi1_batches == 0);
    assert(camd_registry_session_send_midi1(registry, output, &event, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_send_midi1_sysex(
               registry, output, sysex, sizeof(sysex), 1, 2, 3, 4) ==
           CAMD_REGISTRY_OK);
    memset(&ump, 0, sizeof(ump));
    ump.Size = sizeof(ump);
    ump.Version = 1;
    ump.WordCount = 1;
    ump.Words[0] = 0x20903c64;
    assert(camd_registry_session_send_ump(registry, output, &ump, 1) ==
           CAMD_REGISTRY_UNSUPPORTED);
    assert(backend.midi1_batches == 1 && backend.sysex_batches == 1);
    assert(backend.ump_batches == 0);
    assert(camd_registry_session_drain(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_cancel(registry, output) == CAMD_REGISTRY_OK);

    request.EndpointID = ports[1].EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    assert(camd_registry_session_open(registry, &request, &input) ==
           CAMD_REGISTRY_UNSUPPORTED);
    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_registry_session_open(registry, &request, &input) ==
           CAMD_REGISTRY_OK);
    memset(&consumer, 0, sizeof(consumer));
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &consumer;
    sink.SubmitMIDI1 = consumer_midi1;
    sink.SubmitMIDI1SysEx = consumer_sysex;
    assert(camd_registry_session_start_receive(registry, input, &sink) ==
           CAMD_REGISTRY_OK);
    assert(backend.sink.SubmitMIDI1(backend.sink.Context, &event, 1) ==
           CAMD_PROVIDER_OK);
    assert(consumer.midi1_batches == 1);

    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(backend.shutdowns == 1);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_BUSY);
    assert(camd_registry_session_stop_receive(registry, input) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, input) == CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, output) == CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(backend.opens == 2 && backend.closes == 2);
    assert(backend.receive_starts == 1 && backend.receive_stops == 1);
    assert(backend.drains == 1 && backend.cancels == 1);
    camd_registry_destroy(registry);
    puts("CAMD legacy driver adapter OK");
    return 0;
}
