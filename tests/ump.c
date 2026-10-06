#include "midihub/ump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

/* Collects UMP words and MIDI 1.0 bytes from the translators. */
static uint32_t ump[64];
static unsigned ump_count;
static uint8_t midi[256];
static size_t midi_count;

static void take_ump(void *context, const uint32_t *words, unsigned count)
{
    (void)context;
    memcpy(ump + ump_count, words, count * sizeof(*words));
    ump_count += count;
}

static void take_midi(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    memcpy(midi + midi_count, bytes, length);
    midi_count += length;
}

static void to_ump(const uint8_t *bytes, size_t length, uint8_t group)
{
    struct mh_ump_from_midi1 state;
    size_t i;

    ump_count = 0;
    mh_ump_from_midi1_init(&state, group);
    for (i = 0; i < length; ++i)
        mh_ump_from_midi1_byte(&state, bytes[i], take_ump, NULL);
}

static void to_midi(const uint32_t *words, unsigned count, uint8_t group)
{
    struct mh_ump_to_midi1 state;
    unsigned i = 0;

    midi_count = 0;
    mh_ump_to_midi1_init(&state, group);
    while (i < count) {
        unsigned n = mh_ump_words(words[i]);
        CHECK(mh_ump_to_midi1(&state, words + i, count - i, take_midi, NULL) == 0);
        i += n;
    }
}

static void test_sizes(void)
{
    CHECK(mh_ump_words(0x00000000u) == 1);
    CHECK(mh_ump_words(0x20903c64u) == 1);
    CHECK(mh_ump_words(0x30000000u) == 2);
    CHECK(mh_ump_words(0x40000000u) == 2);
    CHECK(mh_ump_words(0x50000000u) == 4);
    CHECK(mh_ump_words(0xb0000000u) == 3);
    CHECK(mh_ump_words(0xf0000000u) == 4);
}

static void test_scaling(void)
{
    /* The UMP specification's own examples: minimum, center and maximum
       keep their places. */
    CHECK(mh_ump_scale_up(0, 7, 16) == 0);
    CHECK(mh_ump_scale_up(64, 7, 16) == 0x8000);
    CHECK(mh_ump_scale_up(127, 7, 16) == 0xffff);
    CHECK(mh_ump_scale_up(0x2000, 14, 32) == 0x80000000u);
    CHECK(mh_ump_scale_up(0x3fff, 14, 32) == 0xffffffffu);
    CHECK(mh_ump_scale_up(127, 7, 32) == 0xffffffffu);
    CHECK(mh_ump_scale_down(0xffff, 16, 7) == 127);
    CHECK(mh_ump_scale_down(0x8000, 16, 7) == 64);
    /* Up and back down is the identity for every 7-bit value. */
    {
        uint32_t v;
        int ok = 1;
        for (v = 0; v < 128; ++v)
            if (mh_ump_scale_down(mh_ump_scale_up(v, 7, 16), 16, 7) != v ||
                mh_ump_scale_down(mh_ump_scale_up(v, 7, 32), 32, 7) != v)
                ok = 0;
        CHECK(ok);
    }
}

static void test_midi1_to_ump(void)
{
    static const uint8_t notes[] = { 0x91, 0x3c, 0x64, 0x3e, 0x50, 0xc2, 0x05 };
    static const uint8_t system[] = { 0xf8, 0xf2, 0x10, 0x20, 0xf6 };
    static const uint8_t sysex13[] = { 0xf0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0xf7 };
    static const uint8_t sysex6[] = { 0xf0, 0x7e, 0x7f, 0x06, 0x01, 0x11, 0x22, 0xf7 };
    static const uint8_t sysex_clock[] = { 0xf0, 0x7e, 0xf8, 0x7f, 0xf7 };

    to_ump(notes, sizeof(notes), 3);
    CHECK(ump_count == 3);
    CHECK(ump[0] == 0x23913c64u);
    CHECK(ump[1] == 0x23913e50u);          /* running status */
    CHECK(ump[2] == 0x23c20500u);

    to_ump(system, sizeof(system), 0);
    CHECK(ump_count == 3);
    CHECK(ump[0] == 0x10f80000u);
    CHECK(ump[1] == 0x10f21020u);
    CHECK(ump[2] == 0x10f60000u);

    /* 13 bytes: Start (6), Continue (6), End (1) */
    to_ump(sysex13, sizeof(sysex13), 0);
    CHECK(ump_count == 6);
    CHECK(ump[0] == 0x30160102u && ump[1] == 0x03040506u);
    CHECK(ump[2] == 0x30260708u && ump[3] == 0x090a0b0cu);
    CHECK(ump[4] == 0x30310d00u && ump[5] == 0x00000000u);

    /* Exactly 6 bytes fit one Complete packet. */
    to_ump(sysex6, sizeof(sysex6), 0);
    CHECK(ump_count == 2);
    CHECK(ump[0] == 0x30067e7fu && ump[1] == 0x06011122u);

    /* A clock inside SysEx comes out on its own. */
    to_ump(sysex_clock, sizeof(sysex_clock), 0);
    CHECK(ump_count == 3);
    CHECK(ump[0] == 0x10f80000u);
    CHECK(ump[1] == 0x30027e7fu);
}

static void test_ump_to_midi1(void)
{
    static const uint32_t voice[] = { 0x22903c64u, 0x22c00500u, 0x22e00040u };
    static const uint8_t voice_bytes[] = { 0x90, 0x3c, 0x64, 0xc0, 0x05, 0xe0, 0x00, 0x40 };
    static const uint32_t sysex[] = {
        0x30160102u, 0x03040506u, 0x30260708u, 0x090a0b0cu, 0x30310d00u, 0
    };
    static const uint8_t sysex_bytes[] = {
        0xf0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0xf7
    };
    static const uint32_t other_group[] = { 0x23903c64u };

    to_midi(voice, 3, 2);
    CHECK(midi_count == sizeof(voice_bytes) && !memcmp(midi, voice_bytes, midi_count));

    to_midi(sysex, 6, 0);
    CHECK(midi_count == sizeof(sysex_bytes) && !memcmp(midi, sysex_bytes, midi_count));

    to_midi(other_group, 1, 2);
    CHECK(midi_count == 0);
}

static void test_midi2_down(void)
{
    /* MIDI 2.0 Note On, group 0, channel 1, note 0x40, velocity 0x8000 */
    static const uint32_t note_on[] = { 0x40914000u, 0x80000000u };
    /* Velocity too small for 7 bits: a MIDI 1.0 Note On must not be 0 */
    static const uint32_t soft[] = { 0x40914000u, 0x00010000u };
    static const uint32_t note_off[] = { 0x40814000u, 0xffff0000u };
    static const uint32_t cc[] = { 0x40b10700u, 0xffffffffu };
    static const uint32_t bend[] = { 0x40e10000u, 0x80000000u };
    /* Program Change 5 with bank 1/2 (Bank Valid set) */
    static const uint32_t program[] = { 0x40c10001u, 0x05000102u };
    /* RPN 0/0 (pitch bend range) = 0x2000 << 18 */
    static const uint32_t rpn[] = { 0x40210000u, 0x80000000u };
    static const uint32_t per_note_bend[] = { 0x40614000u, 0x80000000u };

    to_midi(note_on, 2, 0);
    CHECK(midi_count == 3 && midi[0] == 0x91 && midi[1] == 0x40 && midi[2] == 64);
    to_midi(soft, 2, 0);
    CHECK(midi_count == 3 && midi[2] == 1);
    to_midi(note_off, 2, 0);
    CHECK(midi_count == 3 && midi[0] == 0x81 && midi[2] == 127);
    to_midi(cc, 2, 0);
    CHECK(midi_count == 3 && midi[0] == 0xb1 && midi[1] == 7 && midi[2] == 127);
    to_midi(bend, 2, 0);
    CHECK(midi_count == 3 && midi[0] == 0xe1 && midi[1] == 0x00 && midi[2] == 0x40);
    to_midi(program, 2, 0);
    {
        static const uint8_t expected[] = { 0xb1, 0, 1, 0xb1, 32, 2, 0xc1, 5 };
        CHECK(midi_count == sizeof(expected) && !memcmp(midi, expected, midi_count));
    }
    to_midi(rpn, 2, 0);
    {
        static const uint8_t expected[] = {
            0xb1, 101, 0, 0xb1, 100, 0, 0xb1, 6, 0x40, 0xb1, 38, 0
        };
        CHECK(midi_count == sizeof(expected) && !memcmp(midi, expected, midi_count));
    }
    to_midi(per_note_bend, 2, 0);
    CHECK(midi_count == 0);
}

static void test_stream(void)
{
    uint32_t words[16];
    char text[32];
    unsigned n, i, j;

    mh_ump_endpoint_info(words, MH_UMP_PROTOCOL_MIDI1);
    CHECK(words[0] == 0xf0010101u);
    CHECK(words[1] == 0x80000100u);
    CHECK(mh_ump_stream_status(words) == MH_UMP_ENDPOINT_INFO);

    mh_ump_stream_config(words, MH_UMP_PROTOCOL_MIDI1);
    CHECK(words[0] == 0xf0060100u);

    /* "AROS MIDIHub Pi3" is 16 bytes: a Start and an End packet */
    n = mh_ump_stream_text(words, 16, MH_UMP_ENDPOINT_NAME, "AROS MIDIHub Pi3", 16);
    CHECK(n == 8);
    CHECK((words[0] >> 26) == ((0xfu << 2) | 1));   /* Start */
    CHECK((words[4] >> 26) == ((0xfu << 2) | 3));   /* End */
    CHECK(mh_ump_stream_status(words + 4) == MH_UMP_ENDPOINT_NAME);
    for (i = 0, j = 0; i < 2; ++i) {
        const uint32_t *p = words + i * 4;
        text[j++] = (char)(p[0] >> 8);
        text[j++] = (char)p[0];
        for (n = 1; n < 4; ++n) {
            text[j++] = (char)(p[n] >> 24);
            text[j++] = (char)(p[n] >> 16);
            text[j++] = (char)(p[n] >> 8);
            text[j++] = (char)p[n];
        }
    }
    CHECK(!memcmp(text, "AROS MIDIHub Pi3", 16) && text[16] == 0);
    CHECK(mh_ump_stream_text(words, 4, MH_UMP_ENDPOINT_NAME, "AROS MIDIHub Pi3", 16) == 0);
}

int main(void)
{
    test_sizes();
    test_scaling();
    test_midi1_to_ump();
    test_ump_to_midi1();
    test_midi2_down();
    test_stream();
    if (failures) {
        fprintf(stderr, "UMP: %d failures\n", failures);
        return EXIT_FAILURE;
    }
    puts("UMP codec OK");
    return EXIT_SUCCESS;
}
