#ifndef MIDIHUB_SYNTH_H
#define MIDIHUB_SYNTH_H

#include <stddef.h>
#include <stdint.h>

struct mh_synth;

/* Loads an SF2 bank. Returns NULL if the bank cannot be opened. */
struct mh_synth *mh_synth_open(const char *bank_path, int sample_rate);
void mh_synth_close(struct mh_synth *synth);

/* Accepts a complete MIDI channel message: 0 = handled, 1 = unsupported,
   -1 = malformed or failed. SysEx is not supported yet. */
int mh_synth_send(struct mh_synth *synth, const uint8_t *message,
                  size_t length);

/* Renders mono signed 16-bit PCM at the sample rate given to open(). */
void mh_synth_render(struct mh_synth *synth, int16_t *samples,
                     size_t sample_count);

#endif
