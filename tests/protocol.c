#include "midihub/applemidi.h"
#include "midihub/rtpmidi.h"
#include "midihub/session.h"
#include "midihub/sender.h"
#include "midihub/timing.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void apple_exchange(void)
{
    static const uint8_t invitation[] = {
        0xff, 0xff, 'I', 'N', 0, 0, 0, 2,
        0x12, 0x34, 0x56, 0x78, 0x89, 0xab, 0xcd, 0xef,
        'A', 'R', 'O', 'S', 0
    };
    struct mh_apple_packet packet;
    uint8_t encoded[64];
    size_t length;
    size_t i;

    assert(mh_apple_decode(invitation, sizeof(invitation), &packet) == 0);
    assert(packet.command == MH_APPLE_IN);
    assert(packet.token == 0x12345678 && packet.ssrc == 0x89abcdef);
    assert(packet.name_length == 4 && !memcmp(packet.name, "AROS", 4));
    assert(mh_apple_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(invitation));
    assert(!memcmp(encoded, invitation, length));
    for (i = 0; i < 16; ++i)
        assert(mh_apple_decode(invitation, i, &packet) == -1);
    assert(mh_apple_decode(invitation, 16, &packet) == 0);
    for (i = 17; i < sizeof(invitation); ++i)
        assert(mh_apple_decode(invitation, i, &packet) == -1);
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[7] = 3;
    assert(mh_apple_decode(encoded, sizeof(invitation), &packet) == -1);
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[sizeof(invitation) - 1] = 'X';
    assert(mh_apple_decode(encoded, sizeof(invitation), &packet) == -1);
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[18] = 0;
    assert(mh_apple_decode(encoded, sizeof(invitation), &packet) == -1);
}

static void apple_sync(void)
{
    static const uint8_t sync[] = {
        0xff, 0xff, 'C', 'K', 0x89, 0xab, 0xcd, 0xef,
        2, 0, 0, 0,
        0, 0, 0, 1, 0, 0, 0, 2,
        0, 0, 0, 3, 0, 0, 0, 4,
        0, 0, 0, 5, 0, 0, 0, 6
    };
    struct mh_apple_packet packet;
    uint8_t encoded[36];
    size_t length;

    assert(mh_apple_decode(sync, sizeof(sync), &packet) == 0);
    assert(packet.command == MH_APPLE_CK && packet.sync_count == 2);
    assert(packet.timestamps[0] == UINT64_C(0x0000000100000002));
    assert(packet.timestamps[2] == UINT64_C(0x0000000500000006));
    assert(mh_apple_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(sync) && !memcmp(encoded, sync, length));
    assert(mh_apple_decode(sync, sizeof(sync) - 1, &packet) == -1);
    encoded[8] = 3;
    assert(mh_apple_decode(encoded, sizeof(encoded), &packet) == -1);
}

static void apple_feedback(void)
{
    static const uint8_t feedback[] = {
        0xff, 0xff, 'R', 'S', 0x89, 0xab, 0xcd, 0xef,
        0x00, 0x01, 0x00, 0x02
    };
    struct mh_apple_packet packet;
    uint8_t encoded[12];
    size_t length;

    assert(mh_apple_decode(feedback, sizeof(feedback), &packet) == 0);
    assert(packet.command == MH_APPLE_RS && packet.ssrc == 0x89abcdef);
    assert(packet.feedback_sequence == 0x00010002);
    assert(mh_apple_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(feedback) && !memcmp(encoded, feedback, length));
    assert(mh_apple_decode(feedback, sizeof(feedback) - 1, &packet) == -1);
    assert(mh_apple_encode(&packet, encoded, sizeof(encoded) - 1,
                           &length) == -1);
}

static void rtp_short(void)
{
    static const uint8_t note[] = {0x90, 60, 100};
    static const uint8_t program[] = {0xc2, 10};
    static const uint8_t clock[] = {0xf8};
    static const uint8_t invalid_data[] = {0x90, 0x80, 100};
    static const uint8_t expected[] = {
        0x80, 0xe1, 0x12, 0x34, 0x01, 0x02, 0x03, 0x04,
        0x89, 0xab, 0xcd, 0xef, 3, 0x90, 60, 100
    };
    struct mh_rtp_packet packet;
    uint8_t encoded[32];
    size_t length;
    size_t i;

    assert(mh_rtp_encode_short(0x1234, 0x01020304, 0x89abcdef,
                               note, sizeof(note), encoded,
                               sizeof(encoded), &length) == 0);
    assert(length == sizeof(expected) && !memcmp(encoded, expected, length));
    assert(mh_rtp_decode(expected, sizeof(expected), &packet) == 0);
    assert(packet.sequence == 0x1234 && packet.timestamp == 0x01020304);
    assert(packet.ssrc == 0x89abcdef && packet.midi_length == 3);
    assert(!memcmp(packet.midi, note, sizeof(note)));
    for (i = 0; i < sizeof(expected); ++i)
        assert(mh_rtp_decode(expected, i, &packet) == -1);
    assert(mh_rtp_encode_short(0, 0, 0, note, 0, encoded,
                               sizeof(encoded), &length) == -1);
    assert(mh_rtp_encode_short(0, 0, 0, program, sizeof(program), encoded,
                               sizeof(encoded), &length) == 0 && length == 15);
    assert(mh_rtp_encode_short(0, 0, 0, clock, sizeof(clock), encoded,
                               sizeof(encoded), &length) == 0 && length == 14);
    assert(mh_rtp_encode_short(0, 0, 0, invalid_data,
                               sizeof(invalid_data), encoded,
                               sizeof(encoded), &length) == -1);
    memcpy(encoded, expected, sizeof(expected));
    encoded[0] = 0x81;
    assert(mh_rtp_decode(encoded, sizeof(expected), &packet) == -1);
}

static void rtp_journal_and_lengths(void)
{
    static const uint8_t with_journal[] = {
        0x80, 0xe1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 2,
        0xc0, 3, 0x90, 60, 100, 0, 0
    };
    struct mh_rtp_packet packet;
    uint8_t broken[sizeof(with_journal)];

    assert(mh_rtp_decode(with_journal, sizeof(with_journal), &packet) == 0);
    assert(packet.midi_length == 3 && packet.journal_length == 2);
    assert(packet.journal == with_journal + 17);
    memcpy(broken, with_journal, sizeof(broken));
    broken[12] = 0xc0;
    broken[13] = 20;
    assert(mh_rtp_decode(broken, sizeof(broken), &packet) == -1);
    broken[12] = 0x00;
    assert(mh_rtp_decode(broken, sizeof(broken), &packet) == -1);
}

static void journal_framing(void)
{
    static const uint8_t empty[] = {0x80, 0x12, 0x34};
    static const uint8_t sections[] = {
        0xe1, 0x12, 0x34,
        0x00, 0x02,
        0x80, 0x03, 0x10,
        0x94, 0x03, 0x20
    };
    uint8_t broken[sizeof(sections)];
    struct mh_journal journal;

    assert(mh_journal_decode(empty, sizeof(empty), &journal) == 0);
    assert(journal.checkpoint == 0x1234 &&
           journal.single_packet_safe && !journal.channel_count &&
           !journal.system);
    assert(mh_journal_decode(sections, sizeof(sections), &journal) == 0);
    assert(journal.system == sections + 3 && journal.system_length == 2);
    assert(journal.channel_count == 2);
    assert(journal.channels[0].number == 0 &&
           journal.channels[0].chapters == 0x10 &&
           journal.channels[0].length == 3);
    assert(journal.channels[1].number == 5 &&
           journal.channels[1].chapters == 0x20);
    assert(mh_journal_decode(empty, 2, &journal) == -1);
    assert(mh_journal_decode(sections, sizeof(sections) - 1,
                             &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[4] = 12;
    assert(mh_journal_decode(broken, sizeof(broken), &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[6] = 2;
    assert(mh_journal_decode(broken, sizeof(broken), &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[8] = 0x80; /* Duplicate channel number. */
    assert(mh_journal_decode(broken, sizeof(broken), &journal) == -1);
    assert(mh_journal_decode(empty, sizeof(empty) - 1, &journal) == -1);
    assert(mh_journal_covers_gap(10, 13, 11));
    assert(mh_journal_covers_gap(10, 13, 9));
    assert(!mh_journal_covers_gap(10, 13, 12));
    assert(!mh_journal_covers_gap(10, 13, 13));
    assert(mh_journal_covers_gap(0xfffe, 1, 0xffff));
    assert(!mh_journal_covers_gap(0xfffe, 1, 0));
    assert(!mh_journal_covers_gap(10, 11, 10));
}

static void journal_note_offs(void)
{
    static const uint8_t note_only[] = {
        0x20, 0, 4, 0x00, 0x06, 0x08, 0x00, 0x77, 0x08
    };
    static const uint8_t preceded[] = {
        0x20, 0, 4, 0x00, 0x10, 0xf8,
        0x80, 1, 0,
        0x00, 64, 127,
        0x00, 0x02,
        0x80, 0x00,
        0x80, 0x77, 0x08
    };
    static const uint8_t note_on[] = {
        0x20, 0, 4, 0x00, 0x07, 0x08, 0x01, 0xf1, 0x3c, 0xe4
    };
    static const uint8_t duplicate_notes[] = {
        0x20, 0, 4, 0x00, 0x09, 0x08, 0x02, 0xf1,
        0x3c, 0xe4, 0x3c, 0x64
    };
    struct mh_journal journal;
    struct mh_journal_notes notes;

    assert(mh_journal_decode(note_only, sizeof(note_only), &journal) == 0);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(!notes.offbits_single_packet_safe &&
           notes.offbits[7] == 0x08 && notes.offbits[6] == 0);
    assert(mh_journal_decode(preceded, sizeof(preceded), &journal) == 0);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.offbits_single_packet_safe && notes.offbits[7] == 0x08);
    journal.channels[0].length--;
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == -1);
    assert(mh_journal_decode(note_on, sizeof(note_on), &journal) == 0);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.log_count == 1 && notes.logs[0].number == 60 &&
           notes.logs[0].velocity == 100 && notes.logs[0].simultaneous &&
           !notes.logs[0].single_packet_safe);
    assert(notes.offbits[7] == 0);
    assert(mh_journal_decode(duplicate_notes, sizeof(duplicate_notes),
                             &journal) == 0);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == -1);
}

static void journal_channel_state(void)
{
    static const uint8_t state_bytes[] = {
        0x20, 0, 4, 0x00, 0x08, 0x90,
        0x05, 0x82, 0x03, 0x01, 0x20
    };
    struct mh_journal journal;
    struct mh_journal_channel_state state;

    assert(mh_journal_decode(state_bytes, sizeof(state_bytes),
                             &journal) == 0);
    assert(mh_journal_decode_channel_state(&journal.channels[0],
                                            &state) == 1);
    assert(state.has_program && state.program == 5 && state.has_bank &&
           state.bank_msb == 2 && state.bank_lsb == 3 &&
           !state.program_single_packet_safe);
    assert(state.has_pitch && state.pitch_lsb == 1 &&
           state.pitch_msb == 32 && !state.pitch_single_packet_safe);
    journal.channels[0].length--;
    assert(mh_journal_decode_channel_state(&journal.channels[0],
                                            &state) == -1);
}

static void journal_controls(void)
{
    static const uint8_t controls_bytes[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x07, 0x64, 0xc0, 0x81
    };
    static const uint8_t multiple_tools[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x40, 0xc1, 0x40, 0x81
    };
    static const uint8_t repeated_tool[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x07, 0x64, 0x07, 0x65
    };
    struct mh_journal journal;
    struct mh_journal_controls controls;

    assert(mh_journal_decode(controls_bytes, sizeof(controls_bytes),
                             &journal) == 0);
    assert(mh_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.count == 2 && controls.logs[0].number == 7 &&
           controls.logs[0].value == 100 && !controls.logs[0].alternate);
    assert(controls.logs[1].number == 64 && controls.logs[1].alternate &&
           !controls.logs[1].count_tool && controls.logs[1].value == 1 &&
           controls.logs[1].single_packet_safe);
    journal.channels[0].length--;
    assert(mh_journal_decode_controls(&journal.channels[0],
                                       &controls) == -1);
    assert(mh_journal_decode(multiple_tools, sizeof(multiple_tools),
                             &journal) == 0);
    assert(mh_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.logs[0].count_tool && controls.logs[0].value == 1 &&
           controls.logs[1].alternate && !controls.logs[1].count_tool);
    assert(mh_journal_decode(repeated_tool, sizeof(repeated_tool),
                             &journal) == 0);
    assert(mh_journal_decode_controls(&journal.channels[0],
                                       &controls) == -1);
}

static void outgoing_journal(void)
{
    const uint8_t on[] = {0x90, 60, 100};
    const uint8_t off[] = {0x80, 60, 0};
    const uint8_t bank[] = {0xb0, 0, 2};
    const uint8_t program[] = {0xc0, 5};
    const uint8_t pitch[] = {0xe0, 1, 32};
    const uint8_t volume[] = {0xb0, 7, 90};
    const uint8_t channel_pressure[] = {0xd0, 40};
    const uint8_t poly_pressure[] = {0xa0, 60, 50};
    const uint8_t reset_controllers[] = {0xb0, 121, 0};
    struct mh_sender sender;
    struct mh_journal journal;
    struct mh_journal_notes notes;
    struct mh_journal_channel_state channel_state;
    struct mh_journal_controls controls;
    struct mh_journal_aftertouch aftertouch;
    uint8_t bytes[256];
    size_t length;

    mh_sender_reset(&sender);
    assert(mh_sender_journal(&sender, 100, 1000,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 3 && bytes[0] == 0x80 && bytes[2] == 100);
    assert(mh_rtp_encode_list(100, 1000, 7, NULL, 0,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 13 && mh_rtp_decode(bytes, length, &(struct mh_rtp_packet){0}) == 0);
    mh_sender_record(&sender, 100, 1000, on, sizeof(on));
    assert(mh_sender_journal(&sender, 101, 1050,
                              bytes, sizeof(bytes), &length) == 0);
    assert(mh_journal_decode(bytes, length, &journal) == 0);
    assert(journal.checkpoint == 100 && !journal.single_packet_safe &&
           journal.channel_count == 1);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.log_count == 1 && notes.logs[0].number == 60 &&
           notes.logs[0].velocity == 100 && notes.logs[0].simultaneous &&
           !notes.logs[0].single_packet_safe);
    mh_sender_record(&sender, 101, 1050, off, sizeof(off));
    assert(mh_sender_journal(&sender, 102, 1100,
                              bytes, sizeof(bytes), &length) == 0);
    assert(mh_journal_decode(bytes, length, &journal) == 0);
    assert(mh_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.offbits[7] == 0x08 && notes.log_count == 0 &&
           !notes.offbits_single_packet_safe);
    mh_sender_ack(&sender, 100);
    assert(sender.count == 1 && sender.events[0].sequence == 101);
    mh_sender_ack(&sender, 101);
    assert(sender.count == 0);
    assert(mh_sender_journal(&sender, 102, 1100,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 3 && bytes[2] == 102);

    mh_sender_record(&sender, 102, 1100, bank, sizeof(bank));
    mh_sender_record(&sender, 103, 1200, program, sizeof(program));
    mh_sender_record(&sender, 104, 1300, pitch, sizeof(pitch));
    mh_sender_record(&sender, 105, 1400, volume, sizeof(volume));
    assert(mh_sender_journal(&sender, 106, 1500,
                              bytes, sizeof(bytes), &length) == 0);
    assert(mh_journal_decode(bytes, length, &journal) == 0);
    assert(journal.checkpoint == 102 && journal.channel_count == 1);
    assert(mh_journal_decode_channel_state(&journal.channels[0],
                                            &channel_state) == 1);
    assert(channel_state.has_program && channel_state.program == 5 &&
           channel_state.has_bank && channel_state.bank_msb == 2 &&
           channel_state.has_pitch && channel_state.pitch_lsb == 1);
    assert(mh_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.count == 2);
    mh_sender_ack(&sender, 104);
    assert(sender.count == 1 && sender.events[0].sequence == 105);
    mh_sender_clear_history(&sender);
    assert(sender.count == 0);
    mh_sender_reset(&sender);
    mh_sender_record(&sender, 200, 2000, on, sizeof(on));
    mh_sender_record(&sender, 201, 2010, channel_pressure,
                      sizeof(channel_pressure));
    mh_sender_record(&sender, 202, 2020, poly_pressure,
                      sizeof(poly_pressure));
    assert(mh_sender_journal(&sender, 203, 2030,
                              bytes, sizeof(bytes), &length) == 0);
    assert(mh_journal_decode(bytes, length, &journal) == 0);
    assert(journal.channels[0].chapters == 0x0b);
    assert(mh_journal_decode_aftertouch(&journal.channels[0],
                                        &aftertouch) == 1);
    assert(aftertouch.has_channel_pressure &&
           aftertouch.channel_pressure == 40 &&
           aftertouch.poly_count == 1 &&
           aftertouch.poly[0].number == 60 &&
           aftertouch.poly[0].pressure == 50);
    mh_sender_record(&sender, 203, 2030, reset_controllers,
                      sizeof(reset_controllers));
    assert(mh_sender_journal(&sender, 204, 2040,
                              bytes, sizeof(bytes), &length) == 0);
    assert(mh_journal_decode(bytes, length, &journal) == 0);
    assert(mh_journal_decode_aftertouch(&journal.channels[0],
                                        &aftertouch) == 0);
}

static void rtp_command_reader(void)
{
    static const uint8_t midi[] = {
        0x90, 60, 100, 0x00, 61, 101, 0x81, 0x00, 0x80, 60, 64
    };
    struct mh_rtp_packet packet = {0};
    struct mh_rtp_reader reader;
    struct mh_midi_event event;

    packet.timestamp = 100;
    packet.midi = midi;
    packet.midi_length = sizeof(midi);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 100 && event.length == 3);
    assert(event.bytes[0] == 0x90 && event.bytes[1] == 60 &&
           event.bytes[2] == 100);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 100 && event.bytes[0] == 0x90 &&
           event.bytes[1] == 61 && event.bytes[2] == 101);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 228 && event.bytes[0] == 0x80 &&
           event.bytes[1] == 60 && event.bytes[2] == 64);
    assert(mh_rtp_reader_next(&reader, &event) == 0);

    packet.z = 1;
    packet.midi = (const uint8_t *)"\x01\xc0\x22";
    packet.midi_length = 3;
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 101 && event.length == 2 &&
           event.bytes[0] == 0xc0 && event.bytes[1] == 0x22);

    packet.z = 0;
    packet.midi = (const uint8_t *)"\x40\x22";
    packet.midi_length = 2;
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == -1);
    packet.midi = (const uint8_t *)"\xf0";
    packet.midi_length = 1;
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == -1);
}

static void rtp_system_common(void)
{
    static const uint8_t midi[] = {
        0xf1, 0x7f, 0, 0xf2, 1, 2, 0, 0xf3, 4, 0, 0xf6
    };
    static const uint8_t broken_running_status[] = {
        0x90, 60, 100, 0, 0xf1, 1, 0, 61, 101
    };
    struct mh_rtp_packet packet = {0};
    struct mh_rtp_reader reader;
    struct mh_midi_event event;

    packet.midi = midi;
    packet.midi_length = sizeof(midi);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1 &&
           event.length == 2 && event.bytes[0] == 0xf1);
    assert(mh_rtp_reader_next(&reader, &event) == 1 &&
           event.length == 3 && event.bytes[0] == 0xf2);
    assert(mh_rtp_reader_next(&reader, &event) == 1 &&
           event.length == 2 && event.bytes[0] == 0xf3);
    assert(mh_rtp_reader_next(&reader, &event) == 1 &&
           event.length == 1 && event.bytes[0] == 0xf6);
    assert(mh_rtp_reader_next(&reader, &event) == 0);

    packet.midi = broken_running_status;
    packet.midi_length = sizeof(broken_running_status);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(mh_rtp_reader_next(&reader, &event) == -1);
}

static void rtp_sysex(void)
{
    static const uint8_t complete[] = {0xf0, 0x7d, 1, 2, 0xf7};
static const uint8_t first[] = {0xf0, 0x7d, 1, 0xf0};
    static const uint8_t middle[] = {0xf7, 2, 0xf0};
    static const uint8_t last[] = {0xf7, 2, 0xf7};
    struct mh_rtp_packet packet;
    struct mh_rtp_reader reader;
    struct mh_midi_event event;
    struct mh_sysex_assembler assembler;
    const uint8_t *message;
    size_t message_length;
    uint8_t wire[32];
    size_t wire_length;

    mh_sysex_reset(&assembler);
    assert(mh_rtp_encode_list(1, 100, 7, complete, sizeof(complete),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(mh_rtp_decode(wire, wire_length, &packet) == 0);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1 &&
           event.sysex_length == sizeof(complete));
    assert(mh_sysex_feed(&assembler, &event, 1, &message,
                          &message_length) == 1);
    assert(message_length == sizeof(complete) &&
           !memcmp(message, complete, message_length));

    assert(mh_rtp_encode_list(2, 101, 7, first, sizeof(first),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(mh_rtp_decode(wire, wire_length, &packet) == 0);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(mh_sysex_feed(&assembler, &event, 2, &message,
                          &message_length) == 0);
    assert(mh_rtp_encode_list(3, 102, 7, last, sizeof(last),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(mh_rtp_decode(wire, wire_length, &packet) == 0);
    mh_rtp_reader_init(&reader, &packet);
    assert(mh_rtp_reader_next(&reader, &event) == 1);
    assert(mh_sysex_feed(&assembler, &event, 3, &message,
                          &message_length) == 1);
    assert(message_length == sizeof(complete) &&
           !memcmp(message, complete, message_length));

    mh_sysex_reset(&assembler);
    event.sysex = first;
    event.sysex_length = sizeof(first);
    assert(mh_sysex_feed(&assembler, &event, 7, &message,
                          &message_length) == 0);
    event.sysex = last;
    event.sysex_length = sizeof(last);
    assert(mh_sysex_feed(&assembler, &event, 9, &message,
                          &message_length) == -1);

    mh_sysex_reset(&assembler);
    event.sysex = first;
    event.sysex_length = sizeof(first);
    assert(mh_sysex_feed(&assembler, &event, 10, &message,
                          &message_length) == 0);
    event.sysex = middle;
    event.sysex_length = sizeof(middle);
    assert(mh_sysex_feed(&assembler, &event, 11, &message,
                          &message_length) == 0);
    event.sysex = last;
    event.sysex_length = sizeof(last);
    assert(mh_sysex_feed(&assembler, &event, 12, &message,
                          &message_length) == 1);
    assert(message_length == 6 && message[0] == 0xf0 &&
           message[5] == 0xf7);
}

static void session_handshake(void)
{
    struct mh_session initiator;
    struct mh_session responder;
    struct mh_apple_packet packet;
    struct mh_apple_packet response;
    struct mh_apple_packet ignored;
    int data_port;

    assert(mh_session_init(&initiator, 0x11111111,
                           (const uint8_t *)"AROS", 4) == 0);
    assert(mh_session_init(&responder, 0x22222222,
                           (const uint8_t *)"Peer", 4) == 0);
    assert(mh_session_invite(&initiator, 0x33333333, &packet) == 0);
    assert(packet.command == MH_APPLE_IN);
    assert(mh_session_retry(&initiator, &ignored, &data_port) == 0);
    assert(ignored.command == MH_APPLE_IN && !data_port);
    assert(mh_session_receive(&responder, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == MH_SESSION_IDLE);
    assert(mh_session_receive(&responder, 0, &packet, &response,
                              &data_port) == 1);
    assert(response.command == MH_APPLE_OK && !data_port);
    assert(responder.phase == MH_SESSION_WAITING_DATA);
    assert(mh_session_receive(&responder, 0, &packet, &ignored,
                              &data_port) == 1);
    assert(ignored.command == MH_APPLE_OK && !data_port);
    packet = response;
    assert(mh_session_receive(&initiator, 0, &packet, &response,
                              &data_port) == 1);
    assert(response.command == MH_APPLE_IN && data_port);
    assert(initiator.phase == MH_SESSION_INVITING_DATA);
    assert(mh_session_retry(&initiator, &ignored, &data_port) == 0);
    assert(ignored.command == MH_APPLE_IN && data_port);
    packet = response;
    packet.token++;
    assert(mh_session_receive(&responder, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == MH_SESSION_WAITING_DATA);
    packet.token--;
    assert(mh_session_receive(&responder, 1, &packet, &response,
                              &data_port) == 1);
    assert(response.command == MH_APPLE_OK && data_port);
    assert(responder.phase == MH_SESSION_CONNECTED);
    packet = response;
    assert(mh_session_receive(&initiator, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(initiator.phase == MH_SESSION_CONNECTED);
    assert(mh_session_end(&initiator, &packet) == 0);
    assert(packet.command == MH_APPLE_BY);
    assert(mh_session_receive(&responder, 0, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == MH_SESSION_IDLE);
}

static void session_rejection(void)
{
    struct mh_session initiator;
    struct mh_apple_packet packet;
    struct mh_apple_packet reply;
    int data_port;

    assert(mh_session_init(&initiator, 1, 0, 0) == 0);
    assert(mh_session_invite(&initiator, 2, &packet) == 0);
    reply = packet;
    reply.command = MH_APPLE_NO;
    reply.token = 3;
    assert(mh_session_receive(&initiator, 0, &reply, &packet,
                              &data_port) == 0);
    assert(initiator.phase == MH_SESSION_INVITING_CONTROL);
    reply.token = 2;
    assert(mh_session_receive(&initiator, 0, &reply, &packet,
                              &data_port) == 0);
    assert(initiator.phase == MH_SESSION_IDLE);
}

static void clock_and_queue(void)
{
    const uint8_t first[] = {0x90, 60, 100};
    const uint8_t second[] = {0x80, 60, 0};
    uint64_t stamps[3] = {1000, 800, 1020};
    uint64_t due;
    int64_t offset;
    struct mh_event_queue queue = {0};
    struct mh_queued_event event;

    assert(mh_clock_offset(stamps, 1, &offset) == 0 && offset == 210);
    assert(mh_clock_due(800, 1010, offset, &due) == 0 && due == 1010);
    assert(mh_clock_due(900, 1010, offset, &due) == 0 && due == 1110);
    assert(mh_clock_offset(stamps, 0, &offset) == 0 && offset == -210);
    assert(mh_clock_due(1010, 800, offset, &due) == 0 && due == 800);
    assert(mh_clock_due(1110, 800, offset, &due) == 0 && due == 900);
    stamps[2] = 999;
    assert(mh_clock_offset(stamps, 1, &offset) == -1);

    assert(mh_clock_due(0x64, UINT64_C(0x100000000), 0, &due) == 0 &&
           due == UINT64_C(0x100000064));
    assert(mh_clock_due(0xffffff9c, UINT64_C(0x100000000), 0,
                        &due) == 0 && due == UINT64_C(0xffffff9c));
    assert(mh_queue_push(&queue, 300, first, sizeof(first)) == 0);
    assert(mh_queue_push(&queue, 200, second, sizeof(second)) == 0);
    assert(mh_queue_push(&queue, 300, second, sizeof(second)) == 0);
    assert(mh_queue_next_due(&queue, &due) == 1 && due == 200);
    assert(mh_queue_pop_due(&queue, 199, &event) == 0);
    assert(mh_queue_pop_due(&queue, 200, &event) == 1 &&
           !memcmp(event.bytes, second, sizeof(second)));
    assert(mh_queue_pop_due(&queue, 300, &event) == 1 &&
           !memcmp(event.bytes, first, sizeof(first)));
    assert(mh_queue_pop_due(&queue, 300, &event) == 1 &&
           !memcmp(event.bytes, second, sizeof(second)));
    assert(mh_queue_next_due(&queue, &due) == 0);
    assert(mh_queue_push(&queue, 300, first, sizeof(first)) == 0);
    assert(mh_queue_push(&queue, 200, second, sizeof(second)) == 0);
    assert(mh_queue_cancel_note_on(&queue, 0, 60) == 1);
    assert(!mh_queue_has_note_on(&queue, 0, 60));
    assert(mh_queue_next_due(&queue, &due) == 1 && due == 200);
    assert(mh_queue_cancel_note_on(&queue, 0, 60) == 0);
    assert(mh_queue_push(&queue, 400, first, sizeof(first)) == 0);
    assert(mh_queue_has_note_on(&queue, 0, 60));
    mh_queue_reset(&queue);
    assert(mh_queue_pop_due(&queue, 400, &event) == 0);
}

int main(void)
{
    apple_exchange();
    apple_sync();
    apple_feedback();
    rtp_short();
    rtp_journal_and_lengths();
    journal_framing();
    journal_note_offs();
    journal_channel_state();
    journal_controls();
    outgoing_journal();
    rtp_command_reader();
    rtp_system_common();
    rtp_sysex();
    session_handshake();
    session_rejection();
    clock_and_queue();
    puts("protocol tests passed");
    return 0;
}
