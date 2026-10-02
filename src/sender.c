#include "midihub/sender.h"

#include <string.h>

enum { MH_TOOL_VALUE, MH_TOOL_TOGGLE, MH_TOOL_COUNT };
enum { MH_MTC_UNKNOWN, MH_MTC_FORWARD, MH_MTC_REVERSE };
enum { MH_SYSTEM_NONE, MH_SYSTEM_MTC, MH_SYSTEM_SYSEX };

static int mtc_valid(const uint8_t nibbles[8])
{
    unsigned int frame = nibbles[0] | ((nibbles[1] & 1) << 4);
    unsigned int second = nibbles[2] | ((nibbles[3] & 3) << 4);
    unsigned int minute = nibbles[4] | ((nibbles[5] & 3) << 4);
    unsigned int hour = nibbles[6] | ((nibbles[7] & 1) << 4);
    unsigned int rate = (nibbles[7] >> 1) & 3;
    unsigned int fps = rate == 0 ? 24 : rate == 1 ? 25 : 30;
    return !(nibbles[7] & 8) && frame < fps && second < 60 &&
           minute < 60 && hour < 24;
}

static int mtc_offset_forward(uint8_t nibbles[8])
{
    unsigned int frame = nibbles[0] | ((nibbles[1] & 1) << 4);
    unsigned int second = nibbles[2] | ((nibbles[3] & 3) << 4);
    unsigned int minute = nibbles[4] | ((nibbles[5] & 3) << 4);
    unsigned int hour = nibbles[6] | ((nibbles[7] & 1) << 4);
    unsigned int rate = (nibbles[7] >> 1) & 3;
    unsigned int fps = rate == 0 ? 24 : rate == 1 ? 25 : 30;
    unsigned int step;
    if (!mtc_valid(nibbles)) return -1;
    for (step = 0; step < 2; ++step) {
        if (++frame < fps) continue;
        frame = 0;
        if (++second < 60) continue;
        second = 0;
        if (++minute >= 60) {
            minute = 0;
            hour = (hour + 1) % 24;
        }
        if (rate == 2 && minute % 10)
            frame = 2;
    }
    nibbles[0] = (uint8_t)(frame & 15);
    nibbles[1] = (uint8_t)(frame >> 4);
    nibbles[2] = (uint8_t)(second & 15);
    nibbles[3] = (uint8_t)(second >> 4);
    nibbles[4] = (uint8_t)(minute & 15);
    nibbles[5] = (uint8_t)(minute >> 4);
    nibbles[6] = (uint8_t)(hour & 15);
    nibbles[7] = (uint8_t)((hour >> 4) | (rate << 1));
    return 0;
}

static void mtc_record_quarter_frame(struct mh_sender *sender, uint8_t data)
{
    unsigned int type = data >> 4;
    unsigned int previous = sender->mtc_qf_point;
    sender->mtc_qf_nibbles[type] = data & 15;
    sender->mtc_qf_seen |= (uint8_t)(1u << type);

    if (sender->mtc_qf_direction == MH_MTC_FORWARD &&
        type == ((previous + 1) & 7)) {
        if (type == 0) sender->mtc_partial_mask = 1;
        else sender->mtc_partial_mask |= (uint8_t)(1u << type);
    } else if (sender->mtc_qf_direction == MH_MTC_REVERSE &&
               type == ((previous + 7) & 7)) {
        if (type == 7) sender->mtc_partial_mask = 0x80;
        else if (type >= 2)
            sender->mtc_partial_mask |= (uint8_t)(1u << type);
        else
            sender->mtc_partial_mask = 0;
    } else if (type == 0) {
        sender->mtc_qf_direction = MH_MTC_FORWARD;
        sender->mtc_partial_mask = 1;
        sender->mtc_qf_seen = 1;
    } else if (type == 7) {
        sender->mtc_qf_direction = MH_MTC_REVERSE;
        sender->mtc_partial_mask = 0x80;
        sender->mtc_qf_seen =
            (uint8_t)(previous == 0 && (sender->mtc_qf_seen & 1) ?
                      0x81 : 0x80);
    } else {
        sender->mtc_qf_direction = MH_MTC_UNKNOWN;
        sender->mtc_partial_mask = 0;
        sender->mtc_qf_seen = (uint8_t)(1u << type);
    }
    sender->mtc_qf_point = (uint8_t)type;

    if (sender->mtc_qf_direction == MH_MTC_FORWARD && type == 7 &&
        sender->mtc_partial_mask == 0xff) {
        memcpy(sender->mtc_complete, sender->mtc_qf_nibbles, 8);
        if (mtc_offset_forward(sender->mtc_complete) == 0) {
            sender->mtc_complete_known = 1;
            sender->mtc_complete_quarter_frame = 1;
        }
        sender->mtc_partial_mask = 0;
    } else if (sender->mtc_qf_direction == MH_MTC_REVERSE && type == 1 &&
               (sender->mtc_qf_seen & 0xff) == 0xff) {
        memcpy(sender->mtc_complete, sender->mtc_qf_nibbles, 8);
        if (mtc_valid(sender->mtc_complete)) {
            sender->mtc_complete_known = 1;
            sender->mtc_complete_quarter_frame = 1;
        }
        sender->mtc_partial_mask = 0;
    }
}

void mh_sender_reset(struct mh_sender *sender)
{
    if (sender) memset(sender, 0, sizeof(*sender));
}

void mh_sender_clear_history(struct mh_sender *sender)
{
    if (sender) {
        sender->count = 0;
        sender->sysex_log_count = 0;
    }
}

int mh_sender_supported(const uint8_t *message, size_t length)
{
    uint8_t type;
    if (!message || !length || message[0] < 0x80)
        return 0;
    if (message[0] == 0xf1)
        return length == 2 && message[1] < 0x80;
    if (message[0] == 0xf2)
        return length == 3 && message[1] < 0x80 && message[2] < 0x80;
    if (message[0] == 0xf3)
        return length == 2 && message[1] < 0x80;
    if (message[0] == 0xf6 || message[0] == 0xf8 ||
        message[0] == 0xfa || message[0] == 0xfb ||
        message[0] == 0xfc || message[0] == 0xfe || message[0] == 0xff)
        return length == 1;
    if (message[0] > 0xef)
        return 0;
    type = message[0] & 0xf0;
    if (type == 0xc0 || type == 0xd0)
        return length == 2;
    return length == 3 &&
           (type == 0x80 || type == 0x90 || type == 0xa0 ||
            type == 0xb0 || type == 0xe0);
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
    if (message[0] >= 0xf0) {
        if (message[0] == 0xff) {
            if (sender->count > 1) {
                sender->events[0] = *event;
                sender->count = 1;
            }
            memset(sender->bank_known, 0, sizeof(sender->bank_known));
            memset(sender->sustain_on, 0, sizeof(sender->sustain_on));
            memset(sender->sustain_toggle, 0,
                   sizeof(sender->sustain_toggle));
            memset(sender->reset_count, 0, sizeof(sender->reset_count));
            memset(sender->active_notes, 0, sizeof(sender->active_notes));
            sender->system_reset_count =
                (uint8_t)((sender->system_reset_count + 1) & 0x7f);
            sender->tune_request_count = 0;
            sender->active_sense_count = 0;
            sender->song_select_known = 0;
            sender->sequencer_known = 0;
            sender->sequencer_running = 0;
            sender->sequencer_downbeat = 0;
            sender->sequencer_start_at_zero = 0;
            sender->sequencer_clock = 0;
            sender->mtc_complete_known = 0;
            sender->mtc_complete_quarter_frame = 0;
            sender->mtc_qf_seen = 0;
            sender->mtc_partial_mask = 0;
            sender->mtc_qf_direction = MH_MTC_UNKNOWN;
            sender->mtc_qf_point = 0;
            sender->sysex_log_count = 0;
        } else if (message[0] == 0xf6) {
            sender->tune_request_count =
                (uint8_t)((sender->tune_request_count + 1) & 0x7f);
        } else if (message[0] == 0xfe) {
            sender->active_sense_count =
                (uint8_t)((sender->active_sense_count + 1) & 0x7f);
        } else if (message[0] == 0xf3) {
            sender->song_select = message[1];
            sender->song_select_known = 1;
        } else if (message[0] == 0xf2) {
            sender->sequencer_known = 1;
            sender->sequencer_downbeat = 0;
            sender->sequencer_start_at_zero = 0;
            sender->sequencer_clock =
                ((((uint32_t)message[2] << 7) | message[1]) * 6) & 0x7ffff;
        } else if (message[0] == 0xfa) {
            sender->sequencer_known = 1;
            sender->sequencer_running = 1;
            sender->sequencer_downbeat = 0;
            sender->sequencer_start_at_zero = 1;
            sender->sequencer_clock = 0;
        } else if (message[0] == 0xfb) {
            sender->sequencer_known = 1;
            sender->sequencer_running = 1;
            sender->sequencer_start_at_zero = 0;
        } else if (message[0] == 0xfc) {
            sender->sequencer_known = 1;
            sender->sequencer_running = 0;
            sender->sequencer_start_at_zero = 0;
        } else if (message[0] == 0xf8) {
            sender->sequencer_known = 1;
            sender->sequencer_start_at_zero = 0;
            if (sender->sequencer_running) {
                if (!sender->sequencer_downbeat)
                    sender->sequencer_downbeat = 1;
                else
                    sender->sequencer_clock =
                        (sender->sequencer_clock + 1) & 0x7ffff;
            }
        } else if (message[0] == 0xf1) {
            mtc_record_quarter_frame(sender, message[1]);
        }
        return;
    }
    if (type == 0x80 || type == 0x90) {
        sender->active_notes[channel][message[1]] =
            (uint8_t)(type == 0x90 && message[2] != 0);
    } else if (type == 0xb0) {
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
        if (message[1] == 120 || message[1] >= 123)
            memset(sender->active_notes[channel], 0,
                   sizeof(sender->active_notes[channel]));
    } else if (type == 0xc0) {
        event->bank_msb = sender->bank_msb[channel];
        event->bank_lsb = sender->bank_lsb[channel];
        event->bank_known = sender->bank_known[channel];
    }
}

int mh_sender_record_sysex(struct mh_sender *sender, uint16_t sequence,
                           uint32_t timestamp, const uint8_t *message,
                           size_t length)
{
    struct mh_sent_event *event;
    struct mh_sent_sysex *log;
    size_t pending = 0;
    size_t i;
    int is_mtc;
    if (!sender || !message || length < 2 || message[0] != 0xf0 ||
        message[length - 1] != 0xf7 ||
        length - 2 > MH_SYSEX_JOURNAL_MAX)
        return 0;
    for (i = 1; i + 1 < length; ++i)
        if (message[i] & 0x80)
            return 0;
    is_mtc = length == 10 && message[1] == 0x7f &&
             message[3] == 0x01 && message[4] == 0x01;
    if (!is_mtc) {
        for (i = 0; i < sender->sysex_log_count; ++i)
            pending += sender->sysex_logs[i].length;
        if (sender->sysex_log_count == MH_SYSEX_JOURNAL_LOGS ||
            pending + length - 2 > MH_SYSEX_JOURNAL_MAX)
            mh_sender_clear_history(sender);
    }
    if (sender->count == MH_SENDER_HISTORY) {
        memmove(sender->events, sender->events + 1,
                (MH_SENDER_HISTORY - 1) * sizeof(sender->events[0]));
        --sender->count;
    }
    event = &sender->events[sender->count++];
    memset(event, 0, sizeof(*event));
    event->sequence = sequence;
    event->timestamp = timestamp;
    event->bytes[0] = 0xf0;
    event->length = 1;
    if (is_mtc) {
        event->system_kind = MH_SYSTEM_MTC;
        memset(sender->mtc_complete, 0, sizeof(sender->mtc_complete));
        memcpy(sender->mtc_complete, message + 5, 4);
        sender->mtc_complete_known = 1;
        sender->mtc_complete_quarter_frame = 0;
        sender->mtc_qf_seen = 0;
        sender->mtc_partial_mask = 0;
        sender->mtc_qf_direction = MH_MTC_UNKNOWN;
    } else {
        event->system_kind = MH_SYSTEM_SYSEX;
        ++sender->sysex_count;
        log = &sender->sysex_logs[sender->sysex_log_count++];
        log->sequence = sequence;
        log->count = sender->sysex_count;
        log->length = length - 2;
        memcpy(log->data, message + 1, log->length);
    }
    return 1;
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
    remove_count = 0;
    while (remove_count < sender->sysex_log_count &&
           (uint16_t)(sequence - sender->sysex_logs[remove_count].sequence) <
           0x8000)
        ++remove_count;
    if (remove_count) {
        sender->sysex_log_count -= remove_count;
        memmove(sender->sysex_logs, sender->sysex_logs + remove_count,
                sender->sysex_log_count * sizeof(sender->sysex_logs[0]));
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
    int latest_poly[128];
    int program = -1;
    int pitch = -1;
    int aftertouch = -1;
    int reset_controllers = -1;
    size_t i;
    size_t start = *length;
    size_t control_count = 0;
    size_t note_on_count = 0;
    size_t poly_count = 0;
    uint8_t offbits[16] = {0};
    unsigned int low = 16;
    unsigned int high = 0;
    int off_recent = 0;
    uint8_t toc = 0;
    uint8_t type;
    uint8_t number;

    for (i = 0; i < 128; ++i)
        latest_control[i] = latest_note[i] = latest_poly[i] = -1;
    for (i = 0; i < sender->count; ++i) {
        const struct mh_sent_event *event = &sender->events[i];
        if ((event->bytes[0] & 0x0f) != channel)
            continue;
        type = event->bytes[0] & 0xf0;
        if (type == 0x80 || type == 0x90)
            latest_note[event->bytes[1]] = (int)i;
        else if (type == 0xb0)
            latest_control[event->bytes[1]] = (int)i;
        else if (type == 0xa0)
            latest_poly[event->bytes[1]] = (int)i;
        else if (type == 0xc0)
            program = (int)i;
        else if (type == 0xd0)
            aftertouch = (int)i;
        else if (type == 0xe0)
            pitch = (int)i;
        if (type == 0xb0 && event->bytes[1] == 121)
            reset_controllers = (int)i;
    }
    if (aftertouch <= reset_controllers)
        aftertouch = -1;
    if (aftertouch >= 0) {
        for (i = 0; i < 128 && !sender->active_notes[channel][i]; ++i) {}
        if (i == 128) aftertouch = -1;
    }
    for (i = 0; i < 128; ++i) {
        if (latest_control[i] >= 0) ++control_count;
        if (latest_poly[i] > reset_controllers) ++poly_count;
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
        !note_on_count && low == 16 && aftertouch < 0 && !poly_count)
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
    if (aftertouch >= 0) {
        const struct mh_sent_event *event = &sender->events[aftertouch];
        int is_recent = event->sequence == (uint16_t)(sequence - 1);
        toc |= 0x02;
        *recent |= is_recent;
        if (put(data, capacity, length,
                (uint8_t)((is_recent ? 0 : 0x80) | event->bytes[1])) < 0)
            return -1;
    }
    if (poly_count) {
        size_t header = *length;
        int poly_recent = 0;
        toc |= 0x01;
        if (put(data, capacity, length, 0) < 0) return -1;
        for (i = 0; i < sender->count; ++i) {
            const struct mh_sent_event *event = &sender->events[i];
            size_t later;
            int reset_notes = 0;
            if ((event->bytes[0] & 0xf0) != 0xa0 ||
                (event->bytes[0] & 0x0f) != channel ||
                latest_poly[event->bytes[1]] != (int)i ||
                (int)i <= reset_controllers)
                continue;
            for (later = i + 1; later < sender->count; ++later) {
                const struct mh_sent_event *next = &sender->events[later];
                if ((next->bytes[0] & 0xf0) == 0xb0 &&
                    (next->bytes[0] & 0x0f) == channel &&
                    (next->bytes[1] == 120 || next->bytes[1] >= 123)) {
                    reset_notes = 1;
                    break;
                }
            }
            if (event->sequence == (uint16_t)(sequence - 1))
                poly_recent = 1;
            if (put(data, capacity, length,
                    (uint8_t)((event->sequence ==
                               (uint16_t)(sequence - 1) ? 0 : 0x80) |
                              event->bytes[1])) < 0 ||
                put(data, capacity, length,
                    (uint8_t)((reset_notes ? 0x80 : 0) |
                              event->bytes[2])) < 0)
                return -1;
        }
        data[header] = (uint8_t)((poly_recent ? 0 : 0x80) |
                                 (poly_count - 1));
        *recent |= poly_recent;
    }
    if (*length - start > 1023) return -1;
    data[start] = (uint8_t)((*recent ? 0 : 0x80) |
                            (channel << 2) | ((*length - start) >> 8));
    data[start + 1] = (uint8_t)(*length - start);
    data[start + 2] = toc;
    return 1;
}

static int build_system(const struct mh_sender *sender, uint16_t sequence,
                        uint8_t *data, size_t capacity, size_t *length,
                        int *recent)
{
    int reset = -1;
    int tune = -1;
    int song = -1;
    int active = -1;
    int sequencer = -1;
    int mtc = -1;
    size_t i;
    size_t start;
    size_t d_header = 0;
    uint8_t d_flags = 0;
    int d_recent = 0;
    if (!sender || !data || !length || !recent) return -1;
    for (i = 0; i < sender->count; ++i) {
        switch (sender->events[i].bytes[0]) {
        case 0xff:
            reset = (int)i;
            tune = song = active = sequencer = mtc = -1;
            break;
        case 0xf6: tune = (int)i; break;
        case 0xf3: song = (int)i; break;
        case 0xfe: active = (int)i; break;
        case 0xf2:
        case 0xf8:
        case 0xfa:
        case 0xfb:
        case 0xfc: sequencer = (int)i; break;
        case 0xf0:
            if (sender->events[i].system_kind == MH_SYSTEM_MTC)
                mtc = (int)i;
            break;
        case 0xf1: mtc = (int)i; break;
        default: break;
        }
    }
    if (reset < 0 && tune < 0 && song < 0 && active < 0 && sequencer < 0 &&
        mtc < 0 && sender->sysex_log_count == 0)
        return 0;
    if (capacity - *length < 2) return -1;
    start = *length;
    *length += 2;
    data[start] = 0;
    data[start + 1] = 0;
    *recent = 0;
    if (reset >= 0 || tune >= 0 || song >= 0) {
        if (*length >= capacity) return -1;
        d_header = (*length)++;
        if (reset >= 0) {
            int is_recent = sender->events[reset].sequence ==
                            (uint16_t)(sequence - 1);
            d_flags |= 0x40;
            d_recent |= is_recent;
            if (put(data, capacity, length,
                    (uint8_t)((is_recent ? 0 : 0x80) |
                              sender->system_reset_count)) < 0)
                return -1;
        }
        if (tune >= 0) {
            int is_recent = sender->events[tune].sequence ==
                            (uint16_t)(sequence - 1);
            d_flags |= 0x20;
            d_recent |= is_recent;
            if (put(data, capacity, length,
                    (uint8_t)((is_recent ? 0 : 0x80) |
                              sender->tune_request_count)) < 0)
                return -1;
        }
        if (song >= 0 && sender->song_select_known) {
            int is_recent = sender->events[song].sequence ==
                            (uint16_t)(sequence - 1);
            d_flags |= 0x10;
            d_recent |= is_recent;
            if (put(data, capacity, length,
                    (uint8_t)((is_recent ? 0 : 0x80) |
                              sender->song_select)) < 0)
                return -1;
        }
        data[d_header] = (uint8_t)((d_recent ? 0 : 0x80) | d_flags);
        data[start] |= 0x40;
        *recent |= d_recent;
    }
    if (active >= 0) {
        int is_recent = sender->events[active].sequence ==
                        (uint16_t)(sequence - 1);
        if (put(data, capacity, length,
                (uint8_t)((is_recent ? 0 : 0x80) |
                          sender->active_sense_count)) < 0)
            return -1;
        data[start] |= 0x20;
        *recent |= is_recent;
    }
    if (sequencer >= 0 && sender->sequencer_known) {
        int is_recent = sender->events[sequencer].sequence ==
                        (uint16_t)(sequence - 1);
        int has_clock = !(sender->sequencer_running &&
                          !sender->sequencer_downbeat &&
                          sender->sequencer_start_at_zero &&
                          sender->sequencer_clock == 0);
        uint8_t q_header = (uint8_t)((is_recent ? 0 : 0x80) |
                           (sender->sequencer_running ? 0x40 : 0) |
                           (sender->sequencer_downbeat ? 0x20 : 0));
        if (has_clock)
            q_header = (uint8_t)(q_header | 0x10 |
                                 (sender->sequencer_clock >> 16));
        if (put(data, capacity, length, q_header) < 0)
            return -1;
        if (has_clock &&
            (put(data, capacity, length,
                 (uint8_t)(sender->sequencer_clock >> 8)) < 0 ||
             put(data, capacity, length,
                 (uint8_t)sender->sequencer_clock) < 0))
            return -1;
        data[start] |= 0x10;
        *recent |= is_recent;
    }
    if (mtc >= 0) {
        int is_recent = sender->events[mtc].sequence ==
                        (uint16_t)(sequence - 1);
        uint8_t f_header = (uint8_t)((is_recent ? 0 : 0x80) |
                           (sender->mtc_complete_known ? 0x40 : 0) |
                           (sender->mtc_partial_mask ? 0x20 : 0) |
                           (sender->mtc_complete_known &&
                            sender->mtc_complete_quarter_frame ? 0x10 : 0) |
                           (sender->mtc_qf_direction == MH_MTC_REVERSE ?
                            0x08 : 0) |
                           (sender->mtc_partial_mask ? sender->mtc_qf_point :
                            sender->mtc_qf_direction == MH_MTC_REVERSE ?
                            0 : 7));
        if (put(data, capacity, length, f_header) < 0)
            return -1;
        if (sender->mtc_complete_known) {
            if (sender->mtc_complete_quarter_frame) {
                for (i = 0; i < 8; i += 2)
                    if (put(data, capacity, length,
                            (uint8_t)((sender->mtc_complete[i] << 4) |
                                      sender->mtc_complete[i + 1])) < 0)
                        return -1;
            } else {
                for (i = 0; i < 4; ++i)
                    if (put(data, capacity, length,
                            sender->mtc_complete[i]) < 0)
                        return -1;
            }
        }
        if (sender->mtc_partial_mask) {
            for (i = 0; i < 8; i += 2) {
                uint8_t high = sender->mtc_partial_mask & (1u << i) ?
                               sender->mtc_qf_nibbles[i] : 0;
                uint8_t low = sender->mtc_partial_mask & (1u << (i + 1)) ?
                              sender->mtc_qf_nibbles[i + 1] : 0;
                if (put(data, capacity, length,
                        (uint8_t)((high << 4) | low)) < 0)
                    return -1;
            }
        }
        data[start] |= 0x08;
        *recent |= is_recent;
    }
    if (sender->sysex_log_count) {
        for (i = 0; i < sender->sysex_log_count; ++i) {
            const struct mh_sent_sysex *log = &sender->sysex_logs[i];
            size_t data_index;
            int is_recent = log->sequence == (uint16_t)(sequence - 1);
            uint8_t x_header = (uint8_t)((is_recent ? 0 : 0x80) | 0x27 |
                                         (log->length ? 0x08 : 0));
            if (i == 0 && sender->sysex_log_count > 1)
                x_header &= 0x7f;
            if (put(data, capacity, length, x_header) < 0 ||
                put(data, capacity, length, log->count) < 0)
                return -1;
            for (data_index = 0; data_index < log->length; ++data_index) {
                uint8_t value = log->data[data_index];
                if (data_index + 1 == log->length) value |= 0x80;
                if (put(data, capacity, length, value) < 0)
                    return -1;
            }
            *recent |= is_recent;
        }
        data[start] |= 0x04;
    }
    data[start] = (uint8_t)(data[start] |
                            ((*length - start) >> 8));
    data[start + 1] = (uint8_t)(*length - start);
    return 1;
}

int mh_sender_journal(const struct mh_sender *sender, uint16_t sequence,
                      uint32_t timestamp, uint8_t *data, size_t capacity,
                      size_t *length)
{
    size_t used = 3;
    unsigned int channel;
    unsigned int channel_count = 0;
    int has_system = 0;
    int recent = 0;
    int any_recent = 0;
    int result;
    uint16_t checkpoint;
    if (!sender || !data || !length || capacity < 3)
        return -1;
    checkpoint = sender->count ? sender->events[0].sequence : sequence;
    result = build_system(sender, sequence, data, capacity, &used, &recent);
    if (result < 0) return -1;
    if (result > 0) {
        has_system = 1;
        any_recent |= recent;
    }
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
                        (has_system ? 0x40 : 0) |
                        (channel_count ? 0x20 | (channel_count - 1) : 0));
    data[1] = (uint8_t)(checkpoint >> 8);
    data[2] = (uint8_t)checkpoint;
    *length = used;
    return 0;
}
