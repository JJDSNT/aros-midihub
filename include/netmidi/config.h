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
    int session_name_given;     /* the file says it: not the default */
    /* "applemidi.port.<n> = <name>": port n of the driver applemidi is
     * kept for the peer whose session has that name.
     * "networkmidi2.port.<n> = <name>": the same of the driver
     * networkmidi2, by the peer's endpoint name. */
    char port_name[16][64];
    char midi2_port_name[16][64];
    /* "midi2_port": the UDP port Network MIDI 2.0 is answered on; without
     * it there is none */
    uint16_t midi2_port;
    /* A Network MIDI 2.0 Host to invite, as a Client: at "midi2_peer_ip"
     * and "midi2_peer_port", or wherever one is advertised whose endpoint
     * is called "midi2_peer_name". Either needs midi2_port. */
    char midi2_peer_ip[64];
    uint16_t midi2_peer_port;
    char midi2_peer_name[64];
    /* "peer_name": a peer to invite wherever it is advertised under that
     * session name. "latency_ms": added to the time each received event
     * is due, 0 to 2000. "accept": anyone, nobody, or a list of addresses
     * and session names with commas between. */
    char peer_name[64];
    /* "max_peers": AppleMIDI peers at once, 1 to 8; 0 for all there is
     * room for */
    int max_peers;
    int latency_ms;
    char accept[256];
};

void netmidi_config_defaults(struct netmidi_network_config *config);

/* Returns 0 on success, 1 if the file does not exist, -1 on invalid input
 * or another I/O error. A failed parse leaves *config unchanged. */
int netmidi_config_load(const char *path, struct netmidi_network_config *config);

/* Writes every setting to path, in the form netmidi_config_load reads.
 * Returns 0, or -1 when the file could not be written whole. */
int netmidi_config_save(const char *path,
                        const struct netmidi_network_config *config);

#endif
