#ifndef UMP_H
#define UMP_H

#include <stddef.h>
#include <stdint.h>

/* MIDI 2.0 Universal MIDI Packet (UMP Format and MIDI 2.0 Protocol, v1.1).
 * A UMP is one to four 32-bit words in host order; the first word's top
 * nibble is the message type. CAMD carries MIDI 1.0 byte messages, so
 * these helpers translate between the two. No OS calls occur here. */

enum ump_type {
    UMP_UTILITY = 0x0,
    UMP_SYSTEM = 0x1,          /* System Common and Real Time */
    UMP_MIDI1_VOICE = 0x2,     /* MIDI 1.0 Channel Voice */
    UMP_DATA64 = 0x3,          /* SysEx7 */
    UMP_MIDI2_VOICE = 0x4,     /* MIDI 2.0 Channel Voice */
    UMP_DATA128 = 0x5,         /* SysEx8, Mixed Data Set */
    UMP_FLEX = 0xd,
    UMP_STREAM = 0xf
};

/* Words in a UMP of the message type in the first word's top nibble. */
unsigned ump_words(uint32_t first_word);

/* Min-center-max scaling between resolutions, as the UMP specification
 * defines it, e.g. 7-bit velocity to 16-bit and back. */
uint32_t ump_scale_up(uint32_t value, unsigned from_bits, unsigned to_bits);
uint32_t ump_scale_down(uint32_t value, unsigned from_bits, unsigned to_bits);

/* MIDI 1.0 byte stream to UMP. Feed bytes as they arrive (running status
 * and real time bytes inside SysEx are handled); each complete message
 * is emitted as MT 1, MT 2 or a series of MT 3 SysEx7 packets. */
struct ump_from_midi1 {
    uint8_t group;
    uint8_t status;               /* running status, 0 for none */
    uint8_t data[2];
    uint8_t have;
    uint8_t need;
    uint8_t in_sysex;
    uint8_t sysex[6];
    uint8_t sysex_count;
    uint8_t sysex_started;        /* a Start packet has been emitted */
};

typedef void (*ump_sink)(void *context, const uint32_t *words, unsigned count);

void ump_from_midi1_init(struct ump_from_midi1 *state, uint8_t group);
void ump_from_midi1_byte(struct ump_from_midi1 *state, uint8_t byte,
                            ump_sink sink, void *context);

/* UMP to MIDI 1.0 bytes. MT 1 and MT 2 map directly, SysEx7 is
 * reassembled, MIDI 2.0 Channel Voice is translated down (bank and
 * RPN/NRPN become their Control Change sequences); messages with no
 * MIDI 1.0 equivalent are dropped. Only UMPs of one group are taken. */
struct ump_to_midi1 {
    uint8_t group;
    uint8_t in_sysex;
};

typedef void (*ump_midi1_sink)(void *context, const uint8_t *bytes, size_t length);

void ump_to_midi1_init(struct ump_to_midi1 *state, uint8_t group);
/* Returns -1 when words holds fewer words than the UMP needs. */
int ump_to_midi1(struct ump_to_midi1 *state, const uint32_t *words,
                    unsigned count, ump_midi1_sink sink, void *context);

/* UMP Stream messages (MT 0xF) an endpoint answers discovery with. */
enum ump_stream_status {
    UMP_ENDPOINT_DISCOVERY = 0x000,
    UMP_ENDPOINT_INFO = 0x001,
    UMP_DEVICE_IDENTITY = 0x002,
    UMP_ENDPOINT_NAME = 0x003,
    UMP_PRODUCT_INSTANCE_ID = 0x004,
    UMP_STREAM_CONFIG_REQUEST = 0x005,
    UMP_STREAM_CONFIG_NOTIFY = 0x006
};

enum {
    UMP_FILTER_ENDPOINT_INFO = 0x01,
    UMP_FILTER_DEVICE_IDENTITY = 0x02,
    UMP_FILTER_ENDPOINT_NAME = 0x04,
    UMP_FILTER_PRODUCT_INSTANCE_ID = 0x08,
    UMP_FILTER_STREAM_CONFIG = 0x10
};

enum { UMP_PROTOCOL_MIDI1 = 0x01, UMP_PROTOCOL_MIDI2 = 0x02 };

unsigned ump_stream_status(const uint32_t *words);
/* Endpoint Info Notification: UMP 1.1, no function blocks, the protocols
 * given (UMP_PROTOCOL_* bits). Writes 4 words. */
void ump_endpoint_info(uint32_t *words, unsigned protocols);
/* Stream Configuration Notification for protocol. Writes 4 words. */
void ump_stream_config(uint32_t *words, unsigned protocol);
/* Endpoint Name or Product Instance Id Notification: text split over as
 * many 4-word UMPs as needed (14 bytes each). Returns the words written,
 * 0 when capacity is too small. */
unsigned ump_stream_text(uint32_t *words, size_t capacity_words,
                            unsigned status, const char *text, size_t length);

#endif
