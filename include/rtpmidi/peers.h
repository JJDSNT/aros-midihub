#ifndef APPLEMIDI_PEERS_H
#define APPLEMIDI_PEERS_H

#include <stddef.h>
#include <stdint.h>

#include "rtpmidi/session.h"

/* Several AppleMIDI sessions at once, one per peer, so that a Mac and an
 * iPad can both be connected. Each peer is one session (session.h) found
 * by its address: the peer's control port, its data port being the next
 * one. The caller owns the sockets and the clock and decodes and encodes
 * the packets; no OS calls occur here. */

#define APPLEMIDI_PEERS_MAX 8
#define APPLEMIDI_PEER_NAME_MAX 63

struct applemidi_peer {
    int used;
    uint32_t address;           /* IPv4, host order */
    uint16_t control_port;      /* the peer's; data is control_port + 1 */
    struct applemidi_session session;
    char name[APPLEMIDI_PEER_NAME_MAX + 1];
    uint32_t last_seen_ms;
    int connected_reported;
};

enum applemidi_peer_event {
    APPLEMIDI_PEER_NONE,
    APPLEMIDI_PEER_CONNECTED,          /* the session reached the data port */
    APPLEMIDI_PEER_LEFT                /* BY received, refused or expired */
};

struct applemidi_peers {
    struct applemidi_peer peer[APPLEMIDI_PEERS_MAX];
    uint32_t local_ssrc;
    uint8_t name[64];
    size_t name_length;
    int accept;                 /* answer invitations from new peers */
};

int applemidi_peers_init(struct applemidi_peers *peers, uint32_t local_ssrc,
                  const uint8_t *name, size_t name_length);

/* Handles an AppleMIDI control packet from address:port, which is the
 * peer's control port or, with on_data_port, its data port. When a reply
 * is due, out is filled and *out_data_port says which local port sends
 * it; the function returns 1. It returns 0 when nothing is to be sent,
 * -1 for invalid input. *index is the peer concerned (-1 when none) and
 * *event what happened to it. A new peer is accepted when there is room
 * and accept is set; otherwise its invitation is answered with NO. */
int applemidi_peers_receive(struct applemidi_peers *peers, uint32_t address, uint16_t port,
                     int on_data_port, const struct applemidi_packet *in,
                     uint32_t now_ms, struct applemidi_packet *out,
                     int *out_data_port, int *index, enum applemidi_peer_event *event);

/* Invites address:control_port. Returns the peer index, -1 when the table
 * is full or the peer is already known. out is the IN to send on the
 * local control port. */
int applemidi_peers_invite(struct applemidi_peers *peers, uint32_t address, uint16_t control_port,
                    uint32_t token, uint32_t now_ms, struct applemidi_packet *out);

/* Rebuilds the outstanding IN for a peer whose invitation timed out.
 * out_data_port identifies whether it belongs on the data socket. */
int applemidi_peers_retry(const struct applemidi_peers *peers, int index,
                   struct applemidi_packet *out, int *out_data_port);

/* The connected peer that sends RTP from address:data_port, or -1. */
int applemidi_peers_find_data(const struct applemidi_peers *peers, uint32_t address,
                       uint16_t data_port);
/* The peer with this SSRC, or -1. */
int applemidi_peers_find_ssrc(const struct applemidi_peers *peers, uint32_t ssrc);

/* Records that the peer was heard from (CK, RTP). */
void applemidi_peers_seen(struct applemidi_peers *peers, int index, uint32_t now_ms);

/* Ends one peer: out is the BY to send on the local control port.
 * Returns -1 when the index is not in use. */
int applemidi_peers_end(struct applemidi_peers *peers, int index, struct applemidi_packet *out);

/* The first peer silent for timeout_ms, which the caller then ends with
 * applemidi_peers_end(), or -1. */
int applemidi_peers_expired(const struct applemidi_peers *peers, uint32_t now_ms,
                     uint32_t timeout_ms);

#endif
