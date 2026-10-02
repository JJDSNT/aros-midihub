#ifndef MIDIHUB_TIMING_H
#define MIDIHUB_TIMING_H

#include <stddef.h>
#include <stdint.h>

#define MH_EVENT_QUEUE_CAP 256
#define MH_EVENT_QUEUE_SYSEX_BYTES 65536

struct mh_queued_event {
    uint64_t due;
    uint8_t bytes[3];
    uint8_t length;
    uint8_t *sysex;
    size_t sysex_length;
};

struct mh_event_queue {
    struct mh_queued_event events[MH_EVENT_QUEUE_CAP];
    size_t count;
    size_t sysex_bytes;
};

/* The result converts timestamps in the peer clock into local ticks. */
int mh_clock_offset(const uint64_t timestamps[3], int is_initiator,
                    int64_t *peer_to_local);

/* Resolve a 32-bit RTP timestamp near the current peer time. */
int mh_clock_due(uint32_t timestamp, uint64_t local_now,
                 int64_t peer_to_local, uint64_t *local_due);

void mh_queue_reset(struct mh_event_queue *queue);
int mh_queue_push(struct mh_event_queue *queue, uint64_t due,
                  const uint8_t *bytes, size_t length);
/* SysEx data is copied into bounded queue-owned storage. */
int mh_queue_push_sysex(struct mh_event_queue *queue, uint64_t due,
                        const uint8_t *bytes, size_t length);
/* A popped SysEx event owns its payload until mh_queue_event_release(). */
int mh_queue_pop_due(struct mh_event_queue *queue, uint64_t now,
                      struct mh_queued_event *event);
void mh_queue_event_release(struct mh_queued_event *event);
int mh_queue_next_due(const struct mh_event_queue *queue, uint64_t *due);
size_t mh_queue_cancel_note_on(struct mh_event_queue *queue,
                                uint8_t channel, uint8_t note);
int mh_queue_has_note_on(const struct mh_event_queue *queue,
                          uint8_t channel, uint8_t note);

#endif
