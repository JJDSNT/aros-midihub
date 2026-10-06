#include "midihub/ump.h"

#include <string.h>

unsigned mh_ump_words(uint32_t first_word)
{
    static const unsigned char words[16] = {
        1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4
    };
    return words[first_word >> 28];
}

uint32_t mh_ump_scale_down(uint32_t value, unsigned from_bits, unsigned to_bits)
{
    return value >> (from_bits - to_bits);
}

uint32_t mh_ump_scale_up(uint32_t value, unsigned from_bits, unsigned to_bits)
{
    unsigned scale_bits = to_bits - from_bits;
    uint32_t shifted = value << scale_bits;
    uint32_t center = 1u << (from_bits - 1);
    unsigned repeat_bits;
    uint32_t repeat;

    /* Values up to the center scale by shifting, so that the center maps
       to the center; above it the lower bits are filled by repeating the
       source bits below the top one, so that the maximum maps to the
       maximum. */
    if (value <= center)
        return shifted;
    repeat_bits = from_bits - 1;
    repeat = value & ((1u << repeat_bits) - 1);
    if (scale_bits > repeat_bits)
        repeat <<= scale_bits - repeat_bits;
    else
        repeat >>= repeat_bits - scale_bits;
    while (repeat) {
        shifted |= repeat;
        repeat >>= repeat_bits;
    }
    return shifted;
}

/* MIDI 1.0 to UMP */

void mh_ump_from_midi1_init(struct mh_ump_from_midi1 *state, uint8_t group)
{
    memset(state, 0, sizeof(*state));
    state->group = group & 0x0f;
}

static unsigned midi1_data_bytes(uint8_t status)
{
    switch (status & 0xf0) {
    case 0xc0:
    case 0xd0:
        return 1;
    case 0xf0:
        switch (status) {
        case 0xf1:
        case 0xf3:
            return 1;
        case 0xf2:
            return 2;
        default:
            return 0;
        }
    default:
        return 2;
    }
}

static void emit_sysex7(struct mh_ump_from_midi1 *state, unsigned kind,
                        mh_ump_sink sink, void *context)
{
    uint32_t words[2];
    const uint8_t *b = state->sysex;

    words[0] = ((uint32_t)MH_UMP_DATA64 << 28) | ((uint32_t)state->group << 24) |
               ((uint32_t)kind << 20) | ((uint32_t)state->sysex_count << 16) |
               ((uint32_t)b[0] << 8) | b[1];
    words[1] = ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) |
               ((uint32_t)b[4] << 8) | b[5];
    sink(context, words, 2);
    memset(state->sysex, 0, sizeof(state->sysex));
    state->sysex_count = 0;
}

/* SysEx7 packet kinds */
enum { SYSEX7_COMPLETE, SYSEX7_START, SYSEX7_CONTINUE, SYSEX7_END };

static void emit_short(struct mh_ump_from_midi1 *state, uint8_t status,
                       uint8_t data1, uint8_t data2, mh_ump_sink sink, void *context)
{
    uint32_t word;
    unsigned type = status >= 0xf0 ? MH_UMP_SYSTEM : MH_UMP_MIDI1_VOICE;

    word = ((uint32_t)type << 28) | ((uint32_t)state->group << 24) |
           ((uint32_t)status << 16) | ((uint32_t)data1 << 8) | data2;
    sink(context, &word, 1);
}

void mh_ump_from_midi1_byte(struct mh_ump_from_midi1 *state, uint8_t byte,
                            mh_ump_sink sink, void *context)
{
    if (byte >= 0xf8) {
        /* Real time: may come anywhere, even inside SysEx. */
        emit_short(state, byte, 0, 0, sink, context);
        return;
    }
    if (byte == 0xf0) {
        state->in_sysex = 1;
        state->sysex_started = 0;
        state->sysex_count = 0;
        state->status = 0;
        return;
    }
    if (state->in_sysex) {
        if (byte == 0xf7 || byte & 0x80) {
            emit_sysex7(state, state->sysex_started ? SYSEX7_END : SYSEX7_COMPLETE,
                        sink, context);
            state->in_sysex = 0;
            if (byte == 0xf7)
                return;
            /* Any other status ends the SysEx and starts a message. */
        } else {
            if (state->sysex_count == 6) {
                emit_sysex7(state, state->sysex_started ? SYSEX7_CONTINUE : SYSEX7_START,
                            sink, context);
                state->sysex_started = 1;
            }
            state->sysex[state->sysex_count++] = byte;
            return;
        }
    }
    if (byte & 0x80) {
        if (byte == 0xf7)
            return;
        state->need = (uint8_t)midi1_data_bytes(byte);
        state->have = 0;
        if (state->need == 0) {
            emit_short(state, byte, 0, 0, sink, context);
            state->status = 0;
            return;
        }
        /* System Common messages cancel running status. */
        state->status = byte;
        return;
    }
    if (!state->status)
        return;
    state->data[state->have++] = byte;
    if (state->have < state->need)
        return;
    emit_short(state, state->status, state->data[0],
               state->need == 2 ? state->data[1] : 0, sink, context);
    state->have = 0;
    if (state->status >= 0xf0)
        state->status = 0;
}

/* UMP to MIDI 1.0 */

void mh_ump_to_midi1_init(struct mh_ump_to_midi1 *state, uint8_t group)
{
    memset(state, 0, sizeof(*state));
    state->group = group & 0x0f;
}

static void put3(mh_midi1_sink sink, void *context, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t bytes[3] = { a, b, c };
    sink(context, bytes, 3);
}

static void put_cc(mh_midi1_sink sink, void *context, uint8_t channel,
                   uint8_t controller, uint8_t value)
{
    put3(sink, context, (uint8_t)(0xb0 | channel), controller, value);
}

static void midi2_voice_down(const uint32_t *words, mh_midi1_sink sink, void *context)
{
    unsigned opcode = (words[0] >> 20) & 0x0f;
    uint8_t channel = (uint8_t)((words[0] >> 16) & 0x0f);
    uint8_t byte2 = (uint8_t)(words[0] >> 8);
    uint8_t byte3 = (uint8_t)words[0];
    uint32_t data = words[1];
    uint8_t bytes[2];
    uint32_t value14;

    switch (opcode) {
    case 0x8: /* Note Off */
        put3(sink, context, (uint8_t)(0x80 | channel), byte2 & 0x7f,
             (uint8_t)mh_ump_scale_down(data >> 16, 16, 7));
        break;
    case 0x9: { /* Note On: 0 would mean Note Off in MIDI 1.0 */
        uint8_t velocity = (uint8_t)mh_ump_scale_down(data >> 16, 16, 7);
        put3(sink, context, (uint8_t)(0x90 | channel), byte2 & 0x7f,
             velocity ? velocity : 1);
        break;
    }
    case 0xa: /* Poly Pressure */
        put3(sink, context, (uint8_t)(0xa0 | channel), byte2 & 0x7f,
             (uint8_t)mh_ump_scale_down(data, 32, 7));
        break;
    case 0xb: /* Control Change */
        put_cc(sink, context, channel, byte2 & 0x7f,
               (uint8_t)mh_ump_scale_down(data, 32, 7));
        break;
    case 0x2: /* Registered Controller: RPN */
    case 0x3: /* Assignable Controller: NRPN */
        value14 = mh_ump_scale_down(data, 32, 14);
        put_cc(sink, context, channel, opcode == 0x2 ? 101 : 99, byte2 & 0x7f);
        put_cc(sink, context, channel, opcode == 0x2 ? 100 : 98, byte3 & 0x7f);
        put_cc(sink, context, channel, 6, (uint8_t)(value14 >> 7));
        put_cc(sink, context, channel, 38, (uint8_t)(value14 & 0x7f));
        break;
    case 0xc: /* Program Change, with the bank when Bank Valid is set */
        if (byte3 & 0x01) {
            put_cc(sink, context, channel, 0, (uint8_t)((data >> 8) & 0x7f));
            put_cc(sink, context, channel, 32, (uint8_t)(data & 0x7f));
        }
        bytes[0] = (uint8_t)(0xc0 | channel);
        bytes[1] = (uint8_t)((data >> 24) & 0x7f);
        sink(context, bytes, 2);
        break;
    case 0xd: /* Channel Pressure */
        bytes[0] = (uint8_t)(0xd0 | channel);
        bytes[1] = (uint8_t)mh_ump_scale_down(data, 32, 7);
        sink(context, bytes, 2);
        break;
    case 0xe: /* Pitch Bend */
        value14 = mh_ump_scale_down(data, 32, 14);
        put3(sink, context, (uint8_t)(0xe0 | channel), (uint8_t)(value14 & 0x7f),
             (uint8_t)(value14 >> 7));
        break;
    default:
        /* Per-note and relative controllers, per-note pitch bend and
           management have no MIDI 1.0 form. */
        break;
    }
}

int mh_ump_to_midi1(struct mh_ump_to_midi1 *state, const uint32_t *words,
                    unsigned count, mh_midi1_sink sink, void *context)
{
    unsigned type = words[0] >> 28;
    unsigned needed = mh_ump_words(words[0]);
    uint8_t status;

    if (count < needed)
        return -1;
    if (type != MH_UMP_UTILITY && type != MH_UMP_STREAM &&
        ((words[0] >> 24) & 0x0f) != state->group)
        return 0;

    switch (type) {
    case MH_UMP_SYSTEM:
        status = (uint8_t)(words[0] >> 16);
        if (status >= 0xf8 || status == 0xf6 || status == 0xf4 || status == 0xf5) {
            sink(context, &status, 1);
        } else if (status == 0xf1 || status == 0xf3) {
            uint8_t bytes[2] = { status, (uint8_t)((words[0] >> 8) & 0x7f) };
            sink(context, bytes, 2);
        } else if (status == 0xf2) {
            put3(sink, context, status, (uint8_t)((words[0] >> 8) & 0x7f),
                 (uint8_t)(words[0] & 0x7f));
        }
        break;
    case MH_UMP_MIDI1_VOICE: {
        uint8_t bytes[3];
        status = (uint8_t)(words[0] >> 16);
        if (status < 0x80 || status >= 0xf0)
            break;
        bytes[0] = status;
        bytes[1] = (uint8_t)((words[0] >> 8) & 0x7f);
        bytes[2] = (uint8_t)(words[0] & 0x7f);
        sink(context, bytes, midi1_data_bytes(status) + 1);
        break;
    }
    case MH_UMP_DATA64: {
        unsigned kind = (words[0] >> 20) & 0x0f;
        unsigned n = (words[0] >> 16) & 0x0f;
        uint8_t bytes[8], *out = bytes;
        uint8_t data[6];
        unsigned i;

        if (n > 6 || kind > SYSEX7_END)
            break;
        data[0] = (uint8_t)(words[0] >> 8);
        data[1] = (uint8_t)words[0];
        data[2] = (uint8_t)(words[1] >> 24);
        data[3] = (uint8_t)(words[1] >> 16);
        data[4] = (uint8_t)(words[1] >> 8);
        data[5] = (uint8_t)words[1];
        if (kind == SYSEX7_COMPLETE || kind == SYSEX7_START) {
            *out++ = 0xf0;
            state->in_sysex = 1;
        } else if (!state->in_sysex) {
            break;          /* a continuation without its start */
        }
        for (i = 0; i < n; ++i)
            *out++ = data[i] & 0x7f;
        if (kind == SYSEX7_COMPLETE || kind == SYSEX7_END) {
            *out++ = 0xf7;
            state->in_sysex = 0;
        }
        sink(context, bytes, (size_t)(out - bytes));
        break;
    }
    case MH_UMP_MIDI2_VOICE:
        midi2_voice_down(words, sink, context);
        break;
    default:
        break;
    }
    return 0;
}

/* UMP Stream messages */

unsigned mh_ump_stream_status(const uint32_t *words)
{
    return (words[0] >> 16) & 0x3ff;
}

static uint32_t stream_word0(unsigned form, unsigned status)
{
    return ((uint32_t)MH_UMP_STREAM << 28) | ((uint32_t)form << 26) |
           ((uint32_t)status << 16);
}

void mh_ump_endpoint_info(uint32_t *words, unsigned protocols)
{
    /* UMP version 1.1; static function blocks, none declared. */
    words[0] = stream_word0(0, MH_UMP_ENDPOINT_INFO) | 0x0101;
    words[1] = 0x80000000u |
               ((protocols & MH_UMP_PROTOCOL_MIDI2) ? 0x200u : 0) |
               ((protocols & MH_UMP_PROTOCOL_MIDI1) ? 0x100u : 0);
    words[2] = 0;
    words[3] = 0;
}

void mh_ump_stream_config(uint32_t *words, unsigned protocol)
{
    words[0] = stream_word0(0, MH_UMP_STREAM_CONFIG_NOTIFY) |
               ((uint32_t)(protocol & 0xff) << 8);
    words[1] = 0;
    words[2] = 0;
    words[3] = 0;
}

unsigned mh_ump_stream_text(uint32_t *words, size_t capacity_words,
                            unsigned status, const char *text, size_t length)
{
    unsigned packets = (unsigned)(length ? (length + 13) / 14 : 1);
    unsigned packet, i, written = 0;

    if ((size_t)packets * 4 > capacity_words)
        return 0;
    for (packet = 0; packet < packets; ++packet) {
        uint8_t bytes[14];
        unsigned form;
        size_t offset = (size_t)packet * 14;

        memset(bytes, 0, sizeof(bytes));
        for (i = 0; i < 14 && offset + i < length; ++i)
            bytes[i] = (uint8_t)text[offset + i];
        form = packets == 1 ? 0 : packet == 0 ? 1 : packet == packets - 1 ? 3 : 2;
        words[written++] = stream_word0(form, status) | ((uint32_t)bytes[0] << 8) | bytes[1];
        for (i = 2; i < 14; i += 4)
            words[written++] = ((uint32_t)bytes[i] << 24) | ((uint32_t)bytes[i + 1] << 16) |
                               ((uint32_t)bytes[i + 2] << 8) | bytes[i + 3];
    }
    return written;
}
