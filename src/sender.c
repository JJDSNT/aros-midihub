#include "midihub/sender.h"

#include <string.h>

enum { MH_TOOL_VALUE, MH_TOOL_TOGGLE, MH_TOOL_COUNT };

void mh_sender_reset(struct mh_sender *sender)
{
    if (sender) memset(sender, 0, sizeof(*sender));
}

void mh_sender_clear_history(struct mh_sender *sender)
{
    if (sender) sender->count = 0;
}

int mh_sender_supported(const uint8_t *message, size_t length)
{
    uint8_t type;
    if (!message || !length || message[0] < 0x80 || message[0] > 0xef)
        return 0;
    type = message[0] & 0xf0;
    if (type == 0xc0)
        return length == 2;
    return length == 3 &&
           (type == 0x80 || type == 0x90 || type == 0xb0 || type == 0xe0);
}

void mh_sender_record(struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, const uint8_t *message,
                      size_t length)
{
    struct mh_sent_event *event;
    uint8_t type;
    unsigned int channel;
    if (!sender || !mh_sender_supported(message, length))
        return;
    if (sender->count == MH_SENDER_HISTORY) {
        memmove(sender->events, sender->events + 1,
                (MH_SENDER_HISTORY - 1) * sizeof(sender->events[0]));
        --sender->count;
    }
    event = &sender->events[sender->count++];
    memset(event, 0, sizeof(*event));
    event->sequence = sequence;
    event->timestamp = timestamp;
    event->length = (uint8_t)length;
    memcpy(event->bytes, message, length);
    type = message[0] & 0xf0;
    channel = message[0] & 0x0f;
    if (type == 0xb0) {
        event->recovery_value = message[2];
        if (message[1] == 0) {
            sender->bank_msb[channel] = message[2];
            sender->bank_known[channel] |= 1;
        } else if (message[1] == 32) {
            sender->bank_lsb[channel] = message[2];
            sender->bank_known[channel] |= 2;
        } else if (message[1] == 64) {
            uint8_t on = message[2] >= 64;
            if (sender->sustain_on[channel] != on)
                sender->sustain_toggle[channel] =
                    (uint8_t)((sender->sustain_toggle[channel] + 1) & 0x3f);
            sender->sustain_on[channel] = on;
            event->tool = MH_TOOL_TOGGLE;
            event->recovery_value = sender->sustain_toggle[channel];
        } else if (message[1] == 120 || message[1] == 123) {
            unsigned int index = message[1] == 120 ? 0 : 1;
            sender->reset_count[channel][index] =
                (uint8_t)((sender->reset_count[channel][index] + 1) & 0x3f);
            event->tool = MH_TOOL_COUNT;
            event->recovery_value = sender->reset_count[channel][index];
        } else if (message[1] == 121 && sender->sustain_on[channel]) {
            sender->sustain_toggle[channel] =
                (uint8_t)((sender->sustain_toggle[channel] + 1) & 0x3f);
            sender->sustain_on[channel] = 0;
        }
    } else if (type == 0xc0) {
        event->bank_msb = sender->bank_msb[channel];
        event->bank_lsb = sender->bank_lsb[channel];
        event->bank_known = sender->bank_known[channel];
    }
}

void mh_sender_ack(struct mh_sender *sender, uint32_t extended_sequence)
{
    uint16_t sequence = (uint16_t)extended_sequence;
    size_t remove_count = 0;
    if (!sender) return;
    while (remove_count < sender->count &&
           (uint16_t)(sequence - sender->events[remove_count].sequence) <
           0x8000)
        ++remove_count;
    if (remove_count) {
        sender->count -= remove_count;
        memmove(sender->events, sender->events + remove_count,
                sender->count * sizeof(sender->events[0]));
    }
}

static int put(uint8_t *data, size_t capacity, size_t *length, uint8_t value)
{
    if (*length >= capacity) return -1;
    data[(*length)++] = value;
    return 0;
}

static int build_channel(const struct mh_sender *sender, unsigned int channel,
                         uint16_t sequence, uint32_t timestamp,
                         uint8_t *data, size_t capacity, size_t *length,
                         int *recent)
{
    int latest_control[128];
    int latest_note[128];
    int program = -1;
    int pitch = -1;
    size_t i;
    size_t start = *length;
    size_t control_count = 0;
    size_t note_on_count = 0;
    uint8_t offbits[16] = {0};
    unsigned int low = 16;
    unsigned int high = 0;
    int off_recent = 0;
    uint8_t toc = 0;
    uint8_t type;
    uint8_t number;

    for (i = 0; i < 128; ++i)
        latest_control[i] = latest_note[i] = -1;
    for (i = 0; i < sender->count; ++i) {
        const struct mh_sent_event *event = &sender->events[i];
        if ((event->bytes[0] & 0x0f) != channel)
            continue;
        type = event->bytes[0] & 0xf0;
        if (type == 0x80 || type == 0x90)
            latest_note[event->bytes[1]] = (int)i;
        else if (type == 0xb0)
            latest_control[event->bytes[1]] = (int)i;
        else if (type == 0xc0)
            program = (int)i;
        else if (type == 0xe0)
            pitch = (int)i;
    }
    for (i = 0; i < 128; ++i) {
        if (latest_control[i] >= 0) ++control_count;
        if (latest_note[i] >= 0) {
            const struct mh_sent_event *event =
                &sender->events[latest_note[i]];
            if ((event->bytes[0] & 0xf0) == 0x90 && event->bytes[2])
                ++note_on_count;
            else {
                offbits[i / 8] |= (uint8_t)(0x80u >> (i % 8));
                if (i / 8 < low) low = (unsigned int)(i / 8);
                if (i / 8 > high) high = (unsigned int)(i / 8);
                if (event->sequence == (uint16_t)(sequence - 1))
                    off_recent = 1;
            }
        }
    }
    if (program < 0 && pitch < 0 && !control_count &&
        !note_on_count && low == 16)
        return 0;
    if (capacity - *length < 3) return -1;
    *length += 3;
    *recent = 0;
    if (program >= 0) {
        const struct mh_sent_event *event = &sender->events[program];
        int is_recent = event->sequence == (uint16_t)(sequence - 1);
        toc |= 0x80;
        *recent |= is_recent;
        if (put(data, capacity, length, (uint8_t)((is_recent ? 0 : 0x80) |
                                                event->bytes[1])) < 0 ||
            put(data, capacity, length,
                (uint8_t)((event->bank_known & 1 ? 0x80 : 0) |
                          event->bank_msb)) < 0 ||
            put(data, capacity, length, event->bank_lsb) < 0)
            return -1;
    }
    if (control_count) {
        size_t header = *length;
        int control_recent = 0;
        toc |= 0x40;
        if (put(data, capacity, length, 0) < 0) return -1;
        for (i = 0; i < sender->count; ++i) {
            const struct mh_sent_event *event = &sender->events[i];
            if ((event->bytes[0] & 0xf0) != 0xb0 ||
                (event->bytes[0] & 0x0f) != channel)
                continue;
            number = event->bytes[1];
            if (latest_control[number] != (int)i)
                continue;
            if (event->sequence == (uint16_t)(sequence - 1))
                control_recent = 1;
            if (put(data, capacity, length,
                    (uint8_t)((event->sequence ==
                               (uint16_t)(sequence - 1) ? 0 : 0x80) |
                              number)) < 0 ||
                put(data, capacity, length,
                    (uint8_t)((event->tool == MH_TOOL_VALUE ? 0 :
                               event->tool == MH_TOOL_COUNT ? 0xc0 : 0x80) |
                              event->recovery_value)) < 0)
                return -1;
        }
        data[header] = (uint8_t)((control_recent ? 0 : 0x80) |
                                 (control_count - 1));
        *recent |= control_recent;
    }
    if (pitch >= 0) {
        const struct mh_sent_event *event = &sender->events[pitch];
        int is_recent = event->sequence == (uint16_t)(sequence - 1);
        toc |= 0x10;
        *recent |= is_recent;
        if (put(data, capacity, length,
                (uint8_t)((is_recent ? 0 : 0x80) | event->bytes[1])) < 0 ||
            put(data, capacity, length, event->bytes[2]) < 0)
            return -1;
    }
    if (note_on_count || low != 16) {
        toc |= 0x08;
        *recent |= off_recent;
        if (put(data, capacity, length,
                (uint8_t)((off_recent ? 0 : 0x80) | note_on_count)) < 0 ||
            put(data, capacity, length,
                (uint8_t)(low == 16 ? 0xf1 : (low << 4) | high)) < 0)
            return -1;
        for (i = 0; i < sender->count; ++i) {
            const struct mh_sent_event *event = &sender->events[i];
            uint32_t elapsed;
            if ((event->bytes[0] & 0xf0) != 0x90 ||
                (event->bytes[0] & 0x0f) != channel || !event->bytes[2] ||
                latest_note[event->bytes[1]] != (int)i)
                continue;
            if (event->sequence == (uint16_t)(sequence - 1))
                *recent = 1;
            elapsed = timestamp - event->timestamp;
            if (put(data, capacity, length,
                    (uint8_t)((event->sequence ==
                               (uint16_t)(sequence - 1) ? 0 : 0x80) |
                              event->bytes[1])) < 0 ||
                put(data, capacity, length,
                    (uint8_t)((elapsed <= 1000 ? 0x80 : 0) |
                              event->bytes[2])) < 0)
                return -1;
        }
        if (low != 16)
            for (i = low; i <= high; ++i)
                if (put(data, capacity, length, offbits[i]) < 0)
                    return -1;
    }
    if (*length - start > 1023) return -1;
    data[start] = (uint8_t)((*recent ? 0 : 0x80) |
                            (channel << 2) | ((*length - start) >> 8));
    data[start + 1] = (uint8_t)(*length - start);
    data[start + 2] = toc;
    return 1;
}

int mh_sender_journal(const struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, uint8_t *data, size_t capacity,
                      size_t *length)
{
    size_t used = 3;
    unsigned int channel;
    unsigned int channel_count = 0;
    int recent = 0;
    int any_recent = 0;
    int result;
    uint16_t checkpoint;
    if (!sender || !data || !length || capacity < 3)
        return -1;
    checkpoint = sender->count ? sender->events[0].sequence : sequence;
    for (channel = 0; channel < 16; ++channel) {
        result = build_channel(sender, channel, sequence, timestamp,
                               data, capacity, &used, &recent);
        if (result < 0) return -1;
        if (result > 0) {
            ++channel_count;
            any_recent |= recent;
        }
    }
    data[0] = (uint8_t)((any_recent ? 0 : 0x80) |
                        (channel_count ? 0x20 | (channel_count - 1) : 0));
    data[1] = (uint8_t)(checkpoint >> 8);
    data[2] = (uint8_t)checkpoint;
    *length = used;
    return 0;
}
