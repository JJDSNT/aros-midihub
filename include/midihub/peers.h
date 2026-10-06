#ifndef MIDIHUB_PEERS_H
#define MIDIHUB_PEERS_H

#include <stddef.h>
#include <stdint.h>

#include "midihub/session.h"

/* Several AppleMIDI sessions at once, one per peer, so that a Mac and an
 * iPad can both be connected. Each peer is one session (session.h) found
 * by its address: the peer's control port, its data port being the next
 * one. The caller owns the sockets and the clock and decodes and encodes
 * the packets; no OS calls occur here. */

#define MH_PEERS_MAX 8
#define MH_PEER_NAME_MAX 63

struct mh_peer {
    int used;
    uint32_t address;           /* IPv4, host order */
    uint16_t control_port;      /* the peer's; data is control_port + 1 */
    struct mh_session session;
    char name[MH_PEER_NAME_MAX + 1];
    uint32_t last_seen_ms;
    int connected_reported;
};

enum mh_peer_event {
    MH_PEER_NONE,
    MH_PEER_CONNECTED,          /* the session reached the data port */
    MH_PEER_LEFT                /* BY received, refused or expired */
};

struct mh_peers {
    struct mh_peer peer[MH_PEERS_MAX];
    uint32_t local_ssrc;
    uint8_t name[64];
    size_t name_length;
    int accept;                 /* answer invitations from new peers */
};

int mh_peers_init(struct mh_peers *peers, uint32_t local_ssrc,
                  const uint8_t *name, size_t name_length);

/* Handles an AppleMIDI control packet from address:port, which is the
 * peer's control port or, with on_data_port, its data port. When a reply
 * is due, out is filled and *out_data_port says which local port sends
 * it; the function returns 1. It returns 0 when nothing is to be sent,
 * -1 for invalid input. *index is the peer concerned (-1 when none) and
 * *event what happened to it. A new peer is accepted when there is room
 * and accept is set; otherwise its invitation is answered with NO. */
int mh_peers_receive(struct mh_peers *peers, uint32_t address, uint16_t port,
                     int on_data_port, const struct mh_apple_packet *in,
                     uint32_t now_ms, struct mh_apple_packet *out,
                     int *out_data_port, int *index, enum mh_peer_event *event);

/* Invites address:control_port. Returns the peer index, -1 when the table
 * is full or the peer is already known. out is the IN to send on the
 * local control port. */
int mh_peers_invite(struct mh_peers *peers, uint32_t address, uint16_t control_port,
                    uint32_t token, uint32_t now_ms, struct mh_apple_packet *out);

/* Rebuilds the outstanding IN for a peer whose invitation timed out.
 * out_data_port identifies whether it belongs on the data socket. */
int mh_peers_retry(const struct mh_peers *peers, int index,
                   struct mh_apple_packet *out, int *out_data_port);

/* The connected peer that sends RTP from address:data_port, or -1. */
int mh_peers_find_data(const struct mh_peers *peers, uint32_t address,
                       uint16_t data_port);
/* The peer with this SSRC, or -1. */
int mh_peers_find_ssrc(const struct mh_peers *peers, uint32_t ssrc);

/* Records that the peer was heard from (CK, RTP). */
void mh_peers_seen(struct mh_peers *peers, int index, uint32_t now_ms);

/* Ends one peer: out is the BY to send on the local control port.
 * Returns -1 when the index is not in use. */
int mh_peers_end(struct mh_peers *peers, int index, struct mh_apple_packet *out);

/* The first peer silent for timeout_ms, which the caller then ends with
 * mh_peers_end(), or -1. */
int mh_peers_expired(const struct mh_peers *peers, uint32_t now_ms,
                     uint32_t timeout_ms);

#endif
