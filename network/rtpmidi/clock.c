#include "rtpmidi/clock.h"

#include <string.h>

#include "rtpmidi/timing.h"

void applemidi_clock_init(struct applemidi_clock *clock, int initiator,
                          uint64_t now)
{
    memset(clock, 0, sizeof(*clock));
    clock->initiator = initiator;
    clock->heard = now;
}

int applemidi_clock_poll(struct applemidi_clock *clock, uint32_t local_ssrc,
                         uint64_t now, struct applemidi_packet *out)
{
    uint64_t interval;

    if (!clock || !out || !clock->initiator)
        return 0;
    interval = clock->early < APPLEMIDI_CLOCK_FIRST_EXCHANGES ?
               APPLEMIDI_CLOCK_FIRST_INTERVAL : APPLEMIDI_CLOCK_INTERVAL;
    if (clock->started && now - clock->sent < interval)
        return 0;
    memset(out, 0, sizeof(*out));
    out->command = APPLEMIDI_CK;
    out->ssrc = local_ssrc;
    out->timestamps[0] = now;
    clock->started = 1;
    clock->outstanding = 1;
    clock->sent = now;
    return 1;
}

int applemidi_clock_receive(struct applemidi_clock *clock,
                            uint32_t local_ssrc,
                            const struct applemidi_packet *in, uint64_t now,
                            struct applemidi_packet *out)
{
    if (!clock || !in || !out || in->command != APPLEMIDI_CK)
        return 0;
    if (in->sync_count == 0 && !clock->initiator) {
        clock->heard = now;
        *out = *in;
        out->ssrc = local_ssrc;
        out->sync_count = 1;
        out->timestamps[1] = now;
        clock->peer_time = in->timestamps[0];
        clock->own_time = now;
        clock->answered = 1;
        return 1;
    }
    if (in->sync_count == 1 && clock->initiator && clock->outstanding &&
        in->timestamps[0] == clock->sent) {
        *out = *in;
        out->ssrc = local_ssrc;
        out->sync_count = 2;
        out->timestamps[2] = now;
        if (rtpmidi_clock_offset(out->timestamps, 1,
                                 &clock->peer_to_local) == 0) {
            clock->synchronized = 1;
            clock->outstanding = 0;
            if (clock->early < APPLEMIDI_CLOCK_FIRST_EXCHANGES)
                ++clock->early;
            ++clock->completed;
        }
        return 1;
    }
    if (in->sync_count == 2 && !clock->initiator && clock->answered &&
        in->timestamps[0] == clock->peer_time &&
        in->timestamps[1] == clock->own_time &&
        rtpmidi_clock_offset(in->timestamps, 0, &clock->peer_to_local) == 0) {
        clock->synchronized = 1;
        clock->answered = 0;
        ++clock->completed;
    }
    return 0;
}

int applemidi_clock_expired(const struct applemidi_clock *clock,
                            uint64_t now)
{
    return clock && !clock->initiator &&
           now - clock->heard >= APPLEMIDI_CLOCK_TIMEOUT;
}
