#include "midihub/session.h"

#include <string.h>

static void make_packet(const struct mh_session *session,
                        enum mh_apple_command command,
                        struct mh_apple_packet *out)
{
    memset(out, 0, sizeof(*out));
    out->command = command;
    out->token = session->token;
    out->ssrc = session->local_ssrc;
    if (command != MH_APPLE_NO && command != MH_APPLE_BY) {
        out->name = session->name;
        out->name_length = session->name_length;
    }
}

int mh_session_init(struct mh_session *session, uint32_t local_ssrc,
                    const uint8_t *name, size_t name_length)
{
    if (!session || (!name && name_length) || name_length >= sizeof(session->name))
        return -1;
    memset(session, 0, sizeof(*session));
    session->local_ssrc = local_ssrc;
    if (name_length)
        memcpy(session->name, name, name_length);
    session->name_length = name_length;
    return 0;
}

int mh_session_invite(struct mh_session *session, uint32_t token,
                      struct mh_apple_packet *out)
{
    if (!session || !out || session->phase != MH_SESSION_IDLE)
        return -1;
    session->token = token;
    session->phase = MH_SESSION_INVITING_CONTROL;
    make_packet(session, MH_APPLE_IN, out);
    return 0;
}

int mh_session_retry(const struct mh_session *session,
                     struct mh_apple_packet *out, int *out_data_port)
{
    if (!session || !out || !out_data_port ||
        (session->phase != MH_SESSION_INVITING_CONTROL &&
         session->phase != MH_SESSION_INVITING_DATA))
        return -1;
    make_packet(session, MH_APPLE_IN, out);
    *out_data_port = session->phase == MH_SESSION_INVITING_DATA;
    return 0;
}

int mh_session_receive(struct mh_session *session, int on_data_port,
                       const struct mh_apple_packet *in,
                       struct mh_apple_packet *out, int *out_data_port)
{
    if (!session || !in || !out || !out_data_port ||
        (on_data_port != 0 && on_data_port != 1))
        return -1;
    *out_data_port = 0;

    if (in->command == MH_APPLE_BY) {
        if (session->phase != MH_SESSION_IDLE &&
            in->ssrc == session->peer_ssrc && in->token == session->token)
            session->phase = MH_SESSION_IDLE;
        return 0;
    }

    if (in->command == MH_APPLE_IN) {
        if (!on_data_port && session->phase == MH_SESSION_IDLE) {
            session->token = in->token;
            session->peer_ssrc = in->ssrc;
            session->phase = MH_SESSION_WAITING_DATA;
        } else if (in->token != session->token ||
                   in->ssrc != session->peer_ssrc ||
                   !((!on_data_port && session->phase == MH_SESSION_WAITING_DATA) ||
                     (on_data_port && (session->phase == MH_SESSION_WAITING_DATA ||
                                       session->phase == MH_SESSION_CONNECTED)))) {
            return 0;
        }
        if (on_data_port)
            session->phase = MH_SESSION_CONNECTED;
        make_packet(session, MH_APPLE_OK, out);
        *out_data_port = on_data_port;
        return 1;
    }

    if (in->command == MH_APPLE_NO &&
        (session->phase == MH_SESSION_INVITING_CONTROL ||
         session->phase == MH_SESSION_INVITING_DATA) &&
        in->token == session->token) {
        session->phase = MH_SESSION_IDLE;
        return 0;
    }

    if (in->command != MH_APPLE_OK || in->token != session->token)
        return 0;
    if (!on_data_port && session->phase == MH_SESSION_INVITING_CONTROL) {
        session->peer_ssrc = in->ssrc;
        session->phase = MH_SESSION_INVITING_DATA;
        make_packet(session, MH_APPLE_IN, out);
        *out_data_port = 1;
        return 1;
    }
    if (on_data_port && session->phase == MH_SESSION_INVITING_DATA &&
        in->ssrc == session->peer_ssrc)
        session->phase = MH_SESSION_CONNECTED;
    return 0;
}

int mh_session_end(struct mh_session *session, struct mh_apple_packet *out)
{
    if (!session || !out || session->phase == MH_SESSION_IDLE)
        return -1;
    make_packet(session, MH_APPLE_BY, out);
    session->phase = MH_SESSION_IDLE;
    return 0;
}
