#include "camd.h"

#include <string.h>

int netmidi_camd_open(struct netmidi_camd_runtime *runtime)
{
    if (!runtime)
        return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->bridge.signal_bit = -1;
    if (aros_camd_bridge_open(&runtime->bridge) != 0)
        return -1;
    runtime->opened = 1;
    return 0;
}

void netmidi_camd_close(struct netmidi_camd_runtime *runtime)
{
    if (!runtime || !runtime->opened)
        return;
    aros_camd_bridge_close(&runtime->bridge);
    runtime->opened = 0;
}

int netmidi_camd_is_open(const struct netmidi_camd_runtime *runtime)
{
    return runtime && runtime->opened;
}

int netmidi_camd_signal_bit(const struct netmidi_camd_runtime *runtime)
{
    if (!runtime || !runtime->opened)
        return -1;
    return runtime->bridge.signal_bit;
}

void netmidi_camd_deliver(struct netmidi_camd_runtime *runtime,
                          const uint8_t *message, size_t length)
{
    if (runtime && runtime->opened)
        aros_camd_bridge_deliver(&runtime->bridge, message, length);
}

void netmidi_camd_deliver_sysex(struct netmidi_camd_runtime *runtime,
                                const uint8_t *message, size_t length)
{
    if (runtime && runtime->opened)
        aros_camd_bridge_deliver_sysex(&runtime->bridge, message, length);
}

void netmidi_camd_poll(struct netmidi_camd_runtime *runtime,
                       aros_camd_output output, void *context)
{
    if (runtime && runtime->opened)
        aros_camd_bridge_poll(&runtime->bridge, output, context);
}
