#ifndef MIDIHUB_SESSION_H
#define MIDIHUB_SESSION_H

#include "midihub/applemidi.h"

/* One peer per session. The caller owns UDP sockets, peer address validation,
 * random token generation, retries and timeouts. No OS calls occur here. */
enum mh_session_phase {
    MH_SESSION_IDLE,
    MH_SESSION_INVITING_CONTROL,
    MH_SESSION_INVITING_DATA,
    MH_SESSION_WAITING_DATA,
    MH_SESSION_CONNECTED
};

struct mh_session {
    enum mh_session_phase phase;
    uint32_t local_ssrc;
    uint32_t peer_ssrc;
    uint32_t token;
    uint8_t name[64];
    size_t name_length;
};

/* The output packet points to session-owned name storage. The caller may
 * immediately pass it to mh_apple_encode(). */
int mh_session_init(struct mh_session *session, uint32_t local_ssrc,
                    const uint8_t *name, size_t name_length);
int mh_session_invite(struct mh_session *session, uint32_t token,
                      struct mh_apple_packet *out);
int mh_session_retry(const struct mh_session *session,
                     struct mh_apple_packet *out, int *out_data_port);

/* Returns 1 when an outgoing packet is ready, 0 when no reply is needed,
 * -1 for invalid input. out_data_port selects N+1 rather than control N. */
int mh_session_receive(struct mh_session *session, int on_data_port,
                       const struct mh_apple_packet *in,
                       struct mh_apple_packet *out, int *out_data_port);

/* Emits BY on control port and resets the local state. */
int mh_session_end(struct mh_session *session, struct mh_apple_packet *out);

#endif
