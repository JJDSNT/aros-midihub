#ifndef MIDIHUB_SENDER_H
#define MIDIHUB_SENDER_H

#include <stddef.h>
#include <stdint.h>

#define MH_SENDER_HISTORY 32
#define MH_SYSEX_JOURNAL_MAX 512
#define MH_SYSEX_JOURNAL_LOGS 4

struct mh_sent_sysex {
    uint16_t sequence;
    uint8_t count;
    size_t length;
    uint8_t data[MH_SYSEX_JOURNAL_MAX];
};

struct mh_sent_event {
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
};

struct mh_sender {
    struct mh_sent_event events[MH_SENDER_HISTORY];
    size_t count;
    uint8_t bank_msb[16];
    uint8_t bank_lsb[16];
    uint8_t bank_known[16];
    uint8_t sustain_on[16];
    uint8_t sustain_toggle[16];
    uint8_t reset_count[16][2];
    uint8_t active_notes[16][128];
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
    struct mh_sent_sysex sysex_logs[MH_SYSEX_JOURNAL_LOGS];
    size_t sysex_log_count;
    uint8_t sysex_count;
};

void mh_sender_reset(struct mh_sender *sender);
void mh_sender_clear_history(struct mh_sender *sender);
int mh_sender_supported(const uint8_t *message, size_t length);
void mh_sender_record(struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, const uint8_t *message,
                      size_t length);
int mh_sender_record_sysex(struct mh_sender *sender, uint16_t sequence,
                           uint32_t timestamp, const uint8_t *message,
                           size_t length);
void mh_sender_ack(struct mh_sender *sender, uint32_t extended_sequence);
int mh_sender_journal(const struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, uint8_t *data, size_t capacity,
                      size_t *length);

#endif
