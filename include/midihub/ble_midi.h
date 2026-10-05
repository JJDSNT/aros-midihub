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

/* Reassembles complete MIDI messages from the BLE MIDI packets of one
   sender: running status and SysEx span packets, so every connection needs
   its own stream. The callback receives each channel, System Common or
   Real-Time message, and each SysEx from F0 to F7. */
#define MH_BLE_MIDI_SYSEX_MAX 4096

typedef void (*mh_ble_midi_message_callback)(void *context,
                                             const uint8_t *message,
                                             size_t length);

struct mh_ble_midi_stream {
    struct mh_ble_midi_decoder decoder;
    mh_ble_midi_message_callback callback;
    void *context;
    uint8_t message[3];
    uint8_t message_length;
    uint8_t message_needed;
    size_t sysex_length;
    uint8_t sysex[MH_BLE_MIDI_SYSEX_MAX];
};

void mh_ble_midi_stream_init(struct mh_ble_midi_stream *stream,
                             mh_ble_midi_message_callback callback,
                             void *context);
/* Forgets a partial message, e.g. after the link was lost. */
void mh_ble_midi_stream_reset(struct mh_ble_midi_stream *stream);
/* Returns -1 for a malformed packet or an oversized SysEx; the stream is
   then reset and the next packet starts afresh. */
int mh_ble_midi_stream_feed(struct mh_ble_midi_stream *stream,
                            const uint8_t *packet, size_t length);

/* The CAMD names of a BLE MIDI device AROS connects to: the node is named
   after the device, its clusters "<name> In" (what the device plays) and
   "<name> Out" (what is sent to it). Each buffer holds size bytes; the name
   is shortened so that " Out" still fits. btmidi.class makes the ports and
   MIDIHub.prefs recognises them by these names. */
void mh_ble_midi_port_names(const char *device_name, char *node, char *in,
                            char *out, size_t size);

#endif
