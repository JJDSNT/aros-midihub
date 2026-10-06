#ifndef MIDIHUB_AROS_CAMD_H
#define MIDIHUB_AROS_CAMD_H

#include "camd_bridge.h"

/* CAMD-facing state owned by the Network MIDI runtime.  Keeping this wrapper
 * separate from the reusable bridge gives the resident process one boundary
 * for CAMD lifecycle and leaves room for one bridge per peer later. */
struct mh_camd_runtime {
    struct mh_camd_bridge bridge;
    int opened;
};

int mh_camd_runtime_open(struct mh_camd_runtime *runtime);
void mh_camd_runtime_close(struct mh_camd_runtime *runtime);
int mh_camd_runtime_is_open(const struct mh_camd_runtime *runtime);
int mh_camd_runtime_signal_bit(const struct mh_camd_runtime *runtime);
void mh_camd_runtime_deliver(struct mh_camd_runtime *runtime,
                             const uint8_t *message, size_t length);
void mh_camd_runtime_deliver_sysex(struct mh_camd_runtime *runtime,
                                   const uint8_t *message, size_t length);
void mh_camd_runtime_poll(struct mh_camd_runtime *runtime,
                          mh_camd_output output, void *context);

#endif
