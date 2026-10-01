#ifndef MIDIHUB_SENDER_H
#define MIDIHUB_SENDER_H

#include <stddef.h>
#include <stdint.h>

#define MH_SENDER_HISTORY 32

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
};

void mh_sender_reset(struct mh_sender *sender);
void mh_sender_clear_history(struct mh_sender *sender);
int mh_sender_supported(const uint8_t *message, size_t length);
void mh_sender_record(struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, const uint8_t *message,
                      size_t length);
void mh_sender_ack(struct mh_sender *sender, uint32_t extended_sequence);
int mh_sender_journal(const struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, uint8_t *data, size_t capacity,
                      size_t *length);

#endif
