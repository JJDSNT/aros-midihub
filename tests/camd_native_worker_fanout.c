#include "../prototypes/camd/native_worker_fanout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct pipeline {
    struct CAMDNativeEventQueue *queue;
    struct CAMDNativeEventPump *pump;
    struct CAMDNativeEventWorker *worker;
};

static enum CAMDProviderResult submit_midi1(
    void *context, const struct CAMDMIDI1EventV1 *events, size_t event_count)
{
    (void)context;
    (void)events;
    (void)event_count;
    return CAMD_PROVIDER_OK;
}

static enum CAMDProviderResult submit_sysex(
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

static void pipeline_create(struct pipeline *pipeline)
{
    struct CAMDNativeQueueConfigV1 queue_config;
    struct CAMDNativePumpConfigV1 pump_config;
    struct CAMDNativeWorkerConfigV1 worker_config;
    struct CAMDProviderReceiveSinkV1 sink;

    memset(pipeline, 0, sizeof(*pipeline));
    memset(&queue_config, 0, sizeof(queue_config));
    queue_config.Size = sizeof(queue_config);
    queue_config.Version = 1;
    queue_config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    queue_config.Capacity = 2;
    queue_config.MaxSysExBytes = 16;
    assert(camd_native_queue_create(&queue_config, &pipeline->queue) ==
           CAMD_NATIVE_QUEUE_OK);
    memset(&sink, 0, sizeof(sink));
    sink.Size = sizeof(sink);
    sink.Version = 1;
    sink.SubmitMIDI1 = submit_midi1;
    sink.SubmitMIDI1SysEx = submit_sysex;
    memset(&pump_config, 0, sizeof(pump_config));
    pump_config.Size = sizeof(pump_config);
    pump_config.Version = 1;
    pump_config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    pump_config.MaxBatchRecords = 1;
    pump_config.MaxSysExBytes = 16;
    assert(camd_native_pump_create(pipeline->queue, &pump_config, &sink,
                                   &pipeline->pump) == CAMD_NATIVE_PUMP_OK);
    memset(&worker_config, 0, sizeof(worker_config));
    worker_config.Size = sizeof(worker_config);
    worker_config.Version = 1;
    worker_config.ItemBudget = 1;
    assert(camd_native_worker_create(pipeline->pump, &worker_config,
                                     &pipeline->worker) ==
           CAMD_NATIVE_WORKER_OK);
}

static void pipeline_destroy(struct pipeline *pipeline)
{
    assert(camd_native_worker_stop(pipeline->worker) ==
           CAMD_NATIVE_WORKER_OK);
    assert(camd_native_worker_destroy(pipeline->worker) ==
           CAMD_NATIVE_WORKER_OK);
    camd_native_pump_destroy(pipeline->pump);
    camd_native_queue_destroy(pipeline->queue);
}

static uint64_t wake_requests(struct CAMDNativeEventWorker *worker)
{
    struct CAMDNativeWorkerStatsV1 stats;

    memset(&stats, 0, sizeof(stats));
    stats.Size = sizeof(stats);
    stats.Version = 1;
    assert(camd_native_worker_stats(worker, &stats) == CAMD_NATIVE_WORKER_OK);
    return stats.WakeRequests;
}

int main(void)
{
    struct CAMDNativeWorkerFanout *fanout = NULL;
    struct CAMDNativeWorkerFanoutHandleV1 handles[2], duplicate;
    struct pipeline pipelines[2];
    uint32_t woken = 99;

    pipeline_create(&pipelines[0]);
    pipeline_create(&pipelines[1]);
    assert(camd_native_worker_fanout_create(2, &fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(camd_native_worker_fanout_attach(fanout, pipelines[0].worker,
                                            &handles[0]) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(camd_native_worker_fanout_attach(fanout, pipelines[0].worker,
                                            &duplicate) ==
           CAMD_NATIVE_WORKER_FANOUT_INVALID);
    assert(camd_native_worker_fanout_attach(fanout, pipelines[1].worker,
                                            &handles[1]) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(camd_native_worker_fanout_destroy(fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_STATE);

    assert(camd_native_worker_fanout_wake_all(fanout, &woken) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(woken == 2);
    assert(wake_requests(pipelines[0].worker) == 1);
    assert(wake_requests(pipelines[1].worker) == 1);

    assert(camd_native_worker_fanout_detach(fanout, handles[0]) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(camd_native_worker_fanout_detach(fanout, handles[0]) ==
           CAMD_NATIVE_WORKER_FANOUT_STATE);
    assert(camd_native_worker_fanout_wake_all(fanout, &woken) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(woken == 1);
    assert(wake_requests(pipelines[0].worker) == 1);
    assert(wake_requests(pipelines[1].worker) == 2);

    assert(camd_native_worker_fanout_detach(fanout, handles[1]) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    assert(camd_native_worker_fanout_destroy(fanout) ==
           CAMD_NATIVE_WORKER_FANOUT_OK);
    pipeline_destroy(&pipelines[0]);
    pipeline_destroy(&pipelines[1]);
    puts("CAMD native worker fan-out OK");
    return 0;
}
