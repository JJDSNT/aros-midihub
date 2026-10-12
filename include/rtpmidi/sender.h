#ifndef RTPMIDI_SENDER_H
#define RTPMIDI_SENDER_H

#include <stddef.h>
#include <stdint.h>

#define RTPMIDI_SENDER_HISTORY 32
#define RTPMIDI_SYSEX_JOURNAL_MAX 512
#define RTPMIDI_SYSEX_JOURNAL_LOGS 4
#define RTPMIDI_PARAMETER_STATES 128

struct rtpmidi_parameter_counter {
    uint16_t number;
    uint8_t channel;
    uint8_t nrpn;
    uint8_t count;
    uint8_t used;
};

struct rtpmidi_sent_sysex {
    uint16_t sequence;
    uint8_t count;
    size_t length;
    uint8_t data[RTPMIDI_SYSEX_JOURNAL_MAX];
};

struct rtpmidi_sent_event {
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t bytes[3];
    uint8_t length;
    uint8_t tool;
    uint8_t recovery_value;
    uint8_t bank_msb;
    uint8_t bank_lsb;
    uint8_t bank_known;
    uint8_t system_kind;
    uint16_t parameter;
    uint8_t parameter_valid;
    uint8_t parameter_nrpn;
    uint8_t parameter_initiated;
    uint8_t parameter_count_known;
    uint8_t parameter_count;
};

struct rtpmidi_sender {
    struct rtpmidi_sent_event events[RTPMIDI_SENDER_HISTORY];
    size_t count;
    uint8_t bank_msb[16];
    uint8_t bank_lsb[16];
    uint8_t bank_known[16];
    uint8_t switch_on[16][6];
    uint8_t switch_toggle[16][6];
    uint8_t mode_count[16][5];
    uint8_t active_notes[16][128];
    uint16_t note_count[16][128];
    uint8_t release_velocity[16][128];
    uint8_t parameter_msb[16];
    uint8_t parameter_lsb[16];
    uint8_t parameter_nrpn[16];
    uint8_t parameter_type_known[16];
    uint8_t parameter_pending[16];
    uint8_t parameter_valid[16];
    struct rtpmidi_parameter_counter parameter_counts[RTPMIDI_PARAMETER_STATES];
    uint8_t system_reset_count;
    uint8_t tune_request_count;
    uint8_t active_sense_count;
    uint8_t song_select;
    uint8_t song_select_known;
    uint8_t sequencer_known;
    uint8_t sequencer_running;
    uint8_t sequencer_downbeat;
    uint8_t sequencer_start_at_zero;
    uint32_t sequencer_clock;
    uint8_t mtc_complete[8];
    uint8_t mtc_complete_known;
    uint8_t mtc_complete_quarter_frame;
    uint8_t mtc_qf_nibbles[8];
    uint8_t mtc_qf_seen;
    uint8_t mtc_partial_mask;
    uint8_t mtc_qf_direction;
    uint8_t mtc_qf_point;
    struct rtpmidi_sent_sysex sysex_logs[RTPMIDI_SYSEX_JOURNAL_LOGS];
    size_t sysex_log_count;
    uint8_t sysex_count;
    /* Packets sent without a journal because it did not fit, and messages
     * sent that no journal covers. Not cleared by an acknowledgement. */
    uint32_t journals_omitted;
    uint32_t unjournalled;
};

void rtpmidi_sender_reset(struct rtpmidi_sender *sender);
void rtpmidi_sender_clear_history(struct rtpmidi_sender *sender);
int rtpmidi_sender_supported(const uint8_t *message, size_t length);
void rtpmidi_sender_record(struct rtpmidi_sender *sender, uint16_t sequence,
                      uint32_t timestamp, const uint8_t *message,
                      size_t length);
/* Returns 1 when the message went into the journal's history and 0 when it
 * is too long for chapter X (more than RTPMIDI_SYSEX_JOURNAL_MAX bytes
 * between F0 and F7); such a message has no number in the journal's COUNT,
 * at either end. The history of what was sent before is kept: when there
 * is no room, the oldest SysEx logs give way. */
int rtpmidi_sender_record_sysex(struct rtpmidi_sender *sender, uint16_t sequence,
                           uint32_t timestamp, const uint8_t *message,
                           size_t length);
/* Builds the RTP packet for one message other than SysEx: the command, the
 * journal of what was sent before it, and the message's entry in the
 * history. When the journal does not fit, the packet goes without one and
 * journals_omitted counts it: the command is never held back for its
 * journal. Returns -1 only when the command itself is invalid or does not
 * fit. */
int rtpmidi_sender_packet(struct rtpmidi_sender *sender, uint16_t sequence,
                          uint32_t timestamp, uint32_t ssrc,
                          const uint8_t *message, size_t length,
                          uint8_t *data, size_t capacity,
                          size_t *packet_length);
void rtpmidi_sender_ack(struct rtpmidi_sender *sender, uint32_t extended_sequence);
int rtpmidi_sender_journal(const struct rtpmidi_sender *sender, uint16_t sequence,
                      uint32_t timestamp, uint8_t *data, size_t capacity,
                      size_t *length);

#endif
