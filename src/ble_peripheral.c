#include <midihub/ble_peripheral.h>
#include <midihub/ble_midi.h>

#include <string.h>

int mh_ble_peripheral_advertising(const char *name,
                                  uint8_t *advertising, size_t advertising_capacity,
                                  size_t *advertising_length,
                                  uint8_t *scan_response, size_t scan_capacity,
                                  size_t *scan_length)
{
    static const uint8_t service_uuid[16] = MH_BLE_MIDI_SERVICE_UUID;
    size_t name_length, i;

    if (!name || !advertising || !advertising_length || !scan_response ||
        !scan_length || advertising_capacity < 21 || scan_capacity < 2)
        return -1;

    /* Flags: general discoverable mode, BR/EDR unsupported. */
    advertising[0] = 2;
    advertising[1] = 0x01;
    advertising[2] = 0x06;
    advertising[3] = 17;
    advertising[4] = 0x07; /* complete list of 128-bit service UUIDs */
    for (i = 0; i < sizeof(service_uuid); i++)
        advertising[5 + i] = service_uuid[sizeof(service_uuid) - 1 - i];
    *advertising_length = 21;

    name_length = strlen(name);
    if (name_length > scan_capacity - 2)
        name_length = scan_capacity - 2;
    if (name_length > MH_BLE_LEGACY_AD_MAX - 2)
        name_length = MH_BLE_LEGACY_AD_MAX - 2;
    scan_response[0] = (uint8_t)(name_length + 1);
    scan_response[1] = strlen(name) == name_length ? 0x09 : 0x08;
    if (name_length)
        memcpy(scan_response + 2, name, name_length);
    *scan_length = name_length + 2;
    return 0;
}
