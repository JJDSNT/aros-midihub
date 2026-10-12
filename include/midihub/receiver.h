#ifndef MIDIHUB_RECEIVER_H
#define MIDIHUB_RECEIVER_H

#include <stddef.h>
#include <stdint.h>

#include "midihub/rtpmidi.h"
#include "midihub/sender.h"
#include "midihub/timing.h"

/* What one RTP-MIDI stream's receiver knows of the sender's state, and the
 * repair of that state from a recovery journal after packets were lost. It
 * is the counterpart of struct mh_sender. The caller owns the sockets, the
 * clock and whatever plays the MIDI; no OS calls occur here. */

struct mh_receiver_output {
    /* One complete MIDI 1.0 message other than SysEx. */
    void (*message)(void *context, const uint8_t *bytes, size_t length);
    /* One complete SysEx message, F0 to F7. */
    void (*sysex)(void *context, const uint8_t *message, size_t length);
    /* A line of text, ending in a newline, about what was recovered. May be
     * NULL. */
    void (*report)(void *context, const char *text);
};

/* The caller's clock at the moment of recovery, for commands the journal
 * asks to be played late. */
struct mh_receiver_clock {
    int synchronized;       /* peer_to_local is known */
    int64_t peer_to_local;
    uint64_t now;
};

struct mh_receiver {
    const struct mh_receiver_output *output;
    void *context;
    struct mh_event_queue *queue;
    uint8_t active_notes[16][128];
    uint8_t note_count[16][128];
    uint8_t program[16];
    uint8_t program_known[16];
    uint8_t bank_msb[16];
    uint8_t bank_lsb[16];
    uint8_t bank_msb_known[16];
    uint8_t bank_lsb_known[16];
    uint16_t pitch[16];
    uint8_t controllers[16][128];
    uint8_t controller_known[16][128];
    uint8_t switch_toggles[16][6];
    uint8_t controller_count[16][128];
    uint8_t parameter_msb[16];
    uint8_t parameter_lsb[16];
    uint8_t parameter_nrpn[16];
    uint8_t parameter_type_known[16];
    uint8_t parameter_pending[16];
    uint8_t parameter_valid[16];
    struct mh_parameter_counter parameter_counts[MH_PARAMETER_STATES];
    uint8_t channel_pressure[16];
    uint8_t channel_pressure_known[16];
    uint8_t poly_pressure[16][128];
    uint8_t poly_pressure_known[16][128];
    uint8_t reset_count;
    uint8_t tune_request_count;
    uint8_t active_sense_count;
    uint8_t song_select;
    uint8_t song_select_known;
    uint8_t sequencer_known;
    uint8_t sequencer_running;
    uint8_t sequencer_downbeat;
    uint8_t sequencer_start_at_zero;
    uint32_t sequencer_clock;
    uint8_t mtc_full_frame[4];
    uint8_t mtc_full_known;
    uint8_t sysex_count;
};

/* The queue is the one the caller holds timed events in; recovery looks in
 * it for a Note On still waiting and may add one. */
void mh_receiver_init(struct mh_receiver *receiver,
                      const struct mh_receiver_output *output, void *context,
                      struct mh_event_queue *queue);

/* Every message of the stream passes here on its way out: the receiver
 * notes what it changes and hands it to the output. */
void mh_receiver_deliver(struct mh_receiver *receiver, const uint8_t *bytes,
                         size_t length);
void mh_receiver_deliver_sysex(struct mh_receiver *receiver,
                               const uint8_t *message, size_t length);

/* Repairs the state after a gap that the journal's checkpoint covers,
 * through the output. system is NULL when the journal has no system
 * chapters. single_loss says exactly one packet is missing, which lets
 * chapters marked as safe for that case be skipped. timestamp is that of
 * the packet carrying the journal. clock may be NULL: then nothing is
 * played late. */
void mh_receiver_recover(struct mh_receiver *receiver,
                         const struct mh_journal *journal,
                         const struct mh_journal_system_state *system,
                         int single_loss, uint32_t timestamp,
                         const struct mh_receiver_clock *clock);

/* The end of a session: Note Off for every note still sounding, then the
 * two halves of the state. */
void mh_receiver_release_notes(struct mh_receiver *receiver);
void mh_receiver_reset_channels(struct mh_receiver *receiver);
void mh_receiver_reset_system(struct mh_receiver *receiver);
/* Forgets which notes sound without sending anything. */
void mh_receiver_forget_notes(struct mh_receiver *receiver);

#endif
