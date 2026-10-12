#ifndef NETMIDI_CONFIG_H
#define NETMIDI_CONFIG_H

#include <stdint.h>

/* A text file shared by the command line program and future Preferences UI.
 * Ports are control ports; the data socket uses the following port. */
struct netmidi_network_config {
    uint16_t local_port;
    uint16_t peer_port;
    char peer_ip[64];
    char session_name[64];
    /* "port.<n> = <name>": the driver's port n is kept for the peer whose
     * session has that name */
    char port_name[16][64];
    /* "peer_name": a peer to invite wherever it is advertised under that
     * session name. "latency_ms": added to the time each received event
     * is due, 0 to 2000. "accept": anyone, nobody, or a list of addresses
     * and session names with commas between. */
    char peer_name[64];
    int latency_ms;
    char accept[256];
};

void netmidi_config_defaults(struct netmidi_network_config *config);

/* Returns 0 on success, 1 if the file does not exist, -1 on invalid input
 * or another I/O error. A failed parse leaves *config unchanged. */
int netmidi_config_load(const char *path, struct netmidi_network_config *config);

#endif
