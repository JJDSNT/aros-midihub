#include "ump/ump.h"

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
    struct ump_from_midi1 state;
    size_t i;

    ump_count = 0;
    ump_from_midi1_init(&state, group);
    for (i = 0; i < length; ++i)
        ump_from_midi1_byte(&state, bytes[i], take_ump, NULL);
}

/* How many messages reached the sink, apart from their bytes. */
static unsigned midi_messages;
static struct ump_dropped dropped;
static uint8_t sysex_buffer[64];

static void take_message(void *context, const uint8_t *bytes, size_t length)
{
    ++midi_messages;
    take_midi(context, bytes, length);
}

static void to_midi_sized(const uint32_t *words, unsigned count,
                          uint8_t group, size_t buffer_size)
{
    struct ump_to_midi1 state;
    unsigned i = 0;

    midi_count = 0;
    midi_messages = 0;
    ump_to_midi1_init(&state, group);
    ump_to_midi1_buffer(&state, buffer_size ? sysex_buffer : NULL,
                        buffer_size);
    while (i < count) {
        unsigned n = ump_words(words[i]);
        CHECK(ump_to_midi1(&state, words + i, count - i, take_message,
                           NULL) == 0);
        i += n;
    }
    dropped = state.dropped;
}

static void to_midi(const uint32_t *words, unsigned count, uint8_t group)
{
    to_midi_sized(words, count, group, sizeof(sysex_buffer));
}

static void test_sizes(void)
{
    CHECK(ump_words(0x00000000u) == 1);
    CHECK(ump_words(0x20903c64u) == 1);
    CHECK(ump_words(0x30000000u) == 2);
    CHECK(ump_words(0x40000000u) == 2);
    CHECK(ump_words(0x50000000u) == 4);
    CHECK(ump_words(0xb0000000u) == 3);
    CHECK(ump_words(0xf0000000u) == 4);
    /* Section 2.1.4, table 4, every row: 32, 32, 32, 64, 64, 128, then the
       reserved types 32, 32, 64, 64, 64, 96, 96, then 128, 128, 128. */
    {
        static const unsigned char table4[16] = {
            1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4
        };
        unsigned type;
        for (type = 0; type < 16; ++type)
            CHECK(ump_words((uint32_t)type << 28) == table4[type]);
    }
}

static void test_scaling(void)
{
    /* The UMP specification's own examples: minimum, center and maximum
       keep their places. */
    CHECK(ump_scale_up(0, 7, 16) == 0);
    CHECK(ump_scale_up(64, 7, 16) == 0x8000);
    CHECK(ump_scale_up(127, 7, 16) == 0xffff);
    CHECK(ump_scale_up(0x2000, 14, 32) == 0x80000000u);
    CHECK(ump_scale_up(0x3fff, 14, 32) == 0xffffffffu);
    CHECK(ump_scale_up(127, 7, 32) == 0xffffffffu);
    CHECK(ump_scale_down(0xffff, 16, 7) == 127);
    CHECK(ump_scale_down(0x8000, 16, 7) == 64);
    /* Up and back down is the identity for every 7-bit value. */
    {
        uint32_t v;
        int ok = 1;
        for (v = 0; v < 128; ++v)
            if (ump_scale_down(ump_scale_up(v, 7, 16), 16, 7) != v ||
                ump_scale_down(ump_scale_up(v, 7, 32), 32, 7) != v)
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

/* What the specification's text settles, section by section. */
static void test_against_specification(void)
{
    /* Appendix D.1.3 and D.1.4, the numerical examples. */
    CHECK(ump_scale_up(0x0a, 7, 16) == 0x1400);
    CHECK(ump_scale_up(0x40, 7, 16) == 0x8000);
    CHECK(ump_scale_up(0x57, 7, 16) == 0xaeba);
    CHECK(ump_scale_up(0x7f, 7, 16) == 0xffff);
    CHECK(ump_scale_down(0x1400, 16, 7) == 0x0a);
    CHECK(ump_scale_down(0x8000, 16, 7) == 0x40);
    CHECK(ump_scale_down(0xaeba, 16, 7) == 0x57);
    CHECK(ump_scale_down(0xffff, 16, 7) == 0x7f);
    /* D.1.2: MIDI 1.0 to MIDI 2.0 and back gives the value it started
       from, for every value of both sizes. */
    {
        uint32_t value;
        for (value = 0; value < 128; ++value) {
            CHECK(ump_scale_down(ump_scale_up(value, 7, 16), 16, 7) == value);
            CHECK(ump_scale_down(ump_scale_up(value, 7, 32), 32, 7) == value);
        }
        for (value = 0; value < 16384; ++value)
            if (ump_scale_down(ump_scale_up(value, 14, 32), 32, 14) != value)
                break;
        CHECK(value == 16384);
        CHECK(ump_scale_up(0x2000, 14, 32) == 0x80000000u);
        CHECK(ump_scale_up(0x3fff, 14, 32) == 0xffffffffu);
    }

    /* Section 7.6, table 17: of the system status bytes, F4, F5, F9 and FD
       are reserved, and F0 and F7 are not system messages in a UMP. */
    {
        static const uint8_t defined[] = {
            0xf1, 0xf2, 0xf3, 0xf6, 0xf8, 0xfa, 0xfb, 0xfc, 0xfe, 0xff
        };
        static const uint8_t reserved[] = { 0xf0, 0xf4, 0xf5, 0xf7, 0xf9, 0xfd };
        unsigned i;
        for (i = 0; i < sizeof(defined); ++i) {
            uint32_t word = 0x10000000u | ((uint32_t)defined[i] << 16);
            to_midi(&word, 1, 0);
            CHECK(midi_messages == 1 && midi[0] == defined[i]);
            CHECK(dropped.reserved_status == 0);
        }
        for (i = 0; i < sizeof(reserved); ++i) {
            uint32_t word = 0x10000000u | ((uint32_t)reserved[i] << 16);
            to_midi(&word, 1, 0);
            CHECK(midi_messages == 0 && dropped.reserved_status == 1);
        }
        /* and the other way: the reserved bytes make no UMP */
        {
            static const uint8_t stream[] = {
                0xf4, 0xf5, 0xf9, 0xfd, 0xf8, 0xf6
            };
            to_ump(stream, sizeof(stream), 0);
            CHECK(ump_count == 2 && ump[0] == 0x10f80000u &&
                  ump[1] == 0x10f60000u);
        }
    }

    /* Appendix D.2.7: a SysEx message comes out whole, once. */
    {
        static const uint32_t sysex[] = {
            0x30160102u, 0x03040506u, 0x30260708u, 0x090a0b0cu,
            0x30310d00u, 0
        };
        to_midi(sysex, 6, 0);
        CHECK(midi_messages == 1 && midi_count == 15 && midi[0] == 0xf0 &&
              midi[14] == 0xf7);
        /* nowhere to gather it, or too little room: dropped and counted,
           and the next one that fits is taken */
        to_midi_sized(sysex, 6, 0, 0);
        CHECK(midi_messages == 0 && dropped.sysex_too_long == 1);
        to_midi_sized(sysex, 6, 0, 14);
        CHECK(midi_messages == 0 && dropped.sysex_too_long == 1);
        to_midi_sized(sysex, 6, 0, 15);
        CHECK(midi_messages == 1 && dropped.sysex_too_long == 0);
        {
            static const uint32_t two[] = {
                0x30160102u, 0x03040506u, 0x30260708u, 0x090a0b0cu,
                0x30310d00u, 0, 0x30027d01u, 0
            };
            static const uint8_t second[] = { 0xf0, 0x7d, 0x01, 0xf7 };
            to_midi_sized(two, 8, 0, 8);
            CHECK(midi_messages == 1 && dropped.sysex_too_long == 1 &&
                  midi_count == 4 && !memcmp(midi, second, 4));
        }
    }

    /* Section 7.7.1: inside a SysEx message of a group, only its further
       pieces and System Real Time may come; anything else of the group
       ends it, and what was gathered is not a message. */
    {
        static const uint32_t with_clock[] = {
            0x30160102u, 0x03040506u, 0x10f80000u, 0x30310700u, 0
        };
        static const uint32_t with_note[] = {
            0x30160102u, 0x03040506u, 0x20903c64u, 0x30310700u, 0
        };
        static const uint32_t with_start[] = {
            0x30160102u, 0x03040506u, 0x30110900u, 0, 0x30310a00u, 0
        };
        static const uint32_t with_song[] = {
            0x30160102u, 0x03040506u, 0x10f30500u, 0x30310700u, 0
        };
        /* another group's message and a groupless one do not end it */
        static const uint32_t with_others[] = {
            0x30160102u, 0x03040506u, 0x21903c64u, 0x00000000u,
            0x30310700u, 0
        };
        static const uint8_t whole[] = { 0xf0, 1, 2, 3, 4, 5, 6, 7, 0xf7 };
        static const uint8_t restarted[] = { 0xf0, 9, 10, 0xf7 };

        to_midi(with_clock, 5, 0);
        CHECK(midi_messages == 2 && midi[0] == 0xf8 &&
              midi_count == 1 + sizeof(whole) &&
              !memcmp(midi + 1, whole, sizeof(whole)));
        CHECK(dropped.sysex_broken == 0);

        to_midi(with_note, 5, 0);
        CHECK(midi_messages == 1 && midi_count == 3 && midi[0] == 0x90);
        /* the message that was cut, and its end arriving without a start */
        CHECK(dropped.sysex_broken == 2);

        to_midi(with_start, 6, 0);
        CHECK(midi_messages == 1 && midi_count == sizeof(restarted) &&
              !memcmp(midi, restarted, sizeof(restarted)));
        CHECK(dropped.sysex_broken == 1);

        to_midi(with_song, 5, 0);
        CHECK(midi_messages == 1 && midi_count == 2 && midi[0] == 0xf3);
        CHECK(dropped.sysex_broken == 2);

        to_midi(with_others, 6, 0);
        CHECK(midi_messages == 1 && midi_count == sizeof(whole) &&
              !memcmp(midi, whole, sizeof(whole)));
        CHECK(dropped.sysex_broken == 0 && dropped.other_group == 1 &&
              dropped.not_translated == 1);
    }

    /* Appendix D.2.8 and D.2.9: what has no MIDI 1.0 form is counted. */
    {
        static const uint32_t per_note[] = { 0x40003c00u, 0x12345678u };
        static const uint32_t relative[] = { 0x40400001u, 0x00000001u };
        static const uint32_t per_note_bend[] = { 0x40603c00u, 0x80000000u };
        static const uint32_t management[] = { 0x40f03c03u, 0 };
        static const uint32_t sysex8[] = { 0x50000000u, 0, 0, 0 };
        static const uint32_t flex[] = { 0xd0100000u, 0, 0, 0 };
        to_midi(per_note, 2, 0);
        CHECK(midi_messages == 0 && dropped.no_midi1_form == 1);
        to_midi(relative, 2, 0);
        CHECK(midi_messages == 0 && dropped.no_midi1_form == 1);
        to_midi(per_note_bend, 2, 0);
        CHECK(midi_messages == 0 && dropped.no_midi1_form == 1);
        to_midi(management, 2, 0);
        CHECK(midi_messages == 0 && dropped.no_midi1_form == 1);
        to_midi(sysex8, 4, 0);
        CHECK(midi_messages == 0 && dropped.not_translated == 1);
        to_midi(flex, 4, 0);
        CHECK(midi_messages == 0 && dropped.not_translated == 1);
    }

    /* Appendix D.2.4 with figure 64: the bank, most significant byte
       first, then the program; without Bank Valid the program alone. */
    {
        static const uint32_t with_bank[] = { 0x40c20001u, 0x11000305u };
        static const uint32_t without[] = { 0x40c20000u, 0x11000305u };
        static const uint8_t three[] = {
            0xb2, 0, 3, 0xb2, 32, 5, 0xc2, 0x11
        };
        to_midi(with_bank, 2, 0);
        CHECK(midi_messages == 3 && midi_count == sizeof(three) &&
              !memcmp(midi, three, sizeof(three)));
        to_midi(without, 2, 0);
        CHECK(midi_messages == 1 && midi_count == 2 && midi[0] == 0xc2 &&
              midi[1] == 0x11);
    }

    /* Appendix D.2.3 with figure 97: four messages, the bank and the
       index, then the top fourteen bits of the data. */
    {
        /* RPN 0/0, pitch bend range: 12 semitones and 50 cents (7.4.7.1) */
        static const uint32_t range[] = {
            0x40200000u, (12u << 25) | (50u << 18)
        };
        static const uint32_t assignable[] = {
            0x40310203u, (100u << 25) | (1u << 18)
        };
        static const uint8_t rpn[] = {
            0xb0, 101, 0, 0xb0, 100, 0, 0xb0, 6, 12, 0xb0, 38, 50
        };
        static const uint8_t nrpn[] = {
            0xb1, 99, 2, 0xb1, 98, 3, 0xb1, 6, 100, 0xb1, 38, 1
        };
        to_midi(range, 2, 0);
        CHECK(midi_messages == 4 && !memcmp(midi, rpn, sizeof(rpn)));
        to_midi(assignable, 2, 0);
        CHECK(midi_messages == 4 && !memcmp(midi, nrpn, sizeof(nrpn)));
    }

    /* Sections 7.1.4 and 7.1.5: the lengths and the characters allowed. */
    {
        uint32_t words[4 * 8];
        char text[100];
        memset(text, 'a', sizeof(text));
        CHECK(ump_stream_text(words, 32, UMP_ENDPOINT_NAME, text, 98) == 28);
        CHECK(ump_stream_text(words, 32, UMP_ENDPOINT_NAME, text, 99) == 0);
        CHECK(ump_stream_text(words, 32, UMP_PRODUCT_INSTANCE_ID, text,
                              42) == 12);
        CHECK(ump_stream_text(words, 32, UMP_PRODUCT_INSTANCE_ID, text,
                              43) == 0);
        text[3] = 0x7f;
        CHECK(ump_stream_text(words, 32, UMP_PRODUCT_INSTANCE_ID, text,
                              10) == 0);
        text[3] = 0x1f;
        CHECK(ump_stream_text(words, 32, UMP_PRODUCT_INSTANCE_ID, text,
                              10) == 0);
        /* a name may be any UTF-8 */
        text[3] = (char)0xc3;
        CHECK(ump_stream_text(words, 32, UMP_ENDPOINT_NAME, text, 10) == 4);
    }
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

    ump_endpoint_info(words, UMP_PROTOCOL_MIDI1);
    CHECK(words[0] == 0xf0010101u);
    CHECK(words[1] == 0x80000100u);
    CHECK(ump_stream_status(words) == UMP_ENDPOINT_INFO);

    ump_stream_config(words, UMP_PROTOCOL_MIDI1);
    CHECK(words[0] == 0xf0060100u);

    /* "AROS MIDIHub Pi3" is 16 bytes: a Start and an End packet */
    n = ump_stream_text(words, 16, UMP_ENDPOINT_NAME, "AROS MIDIHub Pi3", 16);
    CHECK(n == 8);
    CHECK((words[0] >> 26) == ((0xfu << 2) | 1));   /* Start */
    CHECK((words[4] >> 26) == ((0xfu << 2) | 3));   /* End */
    CHECK(ump_stream_status(words + 4) == UMP_ENDPOINT_NAME);
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
    CHECK(ump_stream_text(words, 4, UMP_ENDPOINT_NAME, "AROS MIDIHub Pi3", 16) == 0);
}

int main(void)
{
    test_sizes();
    test_scaling();
    test_midi1_to_ump();
    test_ump_to_midi1();
    test_midi2_down();
    test_against_specification();
    test_stream();
    if (failures) {
        fprintf(stderr, "UMP: %d failures\n", failures);
        return EXIT_FAILURE;
    }
    puts("UMP codec OK");
    return EXIT_SUCCESS;
}
