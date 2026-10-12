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

/* Returns 0 for success, -1 for invalid data or insufficient capacity. */
int applemidi_decode(const uint8_t *data, size_t length,
                    struct applemidi_packet *packet);
int applemidi_encode(const struct applemidi_packet *packet, uint8_t *data,
                    size_t capacity, size_t *length);

#endif
