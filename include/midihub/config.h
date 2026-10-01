#ifndef MIDIHUB_CONFIG_H
#define MIDIHUB_CONFIG_H

#include <stdint.h>

/* A text file shared by the command line program and future Preferences UI.
 * Ports are control ports; the data socket uses the following port. */
struct mh_network_config {
    uint16_t local_port;
    uint16_t peer_port;
    char peer_ip[64];
    char session_name[64];
};

void mh_config_defaults(struct mh_network_config *config);

/* Returns 0 on success, 1 if the file does not exist, -1 on invalid input
 * or another I/O error. A failed parse leaves *config unchanged. */
int mh_config_load(const char *path, struct mh_network_config *config);

#endif
