#include "../prototypes/camd/legacy_driver_adapter.h"
#include "../prototypes/camd/legacy_output_backend.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct physical_port {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    unsigned int acquires;
    unsigned int release_attempts;
    unsigned int releases;
    unsigned int attempts;
    unsigned int accepted;
    unsigned int sysex;
    int writable;
    int fail_next_release;
    uint8_t notes[8];
};

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x4c454741u, 0x43594f55u, 0, value } };

    return id;
}

static struct CAMDMIDI1EventV1 note_event(uint8_t note)
{
    struct CAMDMIDI1EventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.Length = 3;
    event.Bytes[0] = 0x90;
    event.Bytes[1] = note;
    event.Bytes[2] = 100;
    return event;
}

static enum CAMDProviderResult acquire_port(void *context)
{
    struct physical_port *port = context;

    pthread_mutex_lock(&port->lock);
    ++port->acquires;
    pthread_mutex_unlock(&port->lock);
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult release_port(void *context)
{
    struct physical_port *port = context;
    enum CAMDProviderResult result = CAMD_PROVIDER_OK;

    pthread_mutex_lock(&port->lock);
    ++port->release_attempts;
    if (port->fail_next_release) {
        port->fail_next_release = 0;
        result = CAMD_PROVIDER_CALLBACK_FAILED;
    } else {
        ++port->releases;
    }
    pthread_mutex_unlock(&port->lock);
    return result;
}

static enum CAMDProviderResult write_midi1(
    void *context, const struct CAMDMIDI1EventV1 *event)
{
    struct physical_port *port = context;
    enum CAMDProviderResult result = CAMD_PROVIDER_QUEUE_FULL;

    assert(event && event->Length == 3 && event->Bytes[0] == 0x90);
    pthread_mutex_lock(&port->lock);
    ++port->attempts;
    if (port->writable) {
        assert(port->accepted < sizeof(port->notes));
        port->notes[port->accepted++] = event->Bytes[1];
        result = CAMD_PROVIDER_OK;
    }
    pthread_cond_broadcast(&port->condition);
    pthread_mutex_unlock(&port->lock);
    return result;
}

static enum CAMDProviderResult write_sysex(
    void *context, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    struct physical_port *port = context;

    assert(byte_count == 3 && bytes[0] == 0xf0 && bytes[2] == 0xf7);
    assert(time_high == 1 && time_low == 2 && clock_domain == 3 && flags == 4);
    pthread_mutex_lock(&port->lock);
    ++port->sysex;
    pthread_cond_broadcast(&port->condition);
    pthread_mutex_unlock(&port->lock);
    return CAMD_PROVIDER_OK;
}

static void wait_for(struct physical_port *port, unsigned int *field,
                     unsigned int expected)
{
    struct timespec deadline;

    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;
    pthread_mutex_lock(&port->lock);
    while (*field < expected) {
        int result = pthread_cond_timedwait(&port->condition, &port->lock,
                                             &deadline);

        assert(result == 0 || result == EINTR);
    }
    pthread_mutex_unlock(&port->lock);
}

int main(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDNativeWorkerFanout *fanout = NULL;
    struct CAMDLegacyOutputBackend *backend = NULL;
    struct CAMDLegacyDriverAdapter *adapter = NULL;
    struct CAMDLegacyOutputBackendConfigV1 backend_config;
    struct CAMDLegacyOutputPortV1 output_port;
    struct CAMDLegacyPortDescriptorV1 adapter_port;
    struct CAMDLegacyDriverDescriptorV1 adapter_descriptor;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDMIDI1EventV1 events[2], third;
    struct CAMDHandleV1 session;
    struct CAMDHandleV1 rejected_session;
    enum CAMDRegistryResult drain_result;
    struct physical_port physical;
    const uint8_t sysex[] = { 0xf0, 0x01, 0xf7 };
    uint32_t woken = 0;

    assert(registry);
    memset(&physical, 0, sizeof(physical));
    assert(pthread_mutex_init(&physical.lock, NULL) == 0);
    assert(pthread_cond_init(&physical.condition, NULL) == 0);
    assert(camd_native_worker_fanout_create(1, &fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);

    memset(&output_port, 0, sizeof(output_port));
    output_port.Size = sizeof(output_port);
    output_port.Version = 1;
    output_port.EndpointID = make_id(1);
    output_port.Context = &physical;
    output_port.CapacityFanout = fanout;
    memset(&backend_config, 0, sizeof(backend_config));
    backend_config.Size = sizeof(backend_config);
    backend_config.Version = 1;
    backend_config.Callbacks.Size = sizeof(backend_config.Callbacks);
    backend_config.Callbacks.Version = 1;
    backend_config.Callbacks.Acquire = acquire_port;
    backend_config.Callbacks.Release = release_port;
    backend_config.Callbacks.SubmitMIDI1 = write_midi1;
    backend_config.Callbacks.SubmitMIDI1SysEx = write_sysex;
    backend_config.Ports = &output_port;
    backend_config.PortCount = 1;
    backend_config.MaxQueueCapacity = 4;
    backend_config.MaxSysExBytes = 16;
    backend_config.WorkerItemBudget = 1;
    assert(camd_legacy_output_backend_create(&backend_config, &backend) ==
           CAMD_PROVIDER_OK);

    memset(&adapter_port, 0, sizeof(adapter_port));
    adapter_port.Size = sizeof(adapter_port);
    adapter_port.Version = 1;
    adapter_port.EndpointID = output_port.EndpointID;
    adapter_port.Directions = CAMD_PROVIDER_DIRECTION_OUTPUT;
    strcpy(adapter_port.Name, "Legacy output 0");
    strcpy(adapter_port.ProductInstance, "Legacy:output:0");
    memset(&adapter_descriptor, 0, sizeof(adapter_descriptor));
    adapter_descriptor.Size = sizeof(adapter_descriptor);
    adapter_descriptor.Version = 1;
    adapter_descriptor.ProviderID = make_id(100);
    adapter_descriptor.IdentityKind = 2;
    adapter_descriptor.ProtocolCapabilities = 1;
    adapter_descriptor.BackendContext =
        camd_legacy_output_backend_context(backend);
    adapter_descriptor.BackendOps = camd_legacy_output_backend_ops(backend);
    adapter_descriptor.Ports = &adapter_port;
    adapter_descriptor.PortCount = 1;
    assert(camd_legacy_driver_adapter_create(registry, &adapter_descriptor,
                                             &adapter) == CAMD_REGISTRY_OK);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = output_port.EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    request.QueueCapacity = 2;
    assert(camd_registry_session_open(registry, &request, &session) ==
           CAMD_REGISTRY_OK);
    assert(physical.acquires == 1);
    /* A full physical-port fan-out rolls the second open back completely. */
    assert(camd_registry_session_open(registry, &request, &rejected_session) ==
           CAMD_REGISTRY_CALLBACK_FAILED);
    assert(physical.acquires == 2 && physical.releases == 1);
    assert(camd_legacy_output_backend_destroy(backend) ==
           CAMD_PROVIDER_STATE);

    events[0] = note_event(60);
    events[1] = note_event(61);
    third = note_event(62);
    assert(camd_registry_session_send_midi1(registry, session, events, 2) ==
           CAMD_REGISTRY_OK);
    wait_for(&physical, &physical.attempts, 1);
    assert(camd_registry_session_send_midi1(registry, session, &third, 1) ==
           CAMD_REGISTRY_QUEUE_FULL);

    pthread_mutex_lock(&physical.lock);
    physical.writable = 1;
    pthread_mutex_unlock(&physical.lock);
    assert(camd_native_worker_fanout_wake_all(fanout, &woken) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(woken == 1);
    wait_for(&physical, &physical.accepted, 2);
    assert(physical.notes[0] == 60 && physical.notes[1] == 61);

    assert(camd_registry_session_send_midi1(registry, session, &third, 1) ==
           CAMD_REGISTRY_OK);
    wait_for(&physical, &physical.accepted, 3);
    assert(physical.notes[2] == 62);
    assert(camd_registry_session_send_midi1_sysex(
               registry, session, sysex, sizeof(sysex), 1, 2, 3, 4) ==
           CAMD_REGISTRY_OK);
    wait_for(&physical, &physical.sysex, 1);
    while ((drain_result = camd_registry_session_drain(registry, session)) ==
           CAMD_REGISTRY_QUEUE_FULL) {
        struct timespec pause = { 0, 1000000 };

        nanosleep(&pause, NULL);
    }
    assert(drain_result == CAMD_REGISTRY_OK);

    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_BUSY);
    physical.fail_next_release = 1;
    assert(camd_registry_session_close(registry, session) ==
           CAMD_REGISTRY_CALLBACK_FAILED);
    assert(camd_legacy_output_backend_destroy(backend) ==
           CAMD_PROVIDER_STATE);
    assert(camd_registry_session_close(registry, session) == CAMD_REGISTRY_OK);
    assert(physical.release_attempts == 3 && physical.releases == 2);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(camd_legacy_output_backend_destroy(backend) == CAMD_PROVIDER_OK);
    assert(camd_native_worker_fanout_destroy(fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    camd_registry_destroy(registry);
    pthread_cond_destroy(&physical.condition);
    pthread_mutex_destroy(&physical.lock);
    puts("CAMD legacy output backend OK");
    return 0;
}
