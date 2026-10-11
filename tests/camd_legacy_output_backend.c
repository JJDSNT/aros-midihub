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

struct input_port {
    unsigned int acquires;
    unsigned int releases;
    unsigned int outputs;
};

struct input_consumer {
    unsigned int events;
    unsigned int sysex;
    uint8_t last[3];
    size_t sysex_bytes;
    int full;
};

static enum CAMDProviderResult input_acquire(void *context)
{
    ++((struct input_port *)context)->acquires;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult input_release(void *context)
{
    ++((struct input_port *)context)->releases;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult input_output(void *context)
{
    ++((struct input_port *)context)->outputs;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult input_no_midi1(
    void *context, const struct CAMDMIDI1EventV1 *event)
{
    (void)context;
    (void)event;
    return CAMD_PROVIDER_CALLBACK_FAILED;
}

static enum CAMDProviderResult input_no_sysex(
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
    return CAMD_PROVIDER_CALLBACK_FAILED;
}

static enum CAMDProviderResult consume_midi1(
    void *context, const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct input_consumer *consumer = context;

    assert(event_count == 1 && events[0].Length == 3);
    if (consumer->full)
        return CAMD_PROVIDER_QUEUE_FULL;
    memcpy(consumer->last, events[0].Bytes, 3);
    ++consumer->events;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult consume_sysex(
    void *context, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    struct input_consumer *consumer = context;

    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    assert(bytes[0] == 0xf0 && bytes[byte_count - 1] == 0xf7);
    consumer->sysex_bytes = byte_count;
    ++consumer->sysex;
    return CAMD_PROVIDER_OK;
}

/* Two input sessions on one of two ports receive what that port delivers. */
static void test_input_sessions(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDNativeWorkerFanout *fanout = NULL;
    struct CAMDLegacyOutputBackend *backend = NULL;
    struct CAMDLegacyDriverAdapter *adapter = NULL;
    struct CAMDLegacyOutputBackendConfigV1 config;
    struct CAMDLegacyOutputPortV1 ports[2];
    struct CAMDLegacyPortDescriptorV1 adapter_ports[2];
    struct CAMDLegacyDriverDescriptorV1 descriptor;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDMIDI1EventV1 event = note_event(60);
    struct CAMDHandleV1 first, second;
    struct input_port physical[2];
    struct input_consumer one, two;
    const uint8_t sysex[] = { 0xf0, 0x7d, 0x01, 0xf7 };
    uint32_t dropped = 99;
    size_t i;

    assert(registry);
    memset(physical, 0, sizeof(physical));
    memset(&one, 0, sizeof(one));
    memset(&two, 0, sizeof(two));
    assert(camd_native_worker_fanout_create(1, &fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    memset(ports, 0, sizeof(ports));
    memset(adapter_ports, 0, sizeof(adapter_ports));
    for (i = 0; i < 2; ++i) {
        ports[i].Size = sizeof(ports[i]);
        ports[i].Version = 1;
        ports[i].EndpointID = make_id(40 + (uint32_t)i);
        ports[i].Context = &physical[i];
        ports[i].CapacityFanout = fanout;
        adapter_ports[i].Size = sizeof(adapter_ports[i]);
        adapter_ports[i].Version = 1;
        adapter_ports[i].EndpointID = ports[i].EndpointID;
        adapter_ports[i].Directions = CAMD_PROVIDER_DIRECTION_ALL;
        strcpy(adapter_ports[i].Name, i ? "Legacy 1" : "Legacy 0");
    }
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Callbacks.Size = sizeof(config.Callbacks);
    config.Callbacks.Version = 1;
    config.Callbacks.Acquire = input_output;
    config.Callbacks.Release = input_output;
    config.Callbacks.SubmitMIDI1 = input_no_midi1;
    config.Callbacks.SubmitMIDI1SysEx = input_no_sysex;
    config.Callbacks.AcquireInput = input_acquire;
    config.Ports = ports;
    config.PortCount = 2;
    config.MaxQueueCapacity = 8;
    config.MaxSysExBytes = 16;
    config.WorkerItemBudget = 1;
    /* Input needs both of its callbacks. */
    assert(camd_legacy_output_backend_create(&config, &backend) ==
           CAMD_PROVIDER_INVALID);
    config.Callbacks.ReleaseInput = input_release;
    assert(camd_legacy_output_backend_create(&config, &backend) ==
           CAMD_PROVIDER_OK);

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(140);
    descriptor.IdentityKind = 3;
    descriptor.ProtocolCapabilities = 1;
    descriptor.BackendContext = camd_legacy_output_backend_context(backend);
    descriptor.BackendOps = camd_legacy_output_backend_ops(backend);
    descriptor.Ports = adapter_ports;
    descriptor.PortCount = 2;
    assert(camd_legacy_driver_adapter_create(registry, &descriptor,
                                             &adapter) == CAMD_REGISTRY_OK);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = ports[1].EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    request.QueueCapacity = 4;
    assert(camd_registry_session_open(registry, &request, &first) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_open(registry, &request, &second) ==
           CAMD_REGISTRY_OK);
    assert(physical[1].acquires == 2 && physical[0].acquires == 0 &&
           physical[1].outputs == 0);
    /* An input session does not send. */
    assert(camd_registry_session_send_midi1(registry, first, &event, 1) !=
           CAMD_REGISTRY_OK);

    /* Nothing is delivered before a session starts receiving. */
    assert(camd_legacy_output_backend_receive_midi1(backend, 1, &event,
                                                    &dropped) ==
           CAMD_PROVIDER_OK && dropped == 0);
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.SubmitMIDI1 = consume_midi1;
    sink.SubmitMIDI1SysEx = consume_sysex;
    sink.Context = &one;
    assert(camd_registry_session_start_receive(registry, first, &sink) ==
           CAMD_REGISTRY_OK);
    sink.Context = &two;
    assert(camd_registry_session_start_receive(registry, second, &sink) ==
           CAMD_REGISTRY_OK);

    assert(camd_legacy_output_backend_receive_midi1(backend, 1, &event,
                                                    &dropped) ==
           CAMD_PROVIDER_OK && dropped == 0);
    assert(one.events == 1 && two.events == 1 && one.last[1] == 60);
    /* The other port's input reaches neither session. */
    assert(camd_legacy_output_backend_receive_midi1(backend, 0, &event,
                                                    NULL) ==
           CAMD_PROVIDER_OK);
    assert(one.events == 1 && two.events == 1);
    assert(camd_legacy_output_backend_receive_sysex(backend, 1, sysex,
                                                    sizeof(sysex), NULL) ==
           CAMD_PROVIDER_OK);
    assert(one.sysex == 1 && two.sysex == 1 &&
           one.sysex_bytes == sizeof(sysex));
    assert(camd_legacy_output_backend_receive_midi1(backend, 2, &event,
                                                    NULL) ==
           CAMD_PROVIDER_INVALID);

    /* A sink without room loses the message; the other still gets it. */
    two.full = 1;
    assert(camd_legacy_output_backend_receive_midi1(backend, 1, &event,
                                                    &dropped) ==
           CAMD_PROVIDER_OK && dropped == 1);
    assert(one.events == 2 && two.events == 1);
    two.full = 0;

    assert(camd_registry_session_stop_receive(registry, first) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_output_backend_receive_midi1(backend, 1, &event,
                                                    NULL) ==
           CAMD_PROVIDER_OK);
    assert(one.events == 2 && two.events == 2);

    assert(camd_legacy_output_backend_destroy(backend) ==
           CAMD_PROVIDER_STATE);
    assert(camd_registry_session_close(registry, first) == CAMD_REGISTRY_OK);
    /* A receiving session has to stop before it closes. */
    assert(camd_registry_session_close(registry, second) ==
           CAMD_REGISTRY_STATE);
    assert(camd_registry_session_stop_receive(registry, second) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, second) == CAMD_REGISTRY_OK);
    assert(physical[1].releases == 2);
    assert(camd_legacy_output_backend_receive_midi1(backend, 1, &event,
                                                    NULL) ==
           CAMD_PROVIDER_OK);
    assert(two.events == 2);

    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(camd_legacy_output_backend_destroy(backend) == CAMD_PROVIDER_OK);
    assert(camd_native_worker_fanout_destroy(fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    camd_registry_destroy(registry);
}

struct timed_port {
    pthread_mutex_t lock;
    unsigned int written;
    uint8_t notes[8];
    uint32_t when[8];
};

static uint32_t clock_ms(void *context)
{
    struct timespec now;

    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000u + now.tv_nsec / 1000000);
}

static enum CAMDProviderResult timed_ok(void *context)
{
    (void)context;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult timed_midi1(
    void *context, const struct CAMDMIDI1EventV1 *event)
{
    struct timed_port *port = context;

    pthread_mutex_lock(&port->lock);
    assert(port->written < sizeof(port->notes));
    port->notes[port->written] = event->Bytes[1];
    port->when[port->written++] = clock_ms(NULL);
    pthread_mutex_unlock(&port->lock);
    return CAMD_PROVIDER_OK;
}

static unsigned int timed_written(struct timed_port *port)
{
    unsigned int written;

    pthread_mutex_lock(&port->lock);
    written = port->written;
    pthread_mutex_unlock(&port->lock);
    return written;
}

static void sleep_ms(long milliseconds)
{
    struct timespec pause = { milliseconds / 1000,
                              (milliseconds % 1000) * 1000000L };

    nanosleep(&pause, NULL);
}

/* A message with a time waits for it, and those behind it wait with it. */
static void test_scheduled_output(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDNativeWorkerFanout *fanout = NULL;
    struct CAMDLegacyOutputBackend *backend = NULL;
    struct CAMDLegacyDriverAdapter *adapter = NULL;
    struct CAMDLegacyOutputBackendConfigV1 config;
    struct CAMDLegacyOutputPortV1 port;
    struct CAMDLegacyPortDescriptorV1 adapter_port;
    struct CAMDLegacyDriverDescriptorV1 descriptor;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDMIDI1EventV1 events[3];
    struct CAMDHandleV1 session;
    struct timed_port physical;
    uint32_t start, target;
    int tries;

    assert(registry);
    memset(&physical, 0, sizeof(physical));
    assert(pthread_mutex_init(&physical.lock, NULL) == 0);
    assert(camd_native_worker_fanout_create(1, &fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    memset(&port, 0, sizeof(port));
    port.Size = sizeof(port);
    port.Version = 1;
    port.EndpointID = make_id(70);
    port.Context = &physical;
    port.CapacityFanout = fanout;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Callbacks.Size = sizeof(config.Callbacks);
    config.Callbacks.Version = 1;
    config.Callbacks.Acquire = timed_ok;
    config.Callbacks.Release = timed_ok;
    config.Callbacks.SubmitMIDI1 = timed_midi1;
    config.Callbacks.SubmitMIDI1SysEx = input_no_sysex;
    config.Callbacks.Now = clock_ms;
    config.Ports = &port;
    config.PortCount = 1;
    config.MaxQueueCapacity = 8;
    config.MaxSysExBytes = 16;
    config.WorkerItemBudget = 4;
    assert(camd_legacy_output_backend_create(&config, &backend) ==
           CAMD_PROVIDER_OK);
    memset(&adapter_port, 0, sizeof(adapter_port));
    adapter_port.Size = sizeof(adapter_port);
    adapter_port.Version = 1;
    adapter_port.EndpointID = port.EndpointID;
    adapter_port.Directions = CAMD_PROVIDER_DIRECTION_OUTPUT;
    strcpy(adapter_port.Name, "Timed output");
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(170);
    descriptor.IdentityKind = 3;
    descriptor.ProtocolCapabilities = 1;
    descriptor.BackendContext = camd_legacy_output_backend_context(backend);
    descriptor.BackendOps = camd_legacy_output_backend_ops(backend);
    descriptor.Ports = &adapter_port;
    descriptor.PortCount = 1;
    assert(camd_legacy_driver_adapter_create(registry, &descriptor,
                                             &adapter) == CAMD_REGISTRY_OK);
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = port.EndpointID;
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    request.Protocol = CAMD_PROVIDER_PROTOCOL_MIDI1;
    request.QueueCapacity = 8;
    assert(camd_registry_session_open(registry, &request, &session) ==
           CAMD_REGISTRY_OK);

    /* A time in an unknown clock is refused whole. */
    events[0] = note_event(1);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = 99;
    assert(camd_registry_session_send_midi1(registry, session, events, 1) ==
           CAMD_REGISTRY_INVALID);

    start = clock_ms(NULL);
    target = start + 150;
    events[0] = note_event(10);                 /* at once */
    events[1] = note_event(11);                 /* at target */
    events[1].Flags = CAMD_EVENT_TIME_VALID;
    events[1].ClockDomain = CAMD_CLOCK_CAMD;
    events[1].TimeLow = target;
    events[2] = note_event(12);                 /* overdue, but behind 11 */
    events[2].Flags = CAMD_EVENT_TIME_VALID;
    events[2].ClockDomain = CAMD_CLOCK_CAMD;
    events[2].TimeLow = start - 1000;
    assert(camd_registry_session_send_midi1(registry, session, events, 3) ==
           CAMD_REGISTRY_OK);
    sleep_ms(40);
    assert(timed_written(&physical) == 1 && physical.notes[0] == 10);
    assert(camd_registry_session_drain(registry, session) ==
           CAMD_REGISTRY_QUEUE_FULL);
    for (tries = 0; tries < 200 && timed_written(&physical) < 3; ++tries)
        sleep_ms(5);
    assert(timed_written(&physical) == 3);
    assert(physical.notes[1] == 11 && physical.notes[2] == 12);
    /* Not early, and not much later than asked on an idle machine. */
    assert((int32_t)(physical.when[1] - target) >= 0);
    assert((int32_t)(physical.when[1] - target) < 500);
    assert(camd_registry_session_drain(registry, session) == CAMD_REGISTRY_OK);

    /* Cancelling drops a message that still waits for its time. */
    events[0] = note_event(20);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = CAMD_CLOCK_CAMD;
    events[0].TimeLow = clock_ms(NULL) + 60000;
    assert(camd_registry_session_send_midi1(registry, session, events, 1) ==
           CAMD_REGISTRY_OK);
    sleep_ms(30);
    assert(timed_written(&physical) == 3);
    assert(camd_registry_session_cancel(registry, session) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_drain(registry, session) == CAMD_REGISTRY_OK);
    events[0] = note_event(21);
    assert(camd_registry_session_send_midi1(registry, session, events, 1) ==
           CAMD_REGISTRY_OK);
    for (tries = 0; tries < 200 && timed_written(&physical) < 4; ++tries)
        sleep_ms(5);
    assert(timed_written(&physical) == 4 && physical.notes[3] == 21);

    /* Closing does not wait for a message's time either. */
    events[0] = note_event(22);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = CAMD_CLOCK_CAMD;
    events[0].TimeLow = clock_ms(NULL) + 60000;
    assert(camd_registry_session_send_midi1(registry, session, events, 1) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_session_close(registry, session) == CAMD_REGISTRY_OK);
    assert(timed_written(&physical) == 4);

    assert(camd_legacy_driver_adapter_begin_retire(adapter) ==
           CAMD_REGISTRY_OK);
    assert(camd_legacy_driver_adapter_release(adapter) == CAMD_REGISTRY_OK);
    assert(camd_legacy_output_backend_destroy(backend) == CAMD_PROVIDER_OK);
    assert(camd_native_worker_fanout_destroy(fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    camd_registry_destroy(registry);
    pthread_mutex_destroy(&physical.lock);
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
    test_input_sessions();
    test_scheduled_output();
    puts("CAMD legacy output backend OK");
    return 0;
}
