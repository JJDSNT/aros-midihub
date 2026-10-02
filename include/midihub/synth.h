#ifndef MIDIHUB_SYNTH_H
#define MIDIHUB_SYNTH_H

#include <stddef.h>
#include <stdint.h>

struct mh_synth;

enum mh_synth_backend {
    MH_SYNTH_TINY,
    MH_SYNTH_FLUID
};

/* Loads an SF2 bank with the default TinySoundFont backend. */
struct mh_synth *mh_synth_open(const char *bank_path, int sample_rate);
/* FluidSynth is available only when compiled with MIDIHUB_ENABLE_FLUIDSYNTH. */
struct mh_synth *mh_synth_open_backend(const char *bank_path, int sample_rate,
                                       enum mh_synth_backend backend);
int mh_synth_has_backend(enum mh_synth_backend backend);
const char *mh_synth_backend_name(enum mh_synth_backend backend);
void mh_synth_close(struct mh_synth *synth);

/* Accepts a complete MIDI channel message: 0 = handled, 1 = unsupported,
   -1 = malformed or failed. */
int mh_synth_send(struct mh_synth *synth, const uint8_t *message,
                  size_t length);

/* Accepts a complete F0...F7 SysEx message. The result codes match
   mh_synth_send(). TinySoundFont does not support SysEx. */
int mh_synth_send_sysex(struct mh_synth *synth, const uint8_t *message,
                        size_t length);

/* Renders mono signed 16-bit PCM at the sample rate given to open(). */
void mh_synth_render(struct mh_synth *synth, int16_t *samples,
                     size_t sample_count);

#endif
