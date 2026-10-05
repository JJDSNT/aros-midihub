#ifndef MIDIHUB_CAMD_BRIDGE_H
#define MIDIHUB_CAMD_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

struct mh_camd_bridge {
    void *camd_base;        /* each bridge holds camd.library open itself */
    void *node;
    void *to_clients;
    void *from_clients;
    uint8_t *sysex_buffer;
    int signal_bit;
};

typedef int (*mh_camd_output)(void *context, const uint8_t *message,
                              size_t length);

int mh_camd_bridge_open(struct mh_camd_bridge *bridge);
int mh_camd_bridge_open_named(struct mh_camd_bridge *bridge,
                              char *node_name, char *incoming_name,
                              char *outgoing_name);
void mh_camd_bridge_close(struct mh_camd_bridge *bridge);
void mh_camd_bridge_deliver(struct mh_camd_bridge *bridge,
                            const uint8_t *message, size_t length);
void mh_camd_bridge_deliver_sysex(struct mh_camd_bridge *bridge,
                                  const uint8_t *message, size_t length);
void mh_camd_bridge_poll(struct mh_camd_bridge *bridge,
                         mh_camd_output output, void *context);

#endif
