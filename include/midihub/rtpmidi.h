#ifndef MIDIHUB_RTPMIDI_H
#define MIDIHUB_RTPMIDI_H

#include <stddef.h>
#include <stdint.h>

/* Apple Network MIDI RTP profile. The MIDI list and any recovery journal are
 * exposed as bounded byte spans; this layer does not interpret them. */
struct mh_rtp_packet {
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
    const uint8_t *midi;
    size_t midi_length;
    const uint8_t *journal;
    size_t journal_length;
    uint8_t z;
    uint8_t p;
};

/* Iterates MIDI 1.0 short commands in a decoded RTP-MIDI command section.
 * SysEx and unsupported system commands return -2; malformed data returns
 * -1. A result of 1 contains a command, and 0 means end of list. */
struct mh_midi_event {
    uint8_t bytes[3];
    uint8_t length;
    uint32_t timestamp;
    const uint8_t *sysex;
    size_t sysex_length;
};

enum { MH_SYSEX_MAX = 4096 };

struct mh_sysex_assembler {
    uint8_t bytes[MH_SYSEX_MAX];
    size_t length;
    uint16_t last_sequence;
    uint8_t active;
};

/* Complete and segmented SysEx messages. A result of 1 provides a complete
 * F0..F7 message in *message; 0 means more segments are needed; -1 means a
 * malformed, lost, or oversized message was discarded. */
void mh_sysex_reset(struct mh_sysex_assembler *assembler);
int mh_sysex_feed(struct mh_sysex_assembler *assembler,
                  const struct mh_midi_event *event, uint16_t sequence,
                  const uint8_t **message, size_t *length);

struct mh_rtp_reader {
    const struct mh_rtp_packet *packet;
    size_t offset;
    uint32_t timestamp;
    uint8_t running_status;
};

void mh_rtp_reader_init(struct mh_rtp_reader *reader,
                        const struct mh_rtp_packet *packet);
int mh_rtp_reader_next(struct mh_rtp_reader *reader,
                       struct mh_midi_event *event);

/* Builds a single, complete MIDI 1.0 command with implicit delta time zero
 * and no recovery journal. Returns 0 for success, -1 for invalid input. */
int mh_rtp_encode_short(uint16_t sequence, uint32_t timestamp, uint32_t ssrc,
                        const uint8_t *midi, size_t midi_length,
                        uint8_t *data, size_t capacity, size_t *length);
int mh_rtp_encode_list(uint16_t sequence, uint32_t timestamp, uint32_t ssrc,
                       const uint8_t *midi, size_t midi_length,
                       uint8_t *data, size_t capacity, size_t *length);
int mh_rtp_decode(const uint8_t *data, size_t length,
                  struct mh_rtp_packet *packet);

#endif
