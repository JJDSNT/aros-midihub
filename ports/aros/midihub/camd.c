#include "camd.h"

#include <string.h>

int mh_camd_runtime_open(struct mh_camd_runtime *runtime)
{
    if (!runtime)
        return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->bridge.signal_bit = -1;
    if (mh_camd_bridge_open(&runtime->bridge) != 0)
        return -1;
    runtime->opened = 1;
    return 0;
}

void mh_camd_runtime_close(struct mh_camd_runtime *runtime)
{
    if (!runtime || !runtime->opened)
        return;
    mh_camd_bridge_close(&runtime->bridge);
    runtime->opened = 0;
}

int mh_camd_runtime_is_open(const struct mh_camd_runtime *runtime)
{
    return runtime && runtime->opened;
}

int mh_camd_runtime_signal_bit(const struct mh_camd_runtime *runtime)
{
    if (!runtime || !runtime->opened)
        return -1;
    return runtime->bridge.signal_bit;
}

void mh_camd_runtime_deliver(struct mh_camd_runtime *runtime,
                             const uint8_t *message, size_t length)
{
    if (runtime && runtime->opened)
        mh_camd_bridge_deliver(&runtime->bridge, message, length);
}

void mh_camd_runtime_deliver_sysex(struct mh_camd_runtime *runtime,
                                   const uint8_t *message, size_t length)
{
    if (runtime && runtime->opened)
        mh_camd_bridge_deliver_sysex(&runtime->bridge, message, length);
}

void mh_camd_runtime_poll(struct mh_camd_runtime *runtime,
                          mh_camd_output output, void *context)
{
    if (runtime && runtime->opened)
        mh_camd_bridge_poll(&runtime->bridge, output, context);
}
