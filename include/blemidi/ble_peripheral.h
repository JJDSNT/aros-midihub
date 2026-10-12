#ifndef BLEMIDI_PERIPHERAL_H
#define BLEMIDI_PERIPHERAL_H

#include <stddef.h>
#include <stdint.h>

#define BLEMIDI_LEGACY_AD_MAX 31

/* Builds the controller payloads used by the BLE MIDI peripheral role.
 * The primary advertisement contains the LE flags and complete BLE MIDI
 * service UUID. The scan response contains the complete or shortened name. */
int blemidi_peripheral_advertising(const char *name,
                                  uint8_t *advertising, size_t advertising_capacity,
                                  size_t *advertising_length,
                                  uint8_t *scan_response, size_t scan_capacity,
                                  size_t *scan_length);

#endif
