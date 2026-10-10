#include "../prototypes/camd/native_event_worker.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct sink_context {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    enum CAMDProviderResult result;
    pthread_t callback_thread;
    size_t calls;
    size_t accepted;
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
    enum CAMDProviderResult result;

    assert(events && count != 0);
    pthread_mutex_lock(&context->lock);
    context->callback_thread = pthread_self();
    ++context->calls;
    result = context->result;
    if (result == CAMD_PROVIDER_OK)
        context->accepted += count;
    pthread_cond_broadcast(&context->condition);
    pthread_mutex_unlock(&context->lock);
    return result;
}

static enum CAMDProviderResult submit_sysex(
    void *opaque, const uint8_t *bytes, size_t byte_count,
    uint32_t time_high, uint32_t time_low, uint32_t clock_domain,
    uint32_t flags)
{
    (void)opaque;
    (void)bytes;
    (void)byte_count;
    (void)time_high;
    (void)time_low;
    (void)clock_domain;
    (void)flags;
    return CAMD_PROVIDER_OK;
}

static void wait_for_calls(struct sink_context *context, size_t expected)
{
    struct timespec deadline;

    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;
    pthread_mutex_lock(&context->lock);
    while (context->calls < expected) {
        int result = pthread_cond_timedwait(&context->condition,
                                             &context->lock, &deadline);

        assert(result == 0 || result == EINTR);
    }
    pthread_mutex_unlock(&context->lock);
}

static void wait_for_accepted(struct sink_context *context, size_t expected)
{
    struct timespec deadline;

    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;
    pthread_mutex_lock(&context->lock);
    while (context->accepted < expected) {
        int result = pthread_cond_timedwait(&context->condition,
                                             &context->lock, &deadline);

        assert(result == 0 || result == EINTR);
    }
    pthread_mutex_unlock(&context->lock);
}

int main(void)
{
    struct CAMDNativeQueueConfigV1 queue_config;
    struct CAMDNativePumpConfigV1 pump_config;
    struct CAMDNativeWorkerConfigV1 worker_config;
    struct CAMDProviderReceiveSinkV1 sink;
    struct CAMDNativeWorkerStatsV1 worker_stats;
    struct CAMDNativeQueueStatsV1 queue_stats;
    struct CAMDNativeEventQueue *queue = NULL;
    struct CAMDNativeEventPump *pump = NULL;
    struct CAMDNativeEventWorker *worker = NULL;
    struct sink_context context;
    struct CAMDMIDI1EventV1 event;
    pthread_t main_thread = pthread_self();
    size_t calls;

    memset(&context, 0, sizeof(context));
    assert(pthread_mutex_init(&context.lock, NULL) == 0);
    assert(pthread_cond_init(&context.condition, NULL) == 0);
    context.result = CAMD_PROVIDER_QUEUE_FULL;

    memset(&queue_config, 0, sizeof(queue_config));
    queue_config.Size = sizeof(queue_config);
    queue_config.Version = 1;
    queue_config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    queue_config.Capacity = 8;
    queue_config.MaxSysExBytes = 64;
    assert(camd_native_queue_create(&queue_config, &queue) ==
           CAMD_NATIVE_QUEUE_OK);
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
    pump_config.MaxBatchRecords = 8;
    pump_config.MaxSysExBytes = 64;
    assert(camd_native_pump_create(queue, &pump_config, &sink, &pump) ==
           CAMD_NATIVE_PUMP_OK);
    memset(&worker_config, 0, sizeof(worker_config));
    worker_config.Size = sizeof(worker_config);
    worker_config.Version = 1;
    worker_config.ItemBudget = 1;
    assert(camd_native_worker_create(pump, &worker_config, &worker) ==
           CAMD_NATIVE_WORKER_OK);

    event = midi1_event(60);
    assert(camd_native_queue_enqueue_midi1(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    event = midi1_event(61);
    assert(camd_native_queue_enqueue_midi1(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    event = midi1_event(62);
    assert(camd_native_queue_enqueue_midi1(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_OK);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_OK);
    wait_for_calls(&context, 1);
    assert(!pthread_equal(main_thread, context.callback_thread));

    pthread_mutex_lock(&context.lock);
    context.result = CAMD_PROVIDER_OK;
    calls = context.calls;
    pthread_mutex_unlock(&context.lock);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_OK);
    wait_for_accepted(&context, 3);
    wait_for_calls(&context, calls + 3);

    event = midi1_event(63);
    assert(camd_native_queue_enqueue_midi1(queue, &event, 1) ==
           CAMD_NATIVE_QUEUE_OK);
    pthread_mutex_lock(&context.lock);
    context.result = CAMD_PROVIDER_CALLBACK_FAILED;
    calls = context.calls;
    pthread_mutex_unlock(&context.lock);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_OK);
    wait_for_calls(&context, calls + 1);
    pthread_mutex_lock(&context.lock);
    context.result = CAMD_PROVIDER_OK;
    pthread_mutex_unlock(&context.lock);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_OK);
    wait_for_accepted(&context, 4);

    memset(&worker_stats, 0, sizeof(worker_stats));
    worker_stats.Size = sizeof(worker_stats);
    worker_stats.Version = 1;
    assert(camd_native_worker_stats(worker, &worker_stats) ==
           CAMD_NATIVE_WORKER_OK);
    assert(worker_stats.WakeRequests >= 5);
    assert(worker_stats.ProcessedItems == 4);
    assert(worker_stats.ProcessedRecords == 4);
    assert(worker_stats.BlockedRuns >= 1);
    assert(worker_stats.DownstreamFailures == 1);

    assert(camd_native_worker_destroy(worker) == CAMD_NATIVE_WORKER_STATE);
    assert(camd_native_worker_stop(worker) == CAMD_NATIVE_WORKER_OK);
    assert(camd_native_worker_stop(worker) == CAMD_NATIVE_WORKER_OK);
    assert(camd_native_worker_wake(worker) == CAMD_NATIVE_WORKER_STATE);
    assert(camd_native_worker_destroy(worker) == CAMD_NATIVE_WORKER_OK);
    memset(&queue_stats, 0, sizeof(queue_stats));
    queue_stats.Size = sizeof(queue_stats);
    queue_stats.Version = 1;
    assert(camd_native_queue_stats(queue, &queue_stats) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(queue_stats.PendingRecords == 0);
    camd_native_pump_destroy(pump);
    camd_native_queue_destroy(queue);
    pthread_cond_destroy(&context.condition);
    pthread_mutex_destroy(&context.lock);
    puts("CAMD native event worker OK");
    return 0;
}
