#include <midihub/ble_midi.h>
#include <midihub/ble_peripheral.h>

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

static void check_advertising(void)
{
    uint8_t advertising[MH_BLE_LEGACY_AD_MAX];
    uint8_t scan_response[MH_BLE_LEGACY_AD_MAX];
    size_t advertising_length = 0, scan_length = 0;

    assert(mh_ble_peripheral_advertising("AROS MIDIHub", advertising,
                                         sizeof(advertising), &advertising_length,
                                         scan_response, sizeof(scan_response),
                                         &scan_length) == 0);
    assert(advertising_length == 21);
    assert(advertising[0] == 2 && advertising[1] == 0x01 && advertising[2] == 0x06);
    assert(advertising[3] == 17 && advertising[4] == 0x07);
    assert(advertising[5] == 0x00 && advertising[20] == 0x03);
    assert(scan_response[1] == 0x09);
    assert(scan_length == strlen("AROS MIDIHub") + 2);
    assert(memcmp(scan_response + 2, "AROS MIDIHub", strlen("AROS MIDIHub")) == 0);

    assert(mh_ble_peripheral_advertising("AROS MIDIHub peripheral name is too long",
                                         advertising, sizeof(advertising),
                                         &advertising_length, scan_response,
                                         sizeof(scan_response), &scan_length) == 0);
    assert(scan_length == MH_BLE_LEGACY_AD_MAX);
    assert(scan_response[1] == 0x08);
}

struct big_capture {
    uint8_t bytes[1024];
    size_t count;
};

static int capture_big(void *context, uint16_t time, uint8_t byte)
{
    struct big_capture *capture = context;
    (void)time;
    if (capture->count == sizeof(capture->bytes))
        return -1;
    capture->bytes[capture->count++] = byte;
    return 0;
}

/* Packets sized to the negotiated ATT MTU: each fits, and a long SysEx
   needs far fewer of them than with the 20-byte default. */
static size_t sysex_packets(const uint8_t *sysex, size_t length,
                            size_t capacity)
{
    struct mh_ble_midi_decoder decoder;
    struct big_capture result;
    uint8_t packet[244];
    size_t offset = 0, written, packets = 0;

    mh_ble_midi_decoder_init(&decoder);
    memset(&result, 0, sizeof(result));
    while (offset < length) {
        assert(mh_ble_midi_encode_sysex_chunk(sysex, length, &offset, 77,
                                              packet, capacity,
                                              &written) == 0);
        assert(written <= capacity);
        assert(mh_ble_midi_decode(&decoder, packet, written,
                                  capture_big, &result) == 0);
        packets++;
    }
    assert(result.count == length);
    assert(memcmp(result.bytes, sysex, length) == 0);
    return packets;
}

static void check_packet_sizes(void)
{
    uint8_t sysex[1000];
    size_t i, small, large;

    sysex[0] = 0xf0;
    for (i = 1; i < sizeof(sysex) - 1; i++)
        sysex[i] = (uint8_t)(i & 0x7f);
    sysex[sizeof(sysex) - 1] = 0xf7;
    small = sysex_packets(sysex, sizeof(sysex), 20);
    large = sysex_packets(sysex, sizeof(sysex), 244);
    assert(large * 10 < small);
}

struct message_log {
    uint8_t bytes[256];
    size_t length;
    size_t messages;
};

static void log_message(void *context, const uint8_t *message, size_t length)
{
    struct message_log *log = context;
    assert(log->length + length + 1 <= sizeof(log->bytes));
    memcpy(log->bytes + log->length, message, length);
    log->length += length;
    log->bytes[log->length++] = 0xff;   /* message boundary */
    log->messages++;
}

/* Two senders whose packets interleave: running status and a SysEx that
   spans packets must each stay with their own stream. */
static void check_streams(void)
{
    static struct mh_ble_midi_stream a, b;
    struct message_log log_a, log_b;
    /* A: a Note On, then two more using running status */
    const uint8_t a1[] = {0x80, 0x81, 0x90, 60, 100};
    const uint8_t a2[] = {0x80, 0x82, 0x90, 61, 101, 62, 102};
    /* B: SysEx split over two packets, with a Real-Time byte inside */
    const uint8_t b1[] = {0x80, 0x81, 0xf0, 0x7e, 0x01};
    const uint8_t b2[] = {0x80, 0x02, 0x81, 0xf8, 0x03, 0x82, 0xf7};
    const uint8_t a_expected[] = {0x90, 60, 100, 0xff, 0x90, 61, 101, 0xff,
                                  0x90, 62, 102, 0xff};
    const uint8_t b_expected[] = {0xf8, 0xff,
                                  0xf0, 0x7e, 0x01, 0x02, 0x03, 0xf7, 0xff};
    uint8_t big[1 + BTMIDI_SYSEX_MAX + 2];
    size_t i;

    memset(&log_a, 0, sizeof(log_a));
    memset(&log_b, 0, sizeof(log_b));
    mh_ble_midi_stream_init(&a, log_message, &log_a);
    mh_ble_midi_stream_init(&b, log_message, &log_b);
    assert(mh_ble_midi_stream_feed(&a, a1, sizeof(a1)) == 0);
    assert(mh_ble_midi_stream_feed(&b, b1, sizeof(b1)) == 0);
    assert(mh_ble_midi_stream_feed(&a, a2, sizeof(a2)) == 0);
    assert(mh_ble_midi_stream_feed(&b, b2, sizeof(b2)) == 0);
    assert(log_a.messages == 3 && log_a.length == sizeof(a_expected));
    assert(memcmp(log_a.bytes, a_expected, sizeof(a_expected)) == 0);
    assert(log_b.messages == 2 && log_b.length == sizeof(b_expected));
    assert(memcmp(log_b.bytes, b_expected, sizeof(b_expected)) == 0);

    /* a SysEx longer than the buffer is an error; the stream recovers */
    memset(&log_a, 0, sizeof(log_a));
    big[0] = 0x80;
    big[1] = 0x81;
    big[2] = 0xf0;
    for (i = 3; i < sizeof(big); i++)
        big[i] = 0x11;
    assert(mh_ble_midi_stream_feed(&a, big, sizeof(big)) == -1);
    assert(mh_ble_midi_stream_feed(&a, a1, sizeof(a1)) == 0);
    assert(log_a.messages == 1 && log_a.bytes[0] == 0x90);
}

static void check_port_names(void)
{
    char node[32], in[32], out[32];

    mh_ble_midi_port_names("KORG microKEY  ", node, in, out, sizeof(node));
    assert(!strcmp(node, "KORG microKEY"));
    assert(!strcmp(in, "KORG microKEY In"));
    assert(!strcmp(out, "KORG microKEY Out"));
    mh_ble_midi_port_names("A very long Bluetooth device name indeed",
                           node, in, out, sizeof(node));
    assert(strlen(out) == sizeof(out) - 1 && !strcmp(out + strlen(out) - 4, " Out"));
    assert(!strncmp(in, node, strlen(node)));
    mh_ble_midi_port_names(NULL, node, in, out, sizeof(node));
    assert(!strcmp(in, "BLE MIDI device In"));
    mh_ble_midi_port_names("   ", node, in, out, sizeof(node));
    assert(!strcmp(out, "BLE MIDI device Out"));
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
    check_advertising();
    check_packet_sizes();
    check_streams();
    check_port_names();
    puts("BLE MIDI codec OK");
    return 0;
}
