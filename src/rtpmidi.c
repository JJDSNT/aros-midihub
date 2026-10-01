#include "midihub/rtpmidi.h"

#include <string.h>

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static size_t short_length(uint8_t status)
{
    if (status >= 0x80 && status <= 0xef)
        return (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 2 : 3;
    switch (status) {
    case 0xf1: case 0xf3: return 2;
    case 0xf2: return 3;
    case 0xf6: case 0xf8: case 0xfa: case 0xfb:
    case 0xfc: case 0xfe: case 0xff: return 1;
    default: return 0;
    }
}

int mh_rtp_encode_short(uint16_t sequence, uint32_t timestamp, uint32_t ssrc,
                        const uint8_t *midi, size_t midi_length,
                        uint8_t *data, size_t capacity, size_t *length)
{
    size_t i;

    if (!midi || !data || !length || !midi_length ||
        midi_length != short_length(midi[0]))
        return -1;
    for (i = 1; i < midi_length; ++i)
        if (midi[i] & 0x80)
            return -1;
    return mh_rtp_encode_list(sequence, timestamp, ssrc, midi, midi_length,
                              data, capacity, length);
}

int mh_rtp_encode_list(uint16_t sequence, uint32_t timestamp, uint32_t ssrc,
                       const uint8_t *midi, size_t midi_length,
                       uint8_t *data, size_t capacity, size_t *length)
{
    size_t header_length;
    if (!midi || !data || !length || !midi_length || midi_length > 4095)
        return -1;
    header_length = midi_length < 16 ? 13 : 14;
    if (capacity < header_length + midi_length)
        return -1;
    data[0] = 0x80;
    data[1] = 0xe1; /* Apple profile: marker=1, PT=0x61. */
    write16(data + 2, sequence);
    write32(data + 4, timestamp);
    write32(data + 8, ssrc);
    data[12] = (uint8_t)((header_length == 14 ? 0x80 : 0) |
                         ((midi_length >> (header_length == 14 ? 8 : 0)) & 0x0f));
    if (header_length == 14)
        data[13] = (uint8_t)midi_length;
    memcpy(data + header_length, midi, midi_length);
    *length = header_length + midi_length;
    return 0;
}

int mh_rtp_decode(const uint8_t *data, size_t length,
                  struct mh_rtp_packet *packet)
{
    size_t header_length;
    size_t midi_length;
    uint8_t flags;

    if (!data || !packet || length < 13 || data[0] != 0x80 ||
        data[1] != 0xe1)
        return -1;
    flags = data[12];
    header_length = (flags & 0x80) ? 14 : 13;
    if (length < header_length)
        return -1;
    midi_length = flags & 0x0f;
    if (flags & 0x80)
        midi_length = (midi_length << 8) | data[13];
    if (midi_length > length - header_length)
        return -1;
    if (!(flags & 0x40) && length != header_length + midi_length)
        return -1;
    packet->sequence = read16(data + 2);
    packet->timestamp = read32(data + 4);
    packet->ssrc = read32(data + 8);
    packet->midi = data + header_length;
    packet->midi_length = midi_length;
    packet->journal = (flags & 0x40) ? data + header_length + midi_length : 0;
    packet->journal_length = length - header_length - midi_length;
    packet->z = !!(flags & 0x20);
    packet->p = !!(flags & 0x10);
    if ((flags & 0x40) && !packet->journal_length)
        return -1;
    return 0;
}

int mh_journal_decode(const uint8_t *data, size_t length,
                      struct mh_journal *journal)
{
    size_t offset = 3;
    size_t section_length;
    size_t count;
    size_t i;
    int previous_channel = -1;

    if (!data || !journal || length < 3)
        return -1;
    memset(journal, 0, sizeof(*journal));
    journal->checkpoint = read16(data + 1);
    journal->single_packet_safe = !!(data[0] & 0x80);
    journal->enhanced_controllers = !!(data[0] & 0x10);
    if (data[0] & 0x40) {
        if (length - offset < 2)
            return -1;
        section_length = ((size_t)(data[offset] & 3) << 8) |
                         data[offset + 1];
        if (section_length < 2 || section_length > length - offset)
            return -1;
        journal->system = data + offset;
        journal->system_length = section_length;
        offset += section_length;
    }
    count = (data[0] & 0x20) ? (size_t)(data[0] & 0x0f) + 1 : 0;
    for (i = 0; i < count; ++i) {
        struct mh_journal_channel *channel = &journal->channels[i];
        if (length - offset < 3)
            return -1;
        section_length = ((size_t)(data[offset] & 3) << 8) |
                         data[offset + 1];
        if (section_length < 3 || section_length > length - offset)
            return -1;
        channel->number = (data[offset] >> 2) & 0x0f;
        if (channel->number <= previous_channel)
            return -1;
        previous_channel = channel->number;
        channel->single_packet_safe = !!(data[offset] & 0x80);
        channel->chapters = data[offset + 2];
        channel->data = data + offset;
        channel->length = section_length;
        offset += section_length;
    }
    if (offset != length)
        return -1;
    journal->channel_count = count;
    return 0;
}

int mh_journal_covers_gap(uint16_t previous, uint16_t current,
                          uint16_t checkpoint)
{
    uint16_t advance = (uint16_t)(current - previous);
    uint16_t first_missing = (uint16_t)(previous + 1);
    return advance > 1 && advance < 0x8000 &&
           (uint16_t)(first_missing - checkpoint) < 0x8000;
}

int mh_journal_decode_notes(const struct mh_journal_channel *channel,
                            struct mh_journal_notes *notes)
{
    const uint8_t *data;
    size_t length;
    size_t offset = 3;
    size_t size;
    size_t logs;
    size_t off_count;
    unsigned int low;
    unsigned int high;
    uint8_t seen[16] = {0};
    size_t i;
    uint8_t number;

    if (!channel || !notes || !channel->data ||
        channel->length < 3)
        return -1;
    memset(notes, 0, sizeof(*notes));
    if (!(channel->chapters & 0x08))
        return 0;
    data = channel->data;
    length = channel->length;
    if (channel->chapters & 0x80) {
        if (length - offset < 3) return -1;
        offset += 3; /* Chapter P. */
    }
    if (channel->chapters & 0x40) {
        if (length - offset < 1 || (data[0] & 0x04))
            return -1; /* Enhanced Chapter C needs separate decoding. */
        size = 1 + 2 * ((size_t)(data[offset] & 0x7f) + 1);
        if (size > length - offset) return -1;
        offset += size;
    }
    if (channel->chapters & 0x20) {
        if (length - offset < 2) return -1;
        size = ((size_t)(data[offset] & 3) << 8) | data[offset + 1];
        if (size < 2 || size > length - offset) return -1;
        offset += size; /* Chapter M. */
    }
    if (channel->chapters & 0x10) {
        if (length - offset < 2) return -1;
        offset += 2; /* Chapter W. */
    }
    if (length - offset < 2) return -1;
    notes->offbits_single_packet_safe = !!(data[offset] & 0x80);
    logs = data[offset] & 0x7f;
    low = data[offset + 1] >> 4;
    high = data[offset + 1] & 0x0f;
    if (low <= high)
        off_count = high - low + 1;
    else if (low == 15 && (high == 0 || high == 1))
        off_count = 0;
    else
        return -1;
    if (logs == 127 && low == 15 && high == 0)
        ++logs;
    size = 2 + logs * 2 + off_count;
    if (size > length - offset) return -1;
    notes->log_count = logs;
    for (i = 0; i < logs; ++i) {
        const uint8_t *log = data + offset + 2 + i * 2;
        struct mh_journal_note_log *entry = &notes->logs[i];
        number = log[0] & 0x7f;
        if (!((log[1] & 0x7f)) ||
            (seen[number / 8] & (0x80u >> (number % 8))))
            return -1;
        seen[number / 8] |= (uint8_t)(0x80u >> (number % 8));
        entry->number = number;
        entry->velocity = log[1] & 0x7f;
        entry->single_packet_safe = !!(log[0] & 0x80);
        entry->simultaneous = !!(log[1] & 0x80);
    }
    if (off_count)
        memcpy(notes->offbits + low,
               data + offset + 2 + logs * 2, off_count);
    for (i = 0; i < 16; ++i)
        if (seen[i] & notes->offbits[i])
            return -1;
    return 1;
}

void mh_rtp_reader_init(struct mh_rtp_reader *reader,
                        const struct mh_rtp_packet *packet)
{
    reader->packet = packet;
    reader->offset = 0;
    reader->timestamp = packet->timestamp;
    reader->running_status = 0;
}

int mh_rtp_reader_next(struct mh_rtp_reader *reader,
                       struct mh_midi_event *event)
{
    const struct mh_rtp_packet *packet;
    const uint8_t *midi;
    size_t offset;
    uint32_t delta = 0;
    size_t i;
    size_t needed;
    uint8_t status;
    uint8_t byte;
    int has_status;

    if (!reader || !event || !reader->packet)
        return -1;
    packet = reader->packet;
    midi = packet->midi;
    offset = reader->offset;
    if (offset >= packet->midi_length)
        return 0;

    if (offset || packet->z) {
        for (i = 0; i < 4; ++i) {
            if (offset >= packet->midi_length)
                return -1;
            byte = midi[offset++];
            delta = (delta << 7) | (byte & 0x7f);
            if (!(byte & 0x80))
                break;
        }
        if (i == 4)
            return -1;
        if (offset == packet->midi_length)
            return 0; /* A trailing delta time is legal. */
    }
    status = midi[offset];
    has_status = !!(status & 0x80);
    if (has_status) {
        ++offset;
        if (status >= 0x80 && status <= 0xef)
            reader->running_status = status;
        else if (status < 0xf8)
            reader->running_status = 0;
    } else {
        status = reader->running_status;
        if (!status)
            return -1;
    }
    if (status == 0xf0 || status == 0xf7) {
        size_t start = offset - 1;
        while (offset < packet->midi_length && !(midi[offset] & 0x80))
            ++offset;
        if (offset >= packet->midi_length ||
            (midi[offset] != 0xf0 && midi[offset] != 0xf7 &&
             !(status == 0xf7 && midi[offset] == 0xf4)))
            return -1;
        event->length = 0;
        event->sysex = midi + start;
        event->sysex_length = offset + 1 - start;
        reader->timestamp += delta;
        event->timestamp = reader->timestamp;
        reader->offset = offset + 1;
        return 1;
    }
    if (status >= 0x80 && status <= 0xef)
        needed = ((status & 0xf0) == 0xc0 ||
                  (status & 0xf0) == 0xd0) ? 1 : 2;
    else if (status == 0xf1 || status == 0xf3)
        needed = 1;
    else if (status == 0xf2)
        needed = 2;
    else if (status == 0xf6)
        needed = 0;
    else if (status >= 0xf8 && status != 0xf9 && status != 0xfd)
        needed = 0;
    else
        return -2;
    if (packet->midi_length - offset < needed)
        return -1;
    event->bytes[0] = status;
    event->bytes[1] = 0;
    event->bytes[2] = 0;
    for (i = 0; i < needed; ++i) {
        if (midi[offset + i] & 0x80)
            return -1;
        event->bytes[i + 1] = midi[offset + i];
    }
    event->length = (uint8_t)(needed + 1);
    event->sysex = NULL;
    event->sysex_length = 0;
    reader->timestamp += delta;
    event->timestamp = reader->timestamp;
    reader->offset = offset + needed;
    return 1;
}

void mh_sysex_reset(struct mh_sysex_assembler *assembler)
{
    memset(assembler, 0, sizeof(*assembler));
}

int mh_sysex_feed(struct mh_sysex_assembler *assembler,
                  const struct mh_midi_event *event, uint16_t sequence,
                  const uint8_t **message, size_t *length)
{
    const uint8_t *segment;
    size_t size;
    uint8_t head;
    uint8_t tail;
    if (!assembler || !event || !message || !length ||
        !event->sysex || event->sysex_length < 2)
        return -1;
    segment = event->sysex;
    size = event->sysex_length;
    head = segment[0];
    tail = segment[size - 1];
    if (head == 0xf7 && tail == 0xf4) {
        mh_sysex_reset(assembler);
        return 0;
    }
    if (head == 0xf0) {
        if (size > MH_SYSEX_MAX) {
            mh_sysex_reset(assembler);
            return -1;
        }
        memcpy(assembler->bytes, segment, size - (tail == 0xf0));
        assembler->length = size - (tail == 0xf0);
        assembler->active = tail == 0xf0;
    } else if (head == 0xf7 && assembler->active &&
               (sequence == assembler->last_sequence ||
                sequence == (uint16_t)(assembler->last_sequence + 1))) {
        if (assembler->length + size - 1 - (tail == 0xf0) > MH_SYSEX_MAX) {
            mh_sysex_reset(assembler);
            return -1;
        }
        memcpy(assembler->bytes + assembler->length, segment + 1,
               size - 1 - (tail == 0xf0));
        assembler->length += size - 1 - (tail == 0xf0);
        assembler->active = tail == 0xf0;
    } else {
        mh_sysex_reset(assembler);
        return -1;
    }
    assembler->last_sequence = sequence;
    if (!assembler->active) {
        *message = assembler->bytes;
        *length = assembler->length;
        return 1;
    }
    return 0;
}
