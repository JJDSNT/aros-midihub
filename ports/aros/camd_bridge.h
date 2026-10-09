#ifndef AROS_CAMD_BRIDGE_H
#define AROS_CAMD_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

struct aros_camd_bridge {
    void *camd_base;        /* each bridge holds camd.library open itself */
    void *node;
    void *to_clients;
    void *from_clients;
    uint8_t *sysex_buffer;
    int signal_bit;
};

typedef int (*aros_camd_output)(void *context, const uint8_t *message,
                                size_t length);

int aros_camd_bridge_open(struct aros_camd_bridge *bridge);
int aros_camd_bridge_open_named(struct aros_camd_bridge *bridge,
                                char *node_name, char *incoming_name,
                                char *outgoing_name);
void aros_camd_bridge_close(struct aros_camd_bridge *bridge);
void aros_camd_bridge_deliver(struct aros_camd_bridge *bridge,
                              const uint8_t *message, size_t length);
void aros_camd_bridge_deliver_sysex(struct aros_camd_bridge *bridge,
                                    const uint8_t *message, size_t length);
void aros_camd_bridge_poll(struct aros_camd_bridge *bridge,
                           aros_camd_output output, void *context);
/* Use CAMD's monotonic millisecond clock when version 42 is available.
   Older libraries keep the caller-provided clock for compatibility. */
uint32_t aros_camd_bridge_time_ms(struct aros_camd_bridge *bridge,
                                  uint32_t fallback_ms);

#endif
