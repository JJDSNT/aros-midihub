#include "midihub/timing.h"

#include <limits.h>
#include <string.h>

int mh_clock_offset(const uint64_t timestamps[3], int is_initiator,
                    int64_t *peer_to_local)
{
    uint64_t midpoint;
    uint64_t distance;
    if (!timestamps || !peer_to_local || timestamps[2] < timestamps[0])
        return -1;
    midpoint = timestamps[0] + (timestamps[2] - timestamps[0]) / 2;
    distance = midpoint >= timestamps[1] ? midpoint - timestamps[1] :
                                           timestamps[1] - midpoint;
    if (distance > INT64_MAX)
        return -1;
    if (is_initiator)
        *peer_to_local = midpoint >= timestamps[1] ?
                         (int64_t)distance : -(int64_t)distance;
    else
        *peer_to_local = midpoint >= timestamps[1] ?
                         -(int64_t)distance : (int64_t)distance;
    return 0;
}

int mh_clock_due(uint32_t timestamp, uint64_t local_now,
                 int64_t peer_to_local, uint64_t *local_due)
{
    uint64_t peer_now;
    uint32_t difference;
    int64_t delta;
    if (!local_due)
        return -1;
    if (peer_to_local >= 0) {
        if (local_now < (uint64_t)peer_to_local)
            return -1;
        peer_now = local_now - (uint64_t)peer_to_local;
    } else {
        uint64_t magnitude = (uint64_t)(-(peer_to_local + 1)) + 1;
        if (local_now > UINT64_MAX - magnitude)
            return -1;
        peer_now = local_now + magnitude;
    }
    difference = timestamp - (uint32_t)peer_now;
    delta = difference <= INT32_MAX ? (int64_t)difference :
            -(int64_t)(UINT64_C(0x100000000) - difference);
    if (delta >= 0) {
        if (local_now > UINT64_MAX - (uint64_t)delta)
            return -1;
        *local_due = local_now + (uint64_t)delta;
    } else {
        uint64_t past = (uint64_t)-delta;
        *local_due = local_now >= past ? local_now - past : 0;
    }
    return 0;
}

void mh_queue_reset(struct mh_event_queue *queue)
{
    if (queue) queue->count = 0;
}

int mh_queue_push(struct mh_event_queue *queue, uint64_t due,
                  const uint8_t *bytes, size_t length)
{
    size_t position;
    if (!queue || !bytes || length == 0 || length > 3 ||
        queue->count == MH_EVENT_QUEUE_CAP)
        return -1;
    position = queue->count;
    while (position && queue->events[position - 1].due > due) {
        queue->events[position] = queue->events[position - 1];
        --position;
    }
    queue->events[position].due = due;
    queue->events[position].length = (uint8_t)length;
    memcpy(queue->events[position].bytes, bytes, length);
    ++queue->count;
    return 0;
}

int mh_queue_pop_due(struct mh_event_queue *queue, uint64_t now,
                      struct mh_queued_event *event)
{
    if (!queue || !event || !queue->count || queue->events[0].due > now)
        return 0;
    *event = queue->events[0];
    --queue->count;
    if (queue->count)
        memmove(queue->events, queue->events + 1,
                queue->count * sizeof(queue->events[0]));
    return 1;
}

int mh_queue_next_due(const struct mh_event_queue *queue, uint64_t *due)
{
    if (!queue || !due || !queue->count)
        return 0;
    *due = queue->events[0].due;
    return 1;
}
