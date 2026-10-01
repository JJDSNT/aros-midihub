#include <midihub/ble_midi.h>
#include <midihub/rtpmidi.h>
#include "midihub/camd_bridge.h"

#ifdef __AROS__
#include <exec/libraries.h>
#include <exec/ports.h>
#include <libraries/bluetooth.h>
#include <proto/bluetooth.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

struct Library *BluetoothBase;

struct ble_runtime {
    struct mh_camd_bridge camd;
    struct mh_ble_midi_decoder decoder;
    APTR write_channel;
    uint8_t message[3];
    uint8_t message_length;
    uint8_t message_needed;
    uint8_t sysex[MH_SYSEX_MAX];
    size_t sysex_length;
    unsigned long received;
    unsigned long sent;
    size_t packet_limit;
};

static uint16_t now_milliseconds(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint16_t)(((uint64_t)now.tv_sec * 1000 +
                       (uint64_t)now.tv_usec / 1000) & 0x1fff);
}

static int received_byte(void *context, uint16_t timestamp, uint8_t byte)
{
    struct ble_runtime *runtime = context;
    (void)timestamp;
    if (byte >= 0xf8)
        return 0;
    if (runtime->sysex_length) {
        if (runtime->sysex_length == MH_SYSEX_MAX) {
            runtime->sysex_length = 0;
            return -1;
        }
        runtime->sysex[runtime->sysex_length++] = byte;
        if (byte == 0xf7) {
            mh_camd_bridge_deliver_sysex(&runtime->camd, runtime->sysex,
                                         runtime->sysex_length);
            runtime->sysex_length = 0;
            runtime->received++;
        }
        return 0;
    }
    if (byte == 0xf0) {
        runtime->sysex[0] = byte;
        runtime->sysex_length = 1;
        runtime->message_length = runtime->message_needed = 0;
        return 0;
    }
    if (byte & 0x80) {
        if (byte > 0xef) {
            runtime->message_length = runtime->message_needed = 0;
            return 0;
        }
        runtime->message[0] = byte;
        runtime->message_length = 1;
        runtime->message_needed = (byte & 0xf0) == 0xc0 ||
                                  (byte & 0xf0) == 0xd0 ? 1 : 2;
        return 0;
    }
    if (!runtime->message_needed)
        return -1;
    runtime->message[runtime->message_length++] = byte;
    if (!--runtime->message_needed) {
        mh_camd_bridge_deliver(&runtime->camd, runtime->message,
                                runtime->message_length);
        runtime->received++;
        runtime->message_length = 0;
    }
    return 0;
}

static int send_to_ble(void *context, const uint8_t *message, size_t length)
{
    struct ble_runtime *runtime = context;
    uint8_t packet[512];
    size_t written;
    uint16_t timestamp = now_milliseconds();
    if (length >= 2 && message[0] == 0xf0 &&
        message[length - 1] == 0xf7) {
        size_t offset = 0;
        while (offset < length) {
            if (mh_ble_midi_encode_sysex_chunk(message, length, &offset,
                                                timestamp, packet,
                                                runtime->packet_limit,
                                                &written) ||
                btDoChannel(runtime->write_channel, packet, written))
                return -1;
        }
    } else {
        if (mh_ble_midi_encode_message(message, length, timestamp, packet,
                                        runtime->packet_limit, &written) ||
            btDoChannel(runtime->write_channel, packet, written))
            return -1;
    }
    runtime->sent++;
    return 0;
}

int main(int argc, char **argv)
{
    static const uint8_t service_uuid[16] = MH_BLE_MIDI_SERVICE_UUID;
    static const uint8_t io_uuid[16] = MH_BLE_MIDI_IO_UUID;
    static char node_name[] = "MIDIHub BLE";
    static char incoming_name[] = "MIDIHub BLE In";
    static char outgoing_name[] = "MIDIHub BLE Out";
    struct ble_runtime runtime;
    struct BtDevice *device;
    struct BtService *service;
    struct BtEndpoint *endpoint;
    struct MsgPort *port = NULL;
    APTR read_channel = NULL;
    uint8_t read_buffer[512];
    ULONG signals;
    IPTR max_packet = 20, can_read = 0, can_write = 0;
    int result = 20;

    if (argc != 2) {
        printf("Usage: MIDIHubBLE <Bluetooth address from BTDevLister>\n");
        return 20;
    }
    memset(&runtime, 0, sizeof(runtime));
    runtime.camd.signal_bit = -1;
    mh_ble_midi_decoder_init(&runtime.decoder);
    BluetoothBase = OpenLibrary("bluetooth.library", 1);
    if (!BluetoothBase) {
        printf("bluetooth.library is unavailable\n");
        return 20;
    }
    btLockReadBase();
    device = btFindDevice(NULL, BDA_AddressString, (IPTR)argv[1], TAG_END);
    btUnlockBase();
    if (!device) {
        printf("Bluetooth address not found: %s\n", argv[1]);
        goto done;
    }
    btLockReadDevice(device);
    service = btFindService(device, NULL, BSVA_UUID, (IPTR)service_uuid,
                            BSVA_Protocol, BSVP_ATT, TAG_END);
    endpoint = service ? btFindEndpoint(service, NULL,
                                        BEA_UUID, (IPTR)io_uuid,
                                        BEA_Type, BEPT_GATT_CHAR,
                                        TAG_END) : NULL;
    btUnlockDevice(device);
    if (!endpoint) {
        printf("BLE MIDI GATT service/characteristic not found\n");
        goto done;
    }
    btGetAttrs(BGA_ENDPOINT, endpoint,
               BEA_MaxPktSize, &max_packet,
               BEA_CanRead, &can_read,
               BEA_CanWrite, &can_write,
               TAG_END);
    if (!can_read || !can_write) {
        printf("BLE MIDI characteristic lacks notify or write support\n");
        goto done;
    }
    runtime.packet_limit = max_packet >= 5 && max_packet <= sizeof(read_buffer)
                         ? max_packet : 20;
    if (!(port = CreateMsgPort()))
        goto done;
    read_channel = btAllocChannel(device, port, endpoint);
    runtime.write_channel = btAllocChannel(device, port, endpoint);
    if (!read_channel || !runtime.write_channel)
        goto done;
    btSetAttrs(BGA_CHANNEL, read_channel, BCHA_AutoConnect, TRUE, TAG_END);
    btSetAttrs(BGA_CHANNEL, runtime.write_channel, BCHA_AutoConnect, TRUE,
               TAG_END);
    btChannelSetup(read_channel, BTPR_READ, 0, 0);
    btChannelSetup(runtime.write_channel, BTPR_WRITE, 0, 0);
    if (mh_camd_bridge_open_named(&runtime.camd, node_name, incoming_name,
                                   outgoing_name)) {
        printf("Cannot open CAMD\n");
        goto done;
    }
    printf("BLE MIDI connected to CAMD as %s / %s\n",
           incoming_name, outgoing_name);
    btSendChannel(read_channel, read_buffer, sizeof(read_buffer));
    result = 0;
    for (;;) {
        signals = Wait((1UL << port->mp_SigBit) |
                       (1UL << runtime.camd.signal_bit) |
                       SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C)
            break;
        if (signals & (1UL << port->mp_SigBit)) {
            APTR channel;
            while ((channel = (APTR)GetMsg(port))) {
                if (channel == read_channel) {
                    LONG error = btGetChannelError(channel);
                    if (!error) {
                        ULONG actual = btGetChannelActual(channel);
                        if (mh_ble_midi_decode(&runtime.decoder, read_buffer,
                                                actual, received_byte,
                                                &runtime)) {
                            printf("Malformed BLE MIDI notification\n");
                            mh_ble_midi_decoder_init(&runtime.decoder);
                            runtime.sysex_length = 0;
                            runtime.message_length = 0;
                            runtime.message_needed = 0;
                        }
                    } else {
                        printf("BLE MIDI read error %ld\n", error);
                        btDelayMS(250);
                    }
                    btSendChannel(read_channel, read_buffer,
                                  sizeof(read_buffer));
                }
            }
        }
        if (signals & (1UL << runtime.camd.signal_bit))
            mh_camd_bridge_poll(&runtime.camd, send_to_ble, &runtime);
    }
    printf("BLE MIDI: %lu received, %lu sent\n",
           runtime.received, runtime.sent);
done:
    mh_camd_bridge_close(&runtime.camd);
    if (read_channel) {
        btAbortChannel(read_channel);
        btWaitChannel(read_channel);
        btFreeChannel(read_channel);
    }
    if (runtime.write_channel)
        btFreeChannel(runtime.write_channel);
    if (port)
        DeleteMsgPort(port);
    CloseLibrary(BluetoothBase);
    return result;
}
#else
int main(void)
{
    return 1;
}
#endif
