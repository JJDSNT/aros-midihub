#ifndef MIDIHUB_AROS_CAMD_H
#define MIDIHUB_AROS_CAMD_H

#include "../camd_bridge.h"

/* CAMD-facing state owned by the Network MIDI runtime.  Keeping this wrapper
 * separate from the reusable bridge gives the resident process one boundary
 * for CAMD lifecycle and leaves room for one bridge per peer later. */
struct netmidi_camd_runtime {
    struct aros_camd_bridge bridge;
    int opened;
};

int netmidi_camd_open(struct netmidi_camd_runtime *runtime);
void netmidi_camd_close(struct netmidi_camd_runtime *runtime);
int netmidi_camd_is_open(const struct netmidi_camd_runtime *runtime);
int netmidi_camd_signal_bit(const struct netmidi_camd_runtime *runtime);
void netmidi_camd_deliver(struct netmidi_camd_runtime *runtime,
                          const uint8_t *message, size_t length);
void netmidi_camd_deliver_sysex(struct netmidi_camd_runtime *runtime,
                                const uint8_t *message, size_t length);
void netmidi_camd_poll(struct netmidi_camd_runtime *runtime,
                       aros_camd_output output, void *context);

#endif
