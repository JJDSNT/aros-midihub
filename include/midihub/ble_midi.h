#ifndef MIDIHUB_BLE_MIDI_H
#define MIDIHUB_BLE_MIDI_H

#include <stddef.h>
#include <stdint.h>

#define MH_BLE_MIDI_SERVICE_UUID \
    {0x03,0xb8,0x0e,0x5a,0xed,0xe8,0x4b,0x33,0xa7,0x51,0x6c,0xe3,0x4e,0xc4,0xc7,0x00}
#define MH_BLE_MIDI_IO_UUID \
    {0x77,0x72,0xe5,0xdb,0x38,0x68,0x41,0x12,0xa1,0xa9,0xf2,0x66,0x9d,0x10,0x6b,0xf3}

typedef int (*mh_ble_midi_byte_callback)(void *context, uint16_t timestamp,
                                          uint8_t byte);

struct mh_ble_midi_decoder {
    uint8_t sysex;
    uint16_t timestamp;
};

void mh_ble_midi_decoder_init(struct mh_ble_midi_decoder *decoder);
int mh_ble_midi_decode(struct mh_ble_midi_decoder *decoder,
                       const uint8_t *packet, size_t length,
                       mh_ble_midi_byte_callback callback, void *context);
int mh_ble_midi_encode_message(const uint8_t *message, size_t length,
                                uint16_t timestamp, uint8_t *packet,
                                size_t capacity, size_t *written);
int mh_ble_midi_encode_sysex_chunk(const uint8_t *message, size_t length,
                                    size_t *offset, uint16_t timestamp,
                                    uint8_t *packet, size_t capacity,
                                    size_t *written);

#endif
