#ifndef NETMIDI_MDNS_H
#define NETMIDI_MDNS_H

#include <stddef.h>
#include <stdint.h>

/* AppleMIDI's DNS-SD service type, over IPv4 mDNS. */
#define NETMIDI_MDNS_PORT 5353

int netmidi_mdns_build(const char *session_name, const char *host_name,
                  const uint8_t address[4], uint16_t control_port,
                  uint32_t ttl, uint16_t query_id,
                  uint8_t *out, size_t capacity, size_t *length);

/* Return 1 when a DNS question asks for our AppleMIDI service, 0 when
 * unrelated, and -1 when malformed. Set unicast for a QU or legacy query. */
int netmidi_mdns_query(const uint8_t *packet, size_t length,
                  const char *session_name, const char *host_name,
                  int *unicast);

#endif
