#ifndef APPLEMIDI_SESSION_H
#define APPLEMIDI_SESSION_H

#include "rtpmidi/applemidi.h"

/* One peer per session. The caller owns UDP sockets, peer address validation,
 * random token generation, retries and timeouts. No OS calls occur here. */
enum applemidi_session_phase {
    APPLEMIDI_SESSION_IDLE,
    APPLEMIDI_SESSION_INVITING_CONTROL,
    APPLEMIDI_SESSION_INVITING_DATA,
    APPLEMIDI_SESSION_WAITING_DATA,
    APPLEMIDI_SESSION_CONNECTED
};

struct applemidi_session {
    enum applemidi_session_phase phase;
    uint32_t local_ssrc;
    uint32_t peer_ssrc;
    uint32_t token;
    uint8_t name[64];
    size_t name_length;
};

/* The output packet points to session-owned name storage. The caller may
 * immediately pass it to applemidi_encode(). */
int applemidi_session_init(struct applemidi_session *session, uint32_t local_ssrc,
                    const uint8_t *name, size_t name_length);
int applemidi_session_invite(struct applemidi_session *session, uint32_t token,
                      struct applemidi_packet *out);
int applemidi_session_retry(const struct applemidi_session *session,
                     struct applemidi_packet *out, int *out_data_port);

/* Returns 1 when an outgoing packet is ready, 0 when no reply is needed,
 * -1 for invalid input. out_data_port selects N+1 rather than control N. */
int applemidi_session_receive(struct applemidi_session *session, int on_data_port,
                       const struct applemidi_packet *in,
                       struct applemidi_packet *out, int *out_data_port);

/* Emits BY on control port and resets the local state. */
int applemidi_session_end(struct applemidi_session *session, struct applemidi_packet *out);

#endif
