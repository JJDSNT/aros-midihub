#ifndef MIDIHUB_APPLEMIDI_H
#define MIDIHUB_APPLEMIDI_H

#include <stddef.h>
#include <stdint.h>

/* Apple MIDI Network Driver Protocol, version 2. All integers are big-endian
 * on the wire. Names are UTF-8 byte strings, without an embedded NUL. */
enum mh_apple_command {
    MH_APPLE_IN = 0x494e,
    MH_APPLE_OK = 0x4f4b,
    MH_APPLE_NO = 0x4e4f,
    MH_APPLE_BY = 0x4259,
    MH_APPLE_CK = 0x434b
};

struct mh_apple_packet {
    enum mh_apple_command command;
    uint32_t token;
    uint32_t ssrc;
    const uint8_t *name;
    size_t name_length;
    uint8_t sync_count;
    uint64_t timestamps[3];
};

/* Returns 0 for success, -1 for invalid data or insufficient capacity. */
int mh_apple_decode(const uint8_t *data, size_t length,
                    struct mh_apple_packet *packet);
int mh_apple_encode(const struct mh_apple_packet *packet, uint8_t *data,
                    size_t capacity, size_t *length);

#endif
