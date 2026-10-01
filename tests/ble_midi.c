#include <midihub/ble_midi.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct capture {
    uint8_t bytes[64];
    uint16_t times[64];
    size_t count;
};

static int capture_byte(void *context, uint16_t time, uint8_t byte)
{
    struct capture *capture = context;
    if (capture->count == sizeof(capture->bytes))
        return -1;
    capture->bytes[capture->count] = byte;
    capture->times[capture->count++] = time;
    return 0;
}

static void check(struct mh_ble_midi_decoder *decoder,
                  const uint8_t *packet, size_t length,
                  const uint8_t *expected, size_t expected_length,
                  uint16_t timestamp)
{
    struct capture result = {{0}, {0}, 0};
    size_t i;
    assert(mh_ble_midi_decode(decoder, packet, length,
                              capture_byte, &result) == 0);
    assert(result.count == expected_length);
    assert(memcmp(result.bytes, expected, expected_length) == 0);
    for (i = 0; i < result.count; i++)
        assert(result.times[i] == timestamp);
}

int main(void)
{
    struct mh_ble_midi_decoder decoder;
    uint8_t packet[8];
    size_t written = 0;
    const uint8_t note[] = {0x90, 0x3c, 0x64};
    const uint8_t program[] = {0xc0, 0x05};
    const uint8_t running_packet[] = {0x80, 0x81, 0x90, 0x3c, 0x64, 0x3d, 0x65};
    const uint8_t running_expected[] = {0x90, 0x3c, 0x64, 0x90, 0x3d, 0x65};
    const uint8_t sysex_start[] = {0x80, 0x82, 0xf0, 0x01, 0x02};
    const uint8_t sysex_continue[] = {0x80, 0x03, 0x04, 0x82, 0xf7};
    const uint8_t sysex_first_expected[] = {0xf0, 0x01, 0x02};
    const uint8_t sysex_last_expected[] = {0x03, 0x04, 0xf7};
    const uint8_t long_sysex[] = {0xf0, 1, 2, 3, 4, 5, 6, 7, 8, 0xf7};
    const uint8_t boundary_sysex[] = {0xf0, 1, 2, 3, 4, 5, 0xf7};
    const uint8_t wrap_packet[] = {0x80, 0xfe, 0x90, 60, 64,
                                   0x81, 0x80, 60, 0};
    const uint8_t wrap_expected[] = {0x90, 60, 64, 0x80, 60, 0};
    const uint8_t realtime_sysex[] =
        {0x80, 0x81, 0xf0, 0x01, 0x81, 0xf8, 0x02, 0x81, 0xf7};
    const uint8_t realtime_sysex_expected[] =
        {0xf0, 0x01, 0xf8, 0x02, 0xf7};
    struct capture result = {{0}, {0}, 0};
    struct capture roundtrip = {{0}, {0}, 0};
    size_t offset = 0;
    mh_ble_midi_decoder_init(&decoder);
    assert(mh_ble_midi_encode_message(note, sizeof(note), 129,
                                       packet, sizeof(packet), &written) == 0);
    check(&decoder, packet, written, note, sizeof(note), 129);
    assert(mh_ble_midi_encode_message(program, sizeof(program), 130,
                                       packet, sizeof(packet), &written) == 0);
    check(&decoder, packet, written, program, sizeof(program), 130);
    check(&decoder, running_packet, sizeof(running_packet),
          running_expected, sizeof(running_expected), 1);
    check(&decoder, sysex_start, sizeof(sysex_start),
          sysex_first_expected, sizeof(sysex_first_expected), 2);
    assert(mh_ble_midi_decode(&decoder, sysex_continue,
                              sizeof(sysex_continue),
                              capture_byte, &result) == 0);
    assert(result.count == sizeof(sysex_last_expected));
    assert(memcmp(result.bytes, sysex_last_expected,
                  sizeof(sysex_last_expected)) == 0);
    while (offset < sizeof(long_sysex)) {
        assert(mh_ble_midi_encode_sysex_chunk(long_sysex,
                    sizeof(long_sysex), &offset, 384, packet,
                    sizeof(packet), &written) == 0);
        assert(mh_ble_midi_decode(&decoder, packet, written,
                                  capture_byte, &roundtrip) == 0);
    }
    assert(roundtrip.count == sizeof(long_sysex));
    assert(memcmp(roundtrip.bytes, long_sysex,
                  sizeof(long_sysex)) == 0);
    offset = 0;
    memset(&roundtrip, 0, sizeof(roundtrip));
    while (offset < sizeof(boundary_sysex)) {
        assert(mh_ble_midi_encode_sysex_chunk(boundary_sysex,
                    sizeof(boundary_sysex), &offset, 384, packet,
                    sizeof(packet), &written) == 0);
        assert(written <= sizeof(packet));
        assert(mh_ble_midi_decode(&decoder, packet, written,
                                  capture_byte, &roundtrip) == 0);
    }
    assert(roundtrip.count == sizeof(boundary_sysex));
    assert(memcmp(roundtrip.bytes, boundary_sysex,
                  sizeof(boundary_sysex)) == 0);
    memset(&result, 0, sizeof(result));
    assert(mh_ble_midi_decode(&decoder, wrap_packet, sizeof(wrap_packet),
                              capture_byte, &result) == 0);
    assert(result.count == sizeof(wrap_expected));
    assert(memcmp(result.bytes, wrap_expected, sizeof(wrap_expected)) == 0);
    assert(result.times[0] == 126 && result.times[3] == 129);
    check(&decoder, realtime_sysex, sizeof(realtime_sysex),
          realtime_sysex_expected, sizeof(realtime_sysex_expected), 1);
    puts("BLE MIDI codec OK");
    return 0;
}
