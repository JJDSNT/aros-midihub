#ifndef MIDIHUB_RTPMIDI_H
#define MIDIHUB_RTPMIDI_H

#include <stddef.h>
#include <stdint.h>

/* Apple Network MIDI RTP profile. The MIDI list and any recovery journal are
 * exposed as bounded byte spans for separate decoding. */
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

struct mh_journal_channel {
    const uint8_t *data;
    size_t length;
    uint8_t number;
    uint8_t chapters;
    uint8_t single_packet_safe;
};

struct mh_journal {
    uint16_t checkpoint;
    uint8_t single_packet_safe;
    uint8_t enhanced_controllers;
    const uint8_t *system;
    size_t system_length;
    struct mh_journal_channel channels[16];
    size_t channel_count;
};

/* Decode journal framing. Chapter contents remain opaque. */
int mh_journal_decode(const uint8_t *data, size_t length,
                      struct mh_journal *journal);
int mh_journal_covers_gap(uint16_t previous, uint16_t current,
                          uint16_t checkpoint);
struct mh_journal_note_log {
    uint8_t number;
    uint8_t velocity;
    uint8_t simultaneous;
    uint8_t single_packet_safe;
};

struct mh_journal_notes {
    uint8_t offbits[16];
    int offbits_single_packet_safe;
    struct mh_journal_note_log logs[128];
    size_t log_count;
};

/* Decode Chapter N. 0: absent, 1: decoded, -1: malformed. */
int mh_journal_decode_notes(const struct mh_journal_channel *channel,
                            struct mh_journal_notes *notes);

struct mh_journal_channel_state {
    int has_program;
    int program_single_packet_safe;
    uint8_t program;
    int has_bank;
    uint8_t bank_msb;
    uint8_t bank_lsb;
    int has_pitch;
    int pitch_single_packet_safe;
    uint8_t pitch_lsb;
    uint8_t pitch_msb;
};

/* Decode Chapters P and W. 0: neither present, 1: decoded, -1: malformed. */
int mh_journal_decode_channel_state(
    const struct mh_journal_channel *channel,
    struct mh_journal_channel_state *state);

struct mh_journal_control_log {
    uint8_t number;
    uint8_t value;
    uint8_t alternate;
    uint8_t count_tool;
    uint8_t single_packet_safe;
};

struct mh_journal_controls {
    struct mh_journal_control_log logs[128];
    size_t count;
};

/* Decode Chapter C logs; alternate tools are marked for separate handling. */
int mh_journal_decode_controls(const struct mh_journal_channel *channel,
                                struct mh_journal_controls *controls);

struct mh_journal_poly_pressure {
    uint8_t number;
    uint8_t pressure;
    uint8_t reset_notes;
    uint8_t single_packet_safe;
};

struct mh_journal_aftertouch {
    int has_channel_pressure;
    int channel_single_packet_safe;
    uint8_t channel_pressure;
    struct mh_journal_poly_pressure poly[128];
    size_t poly_count;
};

/* Decode Chapters T and A. 0: absent, 1: decoded, -1: malformed. */
int mh_journal_decode_aftertouch(const struct mh_journal_channel *channel,
                                  struct mh_journal_aftertouch *aftertouch);

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
