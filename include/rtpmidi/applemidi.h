#ifndef APPLEMIDI_H
#define APPLEMIDI_H

#include <stddef.h>
#include <stdint.h>

/* Apple MIDI Network Driver Protocol, version 2. All integers are big-endian
 * on the wire. Names are UTF-8 byte strings, without an embedded NUL. */
enum applemidi_command {
    APPLEMIDI_IN = 0x494e,
    APPLEMIDI_OK = 0x4f4b,
    APPLEMIDI_NO = 0x4e4f,
    APPLEMIDI_BY = 0x4259,
    APPLEMIDI_CK = 0x434b,
    APPLEMIDI_RS = 0x5253
};

struct applemidi_packet {
    enum applemidi_command command;
    uint32_t token;
    uint32_t ssrc;
    const uint8_t *name;
    size_t name_length;
    uint8_t sync_count;
    uint64_t timestamps[3];
    uint32_t feedback_sequence;
};

/* Why a packet was not taken. All are negative, so a caller that only
 * wants to know tests for zero. */
enum applemidi_decode_result {
    APPLEMIDI_DECODED = 0,
    APPLEMIDI_NOT_APPLEMIDI = -1,   /* no FF FF in front: RTP, or noise */
    APPLEMIDI_UNKNOWN_COMMAND = -2, /* one this code has no use for */
    APPLEMIDI_TRUNCATED = -3,
    APPLEMIDI_BAD_VERSION = -4,     /* not protocol version 2 */
    APPLEMIDI_MALFORMED = -5
};

/* Decoding takes the fields it uses and nothing else: bytes after a name's
 * terminator, a name that runs to the end of the packet without one, and
 * whatever stands in a CK packet's three unused bytes are all accepted.
 * This protocol has no specification to hold a peer to.
 * Encoding returns 0 for success, -1 for invalid data or insufficient
 * capacity. */
int applemidi_decode(const uint8_t *data, size_t length,
                    struct applemidi_packet *packet);
int applemidi_encode(const struct applemidi_packet *packet, uint8_t *data,
                    size_t capacity, size_t *length);

#endif
