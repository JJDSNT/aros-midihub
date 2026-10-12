#ifndef APPLEMIDI_CLOCK_H
#define APPLEMIDI_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "rtpmidi/applemidi.h"

/* The clock exchange of one AppleMIDI session. The side that sent the
 * invitation starts each exchange with a CK that has its own time; the
 * other side answers with its time; the first answers again with its time
 * at that moment. Each side then knows the other's clock against its own.
 * Times are in the session's unit, a ten-thousandth of a second. The
 * caller owns the socket and the clock; no OS calls occur here. */

/* An exchange a second for the first three, then one in fifty seconds; a
 * session whose inviter has sent no CK for two minutes is taken for dead.
 * These are this implementation's numbers, not observed ones: see AM11 in
 * docs/applemidi-backlog.md. */
#define APPLEMIDI_CLOCK_FIRST_EXCHANGES 3
#define APPLEMIDI_CLOCK_FIRST_INTERVAL 10000u
#define APPLEMIDI_CLOCK_INTERVAL 500000u
#define APPLEMIDI_CLOCK_TIMEOUT 1200000u

struct applemidi_clock {
    int initiator;              /* this side sent the invitation */
    int synchronized;           /* peer_to_local is known */
    int64_t peer_to_local;      /* add to a time of the peer's */
    unsigned int completed;     /* exchanges brought to their end */
    /* initiator */
    int started;                /* a first CK went out */
    int outstanding;            /* its answer has not come */
    uint64_t sent;              /* the time in the CK awaited */
    unsigned int early;         /* of the first exchanges, how many done */
    /* the other side */
    int answered;               /* a CK was answered and awaits its third */
    uint64_t peer_time;         /* the time it carried */
    uint64_t own_time;          /* the time we answered with */
    uint64_t heard;             /* when the peer last began an exchange */
};

/* At the moment the session is connected. */
void applemidi_clock_init(struct applemidi_clock *clock, int initiator,
                          uint64_t now);

/* Call regularly. Returns 1 when out is a CK to send on the data port: the
 * first at once, then by the intervals above. The other side never sends
 * of its own accord. */
int applemidi_clock_poll(struct applemidi_clock *clock, uint32_t local_ssrc,
                         uint64_t now, struct applemidi_packet *out);

/* A CK from the session's peer; the caller has checked its SSRC. Returns 1
 * when out is the answer to send on the data port. A packet that belongs
 * to no exchange in progress is ignored. */
int applemidi_clock_receive(struct applemidi_clock *clock,
                            uint32_t local_ssrc,
                            const struct applemidi_packet *in, uint64_t now,
                            struct applemidi_packet *out);

/* Whether the peer that should be starting exchanges has gone silent. */
int applemidi_clock_expired(const struct applemidi_clock *clock,
                            uint64_t now);

#endif
