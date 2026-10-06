#include "midihub/peers.h"

#include <string.h>

int mh_peers_init(struct mh_peers *peers, uint32_t local_ssrc,
                  const uint8_t *name, size_t name_length)
{
    if (!peers || (!name && name_length) || name_length >= sizeof(peers->name))
        return -1;
    memset(peers, 0, sizeof(*peers));
    peers->local_ssrc = local_ssrc;
    if (name_length)
        memcpy(peers->name, name, name_length);
    peers->name_length = name_length;
    peers->accept = 1;
    return 0;
}

static int find(const struct mh_peers *peers, uint32_t address, uint16_t control_port)
{
    int i;
    for (i = 0; i < MH_PEERS_MAX; ++i)
        if (peers->peer[i].used && peers->peer[i].address == address &&
            peers->peer[i].control_port == control_port)
            return i;
    return -1;
}

static int allocate(struct mh_peers *peers, uint32_t address, uint16_t control_port,
                    uint32_t now_ms)
{
    int i;
    for (i = 0; i < MH_PEERS_MAX; ++i) {
        struct mh_peer *peer = &peers->peer[i];
        if (peer->used)
            continue;
        memset(peer, 0, sizeof(*peer));
        if (mh_session_init(&peer->session, peers->local_ssrc,
                            peers->name, peers->name_length))
            return -1;
        peer->used = 1;
        peer->address = address;
        peer->control_port = control_port;
        peer->last_seen_ms = now_ms;
        return i;
    }
    return -1;
}

static void release(struct mh_peers *peers, int index)
{
    memset(&peers->peer[index], 0, sizeof(peers->peer[index]));
}

static void remember_name(struct mh_peer *peer, const struct mh_apple_packet *in)
{
    size_t length = in->name_length;
    if (!in->name || !length)
        return;
    if (length > MH_PEER_NAME_MAX)
        length = MH_PEER_NAME_MAX;
    memcpy(peer->name, in->name, length);
    peer->name[length] = 0;
}

int mh_peers_receive(struct mh_peers *peers, uint32_t address, uint16_t port,
                     int on_data_port, const struct mh_apple_packet *in,
                     uint32_t now_ms, struct mh_apple_packet *out,
                     int *out_data_port, int *index, enum mh_peer_event *event)
{
    uint16_t control_port;
    int i, result;
    struct mh_peer *peer;

    if (!peers || !in || !out || !out_data_port || !index || !event ||
        (on_data_port != 0 && on_data_port != 1) || (on_data_port && port == 0))
        return -1;
    *index = -1;
    *event = MH_PEER_NONE;
    *out_data_port = 0;
    control_port = (uint16_t)(on_data_port ? port - 1 : port);

    i = find(peers, address, control_port);
    if (i < 0) {
        /* Only an invitation on the control port starts a peer. */
        if (in->command != MH_APPLE_IN || on_data_port)
            return 0;
        if (!peers->accept || (i = allocate(peers, address, control_port, now_ms)) < 0) {
            memset(out, 0, sizeof(*out));
            out->command = MH_APPLE_NO;
            out->token = in->token;
            out->ssrc = peers->local_ssrc;
            return 1;
        }
    }
    peer = &peers->peer[i];
    *index = i;
    peer->last_seen_ms = now_ms;
    if (in->command == MH_APPLE_IN || in->command == MH_APPLE_OK)
        remember_name(peer, in);

    result = mh_session_receive(&peer->session, on_data_port, in, out, out_data_port);
    if (result < 0)
        return -1;

    if (peer->session.phase == MH_SESSION_CONNECTED && !peer->connected_reported) {
        peer->connected_reported = 1;
        *event = MH_PEER_CONNECTED;
    } else if (peer->session.phase == MH_SESSION_IDLE &&
               (in->command == MH_APPLE_BY || in->command == MH_APPLE_NO)) {
        *event = peer->connected_reported || in->command == MH_APPLE_NO ?
                 MH_PEER_LEFT : MH_PEER_NONE;
        release(peers, i);
        if (*event == MH_PEER_NONE)
            *event = MH_PEER_LEFT;
    }
    return result;
}

int mh_peers_invite(struct mh_peers *peers, uint32_t address, uint16_t control_port,
                    uint32_t token, uint32_t now_ms, struct mh_apple_packet *out)
{
    int i;

    if (!peers || !out || find(peers, address, control_port) >= 0)
        return -1;
    i = allocate(peers, address, control_port, now_ms);
    if (i < 0)
        return -1;
    if (mh_session_invite(&peers->peer[i].session, token, out)) {
        release(peers, i);
        return -1;
    }
    return i;
}

int mh_peers_retry(const struct mh_peers *peers, int index,
                   struct mh_apple_packet *out, int *out_data_port)
{
    if (!peers || !out || !out_data_port || index < 0 ||
        index >= MH_PEERS_MAX || !peers->peer[index].used)
        return -1;
    return mh_session_retry(&peers->peer[index].session, out, out_data_port);
}

int mh_peers_find_data(const struct mh_peers *peers, uint32_t address,
                       uint16_t data_port)
{
    int i;
    if (!peers || data_port == 0)
        return -1;
    i = find(peers, address, (uint16_t)(data_port - 1));
    if (i >= 0 && peers->peer[i].session.phase != MH_SESSION_CONNECTED)
        return -1;
    return i;
}

int mh_peers_find_ssrc(const struct mh_peers *peers, uint32_t ssrc)
{
    int i;
    for (i = 0; peers && i < MH_PEERS_MAX; ++i)
        if (peers->peer[i].used && peers->peer[i].session.phase != MH_SESSION_IDLE &&
            peers->peer[i].session.peer_ssrc == ssrc)
            return i;
    return -1;
}

void mh_peers_seen(struct mh_peers *peers, int index, uint32_t now_ms)
{
    if (peers && index >= 0 && index < MH_PEERS_MAX && peers->peer[index].used)
        peers->peer[index].last_seen_ms = now_ms;
}

int mh_peers_end(struct mh_peers *peers, int index, struct mh_apple_packet *out)
{
    int result = 0;

    if (!peers || !out || index < 0 || index >= MH_PEERS_MAX || !peers->peer[index].used)
        return -1;
    if (peers->peer[index].session.phase == MH_SESSION_IDLE ||
        mh_session_end(&peers->peer[index].session, out))
        result = -1;        /* nothing was established: no BY to send */
    release(peers, index);
    return result;
}

int mh_peers_expired(const struct mh_peers *peers, uint32_t now_ms,
                     uint32_t timeout_ms)
{
    int i;
    for (i = 0; peers && i < MH_PEERS_MAX; ++i)
        if (peers->peer[i].used && now_ms - peers->peer[i].last_seen_ms >= timeout_ms)
            return i;
    return -1;
}
