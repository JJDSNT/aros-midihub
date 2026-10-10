#include "../prototypes/camd/native_event_pump.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct sink_context {
    struct CAMDNativeEventQueue *queue;
    enum CAMDProviderResult next_result;
    unsigned int midi1_calls;
    unsigned int sysex_calls;
    unsigned int ump_calls;
    size_t midi1_records;
    size_t ump_records;
};

static struct CAMDMIDI1EventV1 midi1_event(uint8_t note)
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

static enum CAMDProviderResult submit_midi1(
    void *opaque, const struct CAMDMIDI1EventV1 *events, size_t count)
{
    struct sink_context *context = opaque;
    size_t cancelled = 99;

    assert(events && count != 0);
    ++context->midi1_calls;
    context->midi1_records += count;
    /* The callback is outside the queue lock, but cancellation cannot remove
       the checked-out head while the downstream decision is pending. */
    assert(camd_native_queue_cancel(context->queue, &cancelled) ==
           CAMD_NATIVE_QUEUE_BUSY);
    return context->next_result;
}

static enum CAMDProviderResult submit_sysex(
    void *opaque, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    struct sink_context *context = opaque;

    assert(bytes && byte_count == 4 && bytes[0] == 0xf0 && bytes[3] == 0xf7);
    assert(time_high == 1 && time_low == 2 && clock_domain == 3 && flags == 4);
    ++context->sysex_calls;
    return context->next_result;
}

static enum CAMDProviderResult submit_ump(
    void *opaque, const struct CAMDUMPEventV1 *events, size_t count)
{
    struct sink_context *context = opaque;

    assert(events && count != 0);
    ++context->ump_calls;
    context->ump_records += count;
    return context->next_result;
}

static void test_midi1_retry_and_budget(void)
{
    struct CAMDNativeQueueConfigV1 queue_config;
    struct CAMDNativePumpConfigV1 pump_config;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDNativeEventQueue *queue = NULL;
    struct CAMDNativeEventPump *pump = NULL;
    struct CAMDMIDI1EventV1 events[2];
    struct CAMDNativeQueueStatsV1 stats;
    struct sink_context context;
    enum CAMDProviderResult downstream;
    const uint8_t sysex[] = { 0xf0, 1, 2, 0xf7 };
    size_t items, records, cancelled;

    memset(&queue_config, 0, sizeof(queue_config));
    queue_config.Size = sizeof(queue_config);
    queue_config.Version = 1;
    queue_config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    queue_config.Capacity = 4;
    queue_config.MaxSysExBytes = sizeof(sysex);
    assert(camd_native_queue_create(&queue_config, &queue) ==
           CAMD_NATIVE_QUEUE_OK);

    memset(&context, 0, sizeof(context));
    context.queue = queue;
    context.next_result = CAMD_PROVIDER_QUEUE_FULL;
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &context;
    sink.SubmitMIDI1 = submit_midi1;
    sink.SubmitMIDI1SysEx = submit_sysex;
    memset(&pump_config, 0, sizeof(pump_config));
    pump_config.Size = sizeof(pump_config);
    pump_config.Version = 1;
    pump_config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    pump_config.MaxBatchRecords = 4;
    pump_config.MaxSysExBytes = sizeof(sysex);
    assert(camd_native_pump_create(queue, &pump_config, &sink, &pump) ==
           CAMD_NATIVE_PUMP_OK);

    events[0] = midi1_event(60);
    events[1] = midi1_event(61);
    assert(camd_native_queue_enqueue_midi1(queue, events, 2) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_enqueue_midi1_sysex(
               queue, sysex, sizeof(sysex), 1, 2, 3, 4) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_pump_run(pump, 2, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_BLOCKED);
    assert(items == 0 && records == 0);
    assert(downstream == CAMD_PROVIDER_QUEUE_FULL);

    memset(&stats, 0, sizeof(stats));
    stats.Size = sizeof(stats);
    stats.Version = 1;
    assert(camd_native_queue_stats(queue, &stats) == CAMD_NATIVE_QUEUE_OK);
    assert(stats.PendingRecords == 3 && stats.DequeuedRecords == 0);

    context.next_result = CAMD_PROVIDER_OK;
    assert(camd_native_pump_run(pump, 1, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_OK);
    assert(items == 1 && records == 2);
    assert(camd_native_pump_run(pump, 2, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_OK);
    assert(items == 1 && records == 1 && context.sysex_calls == 1);
    assert(camd_native_pump_run(pump, 1, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_EMPTY);

    assert(camd_native_queue_enqueue_midi1(queue, events, 2) ==
           CAMD_NATIVE_QUEUE_OK);
    context.next_result = CAMD_PROVIDER_CALLBACK_FAILED;
    assert(camd_native_pump_run(pump, 1, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_DOWNSTREAM);
    assert(items == 0 && records == 0);
    assert(downstream == CAMD_PROVIDER_CALLBACK_FAILED);
    assert(camd_native_queue_cancel(queue, &cancelled) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(cancelled == 2);
    camd_native_pump_destroy(pump);
    camd_native_queue_destroy(queue);
}

static void test_ump_pump(void)
{
    struct CAMDNativeQueueConfigV1 queue_config;
    struct CAMDNativePumpConfigV1 pump_config;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDNativeEventQueue *queue = NULL;
    struct CAMDNativeEventPump *pump = NULL;
    struct CAMDUMPEventV1 event;
    struct sink_context context;
    enum CAMDProviderResult downstream;
    size_t items, records;

    memset(&queue_config, 0, sizeof(queue_config));
    queue_config.Size = sizeof(queue_config);
    queue_config.Version = 1;
    queue_config.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    queue_config.Capacity = 2;
    assert(camd_native_queue_create(&queue_config, &queue) ==
           CAMD_NATIVE_QUEUE_OK);
    memset(&context, 0, sizeof(context));
    context.next_result = CAMD_PROVIDER_OK;
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.Context = &context;
    sink.SubmitUMP = submit_ump;
    memset(&pump_config, 0, sizeof(pump_config));
    pump_config.Size = sizeof(pump_config);
    pump_config.Version = 1;
    pump_config.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    pump_config.MaxBatchRecords = 2;
    assert(camd_native_pump_create(queue, &pump_config, &sink, &pump) ==
           CAMD_NATIVE_PUMP_OK);
    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = 1;
    event.Words[0] = 0x20903c64;
    assert(camd_native_queue_enqueue_ump(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_enqueue_ump(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_pump_run(pump, 1, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_OK);
    assert(items == 1 && records == 1);
    assert(camd_native_pump_run(pump, 2, &items, &records, &downstream) ==
           CAMD_NATIVE_PUMP_OK);
    assert(items == 1 && records == 1 && context.ump_records == 2);
    camd_native_pump_destroy(pump);
    camd_native_queue_destroy(queue);
}

int main(void)
{
    test_midi1_retry_and_budget();
    test_ump_pump();
    puts("CAMD native event pump OK");
    return 0;
}
