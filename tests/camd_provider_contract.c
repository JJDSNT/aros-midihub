#include "../prototypes/camd/provider_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct software_provider {
    unsigned int opens;
    unsigned int closes;
    unsigned int midi1_batches;
    unsigned int sysex_batches;
    unsigned int ump_batches;
    unsigned int receive_starts;
    unsigned int receive_stops;
    unsigned int received_midi1;
    unsigned int received_sysex;
    unsigned int received_ump;
    unsigned int drains;
    unsigned int cancels;
    unsigned int shutdowns;
    struct CAMDMIDI1EventV1 last_midi1;
    struct CAMDUMPEventV1 last_ump;
    uint8_t last_sysex[16];
    size_t last_sysex_size;
    struct CAMDProviderReceiveSinkV1 last_sink;
};

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x43414d44u, 0, 0, value } };
    return id;
}

static enum CAMDProviderResult software_open(
    void *context, const struct CAMDProviderOpenRequestV1 *request,
    void **session_context)
{
    struct software_provider *provider = context;

    ++provider->opens;
    assert(request != NULL);
    *session_context = context;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_close(void *context,
                                               void *session_context)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    ++provider->closes;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_send_midi1(
    void *context, void *session_context,
    const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    assert(event_count == 1);
    ++provider->midi1_batches;
    provider->last_midi1 = events[0];
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_send_sysex(
    void *context, void *session_context, const uint8_t *bytes,
    size_t byte_count, uint32_t time_high, uint32_t time_low,
    uint32_t clock_domain, uint32_t flags)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    assert(byte_count <= sizeof(provider->last_sysex));
    assert(time_high == 1 && time_low == 2);
    assert(clock_domain == 3 && flags == 4);
    ++provider->sysex_batches;
    memcpy(provider->last_sysex, bytes, byte_count);
    provider->last_sysex_size = byte_count;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_send_ump(
    void *context, void *session_context,
    const struct CAMDUMPEventV1 *events, size_t event_count)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    assert(event_count == 1);
    ++provider->ump_batches;
    provider->last_ump = events[0];
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_start_receive(
    void *context, void *session_context,
    const struct CAMDProviderReceiveSinkV1 *sink)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    assert(sink != NULL);
    ++provider->receive_starts;
    provider->last_sink = *sink;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_stop_receive(void *context,
                                                      void *session_context)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    ++provider->receive_stops;
    memset(&provider->last_sink, 0, sizeof(provider->last_sink));
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult receive_midi1(
    void *context, const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    struct software_provider *provider = context;

    assert(events != NULL && event_count == 1);
    ++provider->received_midi1;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult receive_sysex(
    void *context, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    struct software_provider *provider = context;

    assert(bytes != NULL && byte_count >= 2);
    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    ++provider->received_sysex;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult receive_ump(
    void *context, const struct CAMDUMPEventV1 *events, size_t event_count)
{
    struct software_provider *provider = context;

    assert(events != NULL && event_count == 1);
    ++provider->received_ump;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_drain(void *context,
                                               void *session_context)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    ++provider->drains;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_cancel(void *context,
                                                void *session_context)
{
    struct software_provider *provider = context;

    assert(session_context != NULL);
    ++provider->cancels;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult software_shutdown(void *context)
{
    struct software_provider *provider = context;

    ++provider->shutdowns;
    return CAMD_PROVIDER_OK;
}

static int software_shutdown_ready(void *context)
{
    struct software_provider *provider = context;

    return provider->shutdowns != 0;
}

static struct CAMDProviderOpsV1 make_ops(void)
{
    struct CAMDProviderOpsV1 operations;

    memset(&operations, 0, sizeof(operations));
    operations.Size = sizeof(operations);
    operations.Version = 1;
    operations.Open = software_open;
    operations.Close = software_close;
    operations.SendMIDI1 = software_send_midi1;
    operations.SendMIDI1SysEx = software_send_sysex;
    operations.SendUMP = software_send_ump;
    operations.StartReceive = software_start_receive;
    operations.StopReceive = software_stop_receive;
    operations.Drain = software_drain;
    operations.Cancel = software_cancel;
    operations.BeginShutdown = software_shutdown;
    operations.ShutdownReady = software_shutdown_ready;
    return operations;
}

static struct CAMDProviderDescriptorV1 make_descriptor(
    struct software_provider *context,
    const struct CAMDProviderOpsV1 *operations,
    uint32_t paths)
{
    struct CAMDProviderDescriptorV1 descriptor;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.Size = sizeof(descriptor);
    descriptor.Version = 1;
    descriptor.ProviderID = make_id(0x2000u);
    descriptor.NativePaths = paths;
    descriptor.Directions = CAMD_PROVIDER_DIRECTION_ALL;
    descriptor.Context = context;
    descriptor.Ops = operations;
    return descriptor;
}

static struct CAMDProviderOpenRequestV1 make_request(uint32_t format,
                                                      uint32_t protocol)
{
    struct CAMDProviderOpenRequestV1 request;

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = make_id(0x2001u);
    request.Direction = CAMD_PROVIDER_DIRECTION_OUTPUT;
    request.DataFormat = format;
    request.Protocol = protocol;
    request.QueueCapacity = 16;
    return request;
}

static void test_descriptor_validation(void)
{
    struct software_provider context;
    struct CAMDPrivateProvider provider;
    struct CAMDProviderOpsV1 operations = make_ops();
    struct CAMDProviderDescriptorV1 descriptor;

    memset(&context, 0, sizeof(context));
    descriptor = make_descriptor(&context, &operations,
                                 CAMD_PROVIDER_PATH_MIDI1);
    operations.SendMIDI1SysEx = NULL;
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_INVALID);
    operations = make_ops();
    descriptor = make_descriptor(&context, &operations,
                                 CAMD_PROVIDER_PATH_UMP_MIDI2);
    operations.SendUMP = NULL;
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_INVALID);
    descriptor.NativePaths = 0;
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_INVALID);

    operations = make_ops();
    operations.SendMIDI1 = NULL;
    operations.SendMIDI1SysEx = NULL;
    operations.SendUMP = NULL;
    operations.Drain = NULL;
    operations.Cancel = NULL;
    descriptor = make_descriptor(&context, &operations,
                                 CAMD_PROVIDER_PATH_ALL);
    descriptor.Directions = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_OK);

    operations = make_ops();
    operations.StartReceive = NULL;
    operations.StopReceive = NULL;
    descriptor = make_descriptor(&context, &operations,
                                 CAMD_PROVIDER_PATH_ALL);
    descriptor.Directions = CAMD_PROVIDER_DIRECTION_OUTPUT;
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_OK);
}

static void test_exact_native_paths(void)
{
    static const uint8_t sysex[] = { 0xf0, 0x7d, 0x01, 0xf7 };
    struct software_provider context;
    struct CAMDPrivateProvider provider;
    struct CAMDPrivateProviderSession midi1, ump1, ump2, midi1_in, ump_in;
    struct CAMDProviderOpsV1 operations = make_ops();
    struct CAMDProviderDescriptorV1 descriptor;
    struct CAMDProviderOpenRequestV1 request;
    struct CAMDMIDI1EventV1 midi1_event;
    struct CAMDUMPEventV1 ump_event;
    struct CAMDProviderReceiveSinkV1 sink;

    memset(&context, 0, sizeof(context));
    descriptor = make_descriptor(&context, &operations, CAMD_PROVIDER_PATH_ALL);
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_OK);
    assert(provider.descriptor.Ops != &operations);

    request = make_request(CAMD_PROVIDER_FORMAT_MIDI1,
                           CAMD_PROVIDER_PROTOCOL_MIDI1);
    assert(camd_provider_open(&provider, &request, &midi1) == CAMD_PROVIDER_OK);
    request = make_request(CAMD_PROVIDER_FORMAT_UMP,
                           CAMD_PROVIDER_PROTOCOL_MIDI1);
    assert(camd_provider_open(&provider, &request, &ump1) == CAMD_PROVIDER_OK);
    request = make_request(CAMD_PROVIDER_FORMAT_UMP,
                           CAMD_PROVIDER_PROTOCOL_MIDI2);
    assert(camd_provider_open(&provider, &request, &ump2) == CAMD_PROVIDER_OK);
    request = make_request(CAMD_PROVIDER_FORMAT_MIDI1,
                           CAMD_PROVIDER_PROTOCOL_MIDI1);
    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_provider_open(&provider, &request, &midi1_in) ==
           CAMD_PROVIDER_OK);
    request = make_request(CAMD_PROVIDER_FORMAT_UMP,
                           CAMD_PROVIDER_PROTOCOL_MIDI2);
    request.Direction = CAMD_PROVIDER_DIRECTION_INPUT;
    assert(camd_provider_open(&provider, &request, &ump_in) == CAMD_PROVIDER_OK);
    assert(context.opens == 5 && provider.active_sessions == 5);

    memset(&midi1_event, 0, sizeof(midi1_event));
    midi1_event.Size = sizeof(midi1_event);
    midi1_event.Version = 1;
    midi1_event.Length = 3;
    midi1_event.Bytes[0] = 0x90;
    midi1_event.Bytes[1] = 60;
    midi1_event.Bytes[2] = 100;
    midi1_event.TimeLow = 123;
    assert(camd_provider_send_midi1(&midi1, &midi1_event, 1) ==
           CAMD_PROVIDER_OK);
    assert(context.midi1_batches == 1);
    assert(memcmp(&context.last_midi1, &midi1_event,
                  sizeof(midi1_event)) == 0);
    midi1_event.Length = 2;
    assert(camd_provider_send_midi1(&midi1, &midi1_event, 1) ==
           CAMD_PROVIDER_INVALID);
    midi1_event.Length = 3;
    midi1_event.Bytes[1] = 0x80;
    assert(camd_provider_send_midi1(&midi1, &midi1_event, 1) ==
           CAMD_PROVIDER_INVALID);
    midi1_event.Bytes[1] = 60;
    assert(camd_provider_send_ump(&midi1, &ump_event, 1) ==
           CAMD_PROVIDER_UNSUPPORTED);
    assert(context.ump_batches == 0);

    assert(camd_provider_send_midi1_sysex(&midi1, sysex, sizeof(sysex),
                                          1, 2, 3, 4) == CAMD_PROVIDER_OK);
    assert(context.sysex_batches == 1);
    assert(context.last_sysex_size == sizeof(sysex));
    assert(memcmp(context.last_sysex, sysex, sizeof(sysex)) == 0);

    memset(&ump_event, 0, sizeof(ump_event));
    ump_event.Size = sizeof(ump_event);
    ump_event.Version = 1;
    ump_event.WordCount = 2;
    ump_event.Words[0] = 0x40903c00u;
    ump_event.Words[1] = 0xffff0000u;
    assert(camd_provider_send_ump(&ump1, &ump_event, 1) == CAMD_PROVIDER_OK);
    assert(camd_provider_send_ump(&ump2, &ump_event, 1) == CAMD_PROVIDER_OK);
    assert(context.ump_batches == 2);
    assert(memcmp(&context.last_ump, &ump_event, sizeof(ump_event)) == 0);
    assert(camd_provider_send_midi1(&ump1, &midi1_event, 1) ==
           CAMD_PROVIDER_UNSUPPORTED);
    assert(context.midi1_batches == 1);

    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &context;
    sink.SubmitMIDI1 = receive_midi1;
    sink.SubmitMIDI1SysEx = receive_sysex;
    sink.SubmitUMP = receive_ump;
    assert(camd_provider_start_receive(&midi1_in, &sink) == CAMD_PROVIDER_OK);
    assert(context.last_sink.SubmitMIDI1 != NULL);
    assert(context.last_sink.SubmitMIDI1SysEx != NULL);
    assert(context.last_sink.SubmitUMP == NULL);
    assert(context.last_sink.SubmitMIDI1(context.last_sink.Context,
                                         &midi1_event, 1) == CAMD_PROVIDER_OK);
    assert(context.last_sink.SubmitMIDI1SysEx(
               context.last_sink.Context, sysex, sizeof(sysex), 0, 0, 0, 0) ==
           CAMD_PROVIDER_OK);
    assert(context.received_midi1 == 1 && context.received_sysex == 1);
    assert(camd_provider_send_midi1(&midi1_in, &midi1_event, 1) ==
           CAMD_PROVIDER_UNSUPPORTED);
    assert(camd_provider_drain(&midi1_in) == CAMD_PROVIDER_UNSUPPORTED);
    assert(camd_provider_close(&midi1_in) == CAMD_PROVIDER_STATE);
    assert(camd_provider_stop_receive(&midi1_in) == CAMD_PROVIDER_OK);

    assert(camd_provider_start_receive(&ump_in, &sink) == CAMD_PROVIDER_OK);
    assert(context.last_sink.SubmitMIDI1 == NULL);
    assert(context.last_sink.SubmitMIDI1SysEx == NULL);
    assert(context.last_sink.SubmitUMP != NULL);
    assert(context.last_sink.SubmitUMP(context.last_sink.Context,
                                       &ump_event, 1) == CAMD_PROVIDER_OK);
    assert(context.received_ump == 1);
    assert(camd_provider_stop_receive(&ump_in) == CAMD_PROVIDER_OK);
    assert(context.receive_starts == 2 && context.receive_stops == 2);

    assert(camd_provider_drain(&midi1) == CAMD_PROVIDER_OK);
    assert(camd_provider_cancel(&ump1) == CAMD_PROVIDER_OK);
    assert(context.drains == 1 && context.cancels == 1);

    assert(camd_provider_begin_retire(&provider) == CAMD_PROVIDER_OK);
    assert(context.shutdowns == 1);
    assert(!camd_provider_ready_to_release(&provider));
    assert(camd_provider_open(&provider, &request, &midi1) ==
           CAMD_PROVIDER_RETIRED);
    assert(camd_provider_send_ump(&ump2, &ump_event, 1) ==
           CAMD_PROVIDER_RETIRED);
    assert(camd_provider_drain(&ump2) == CAMD_PROVIDER_OK);
    assert(camd_provider_cancel(&ump2) == CAMD_PROVIDER_OK);
    assert(camd_provider_close(&midi1) == CAMD_PROVIDER_OK);
    assert(camd_provider_close(&ump1) == CAMD_PROVIDER_OK);
    assert(camd_provider_close(&ump2) == CAMD_PROVIDER_OK);
    assert(camd_provider_close(&midi1_in) == CAMD_PROVIDER_OK);
    assert(camd_provider_close(&ump_in) == CAMD_PROVIDER_OK);
    assert(context.closes == 5);
    assert(camd_provider_ready_to_release(&provider));
}

static void test_unsupported_combinations(void)
{
    struct software_provider context;
    struct CAMDPrivateProvider provider;
    struct CAMDPrivateProviderSession session;
    struct CAMDProviderOpsV1 operations = make_ops();
    struct CAMDProviderDescriptorV1 descriptor;
    struct CAMDProviderOpenRequestV1 request;

    memset(&context, 0, sizeof(context));
    descriptor = make_descriptor(&context, &operations,
                                 CAMD_PROVIDER_PATH_MIDI1);
    assert(camd_provider_init(&provider, &descriptor) == CAMD_PROVIDER_OK);
    request = make_request(CAMD_PROVIDER_FORMAT_UMP,
                           CAMD_PROVIDER_PROTOCOL_MIDI1);
    assert(camd_provider_open(&provider, &request, &session) ==
           CAMD_PROVIDER_UNSUPPORTED);
    request = make_request(CAMD_PROVIDER_FORMAT_MIDI1,
                           CAMD_PROVIDER_PROTOCOL_MIDI2);
    assert(camd_provider_open(&provider, &request, &session) ==
           CAMD_PROVIDER_UNSUPPORTED);
    assert(context.opens == 0);
}

int main(void)
{
    assert(sizeof(struct CAMDMIDI1EventV1) == 32);
    assert(sizeof(struct CAMDUMPEventV1) == 44);
    test_descriptor_validation();
    test_exact_native_paths();
    test_unsupported_combinations();
    puts("CAMD private provider contract OK");
    return 0;
}
