#include "../prototypes/camd/native_event_queue.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

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
    event.TimeLow = note;
    return event;
}

static struct CAMDUMPEventV1 ump_event(uint32_t word_count)
{
    struct CAMDUMPEventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = word_count;
    event.Words[0] = word_count == 4 ? 0x50000000u : 0x20903c64u;
    event.Words[1] = 0x11223344u;
    event.Words[2] = 0x55667788u;
    event.Words[3] = 0x99aabbccu;
    event.TimeHigh = 1;
    event.TimeLow = 2;
    event.ClockDomain = 3;
    return event;
}

static void test_midi1_queue(void)
{
    struct CAMDNativeQueueConfigV1 config;
    struct CAMDNativeEventQueue *queue = NULL;
    struct CAMDMIDI1EventV1 input[2], output[2];
    struct CAMDNativeQueueHeadV1 head;
    struct CAMDNativeQueueStatsV1 stats;
    struct CAMDNativeQueueSysExInfoV1 sysex_info;
    struct CAMDUMPEventV1 ump = ump_event(1);
    const uint8_t sysex[] = { 0xf0, 0x01, 0x02, 0xf7 };
    const uint8_t oversized[] = { 0xf0, 1, 2, 3, 0xf7 };
    uint8_t sysex_output[4];
    size_t count, cancelled;

    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.DataFormat = CAMD_PROVIDER_FORMAT_MIDI1;
    config.Capacity = 3;
    config.MaxSysExBytes = 4;
    assert(camd_native_queue_create(&config, &queue) == CAMD_NATIVE_QUEUE_OK);

    input[0] = midi1_event(60);
    input[1] = midi1_event(61);
    assert(camd_native_queue_enqueue_midi1(queue, input, 2) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_enqueue_midi1(queue, input, 2) ==
           CAMD_NATIVE_QUEUE_FULL);

    memset(&head, 0, sizeof(head));
    head.Size = sizeof(head);
    head.Version = 1;
    assert(camd_native_queue_peek(queue, &head) == CAMD_NATIVE_QUEUE_OK);
    assert(head.Kind == CAMD_NATIVE_QUEUE_ITEM_MIDI1);
    assert(head.RecordCount == 2 && head.ByteCount == 0);

    count = 0;
    assert(camd_native_queue_dequeue_midi1(queue, output, 1, &count) ==
           CAMD_NATIVE_QUEUE_RANGE);
    assert(count == 2);
    assert(camd_native_queue_dequeue_midi1(queue, output, 2, &count) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(count == 2);
    assert(memcmp(input, output, sizeof(input)) == 0);

    assert(camd_native_queue_enqueue_midi1_sysex(
               queue, oversized, sizeof(oversized), 1, 2, 3, 4) ==
           CAMD_NATIVE_QUEUE_TOO_LARGE);
    assert(camd_native_queue_enqueue_midi1_sysex(
               queue, sysex, sizeof(sysex), 1, 2, 3, 4) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_enqueue_ump(queue, &ump, 1) ==
           CAMD_NATIVE_QUEUE_WRONG_FORMAT);
    assert(camd_native_queue_peek(queue, &head) == CAMD_NATIVE_QUEUE_OK);
    assert(head.Kind == CAMD_NATIVE_QUEUE_ITEM_MIDI1_SYSEX);
    assert(head.RecordCount == 1 && head.ByteCount == sizeof(sysex));

    memset(&sysex_info, 0, sizeof(sysex_info));
    sysex_info.Size = sizeof(sysex_info);
    sysex_info.Version = 1;
    assert(camd_native_queue_dequeue_midi1_sysex(
               queue, sysex_output, sizeof(sysex_output) - 1, &sysex_info) ==
           CAMD_NATIVE_QUEUE_RANGE);
    assert(sysex_info.ByteCount == sizeof(sysex));
    assert(camd_native_queue_dequeue_midi1_sysex(
               queue, sysex_output, sizeof(sysex_output), &sysex_info) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(memcmp(sysex, sysex_output, sizeof(sysex)) == 0);
    assert(sysex_info.TimeHigh == 1 && sysex_info.TimeLow == 2);
    assert(sysex_info.ClockDomain == 3 && sysex_info.Flags == 4);

    assert(camd_native_queue_enqueue_midi1(queue, input, 2) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_cancel(queue, &cancelled) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(cancelled == 2);
    assert(camd_native_queue_peek(queue, &head) == CAMD_NATIVE_QUEUE_EMPTY);

    memset(&stats, 0, sizeof(stats));
    stats.Size = sizeof(stats);
    stats.Version = 1;
    assert(camd_native_queue_stats(queue, &stats) == CAMD_NATIVE_QUEUE_OK);
    assert(stats.AcceptedRecords == 5);
    assert(stats.DequeuedRecords == 3);
    assert(stats.CancelledRecords == 2);
    assert(stats.FullRejections == 1);
    assert(stats.OversizeRejections == 1);
    assert(stats.PendingRecords == 0 && stats.HighWaterRecords == 2);

    /* The two-record batch crosses the physical end of the three-slot ring. */
    assert(camd_native_queue_enqueue_midi1(queue, input, 2) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(camd_native_queue_dequeue_midi1(queue, output, 2, &count) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(count == 2 && memcmp(input, output, sizeof(input)) == 0);
    camd_native_queue_destroy(queue);
}

struct producer_context {
    struct CAMDNativeEventQueue *queue;
    struct CAMDUMPEventV1 event;
    unsigned int accepted;
    unsigned int full;
};

static void *produce_ump(void *opaque)
{
    struct producer_context *context = opaque;
    unsigned int i;

    for (i = 0; i < 100; ++i) {
        enum CAMDNativeQueueResult result =
            camd_native_queue_enqueue_ump(context->queue, &context->event, 1);

        if (result == CAMD_NATIVE_QUEUE_OK)
            ++context->accepted;
        else {
            assert(result == CAMD_NATIVE_QUEUE_FULL);
            ++context->full;
        }
    }
    return NULL;
}

static void test_ump_queue_concurrency(void)
{
    struct CAMDNativeQueueConfigV1 config;
    struct CAMDNativeEventQueue *queue = NULL;
    struct producer_context producers[2];
    struct CAMDNativeQueueStatsV1 stats;
    struct CAMDNativeQueueHeadV1 head;
    struct CAMDUMPEventV1 output[8];
    pthread_t threads[2];
    size_t count, cancelled;

    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.DataFormat = CAMD_PROVIDER_FORMAT_UMP;
    config.Capacity = 8;
    assert(camd_native_queue_create(&config, &queue) == CAMD_NATIVE_QUEUE_OK);

    memset(producers, 0, sizeof(producers));
    producers[0].queue = producers[1].queue = queue;
    producers[0].event = ump_event(4);
    producers[1].event = ump_event(1);
    assert(pthread_create(&threads[0], NULL, produce_ump, &producers[0]) == 0);
    assert(pthread_create(&threads[1], NULL, produce_ump, &producers[1]) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(producers[0].accepted + producers[1].accepted == 8);
    assert(producers[0].full + producers[1].full == 192);

    memset(&stats, 0, sizeof(stats));
    stats.Size = sizeof(stats);
    stats.Version = 1;
    assert(camd_native_queue_stats(queue, &stats) == CAMD_NATIVE_QUEUE_OK);
    assert(stats.PendingRecords == 8 && stats.HighWaterRecords == 8);
    assert(stats.FullRejections == 192);

    memset(&head, 0, sizeof(head));
    head.Size = sizeof(head);
    head.Version = 1;
    while (camd_native_queue_peek(queue, &head) == CAMD_NATIVE_QUEUE_OK) {
        assert(head.Kind == CAMD_NATIVE_QUEUE_ITEM_UMP);
        assert(head.RecordCount == 1);
        assert(camd_native_queue_dequeue_ump(queue, output, 8, &count) ==
               CAMD_NATIVE_QUEUE_OK);
        assert(count == 1);
        assert(output[0].WordCount == 1 || output[0].WordCount == 4);
    }
    assert(camd_native_queue_cancel(queue, &cancelled) ==
           CAMD_NATIVE_QUEUE_OK);
    assert(cancelled == 0);
    camd_native_queue_destroy(queue);
}

int main(void)
{
    test_midi1_queue();
    test_ump_queue_concurrency();
    puts("CAMD native event queue OK");
    return 0;
}
