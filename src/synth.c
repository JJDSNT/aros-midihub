#include <midihub/synth.h>

#include <stdlib.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

struct mh_synth {
    tsf *font;
};

struct mh_synth *mh_synth_open(const char *bank_path, int sample_rate)
{
    struct mh_synth *synth;
    int channel;
    if (!bank_path || sample_rate <= 0) return NULL;
    synth = malloc(sizeof(*synth));
    if (!synth) return NULL;
    synth->font = tsf_load_filename(bank_path);
    if (!synth->font) {
        free(synth);
        return NULL;
    }
    tsf_set_output(synth->font, TSF_MONO, sample_rate, 0);
    for (channel = 0; channel < 16; ++channel)
        tsf_channel_set_presetnumber(synth->font, channel, 0, channel == 9);
    return synth;
}

void mh_synth_close(struct mh_synth *synth)
{
    if (!synth) return;
    tsf_close(synth->font);
    free(synth);
}

int mh_synth_send(struct mh_synth *synth, const uint8_t *message,
                  size_t length)
{
    unsigned status, command, channel;
    if (!synth || !message || !length) return -1;
    status = message[0];
    if (status < 0x80 || status > 0xef) return 1;
    command = status & 0xf0;
    channel = status & 0x0f;
    if (length != ((command == 0xc0 || command == 0xd0) ? 2u : 3u))
        return -1;
    if (message[1] > 127 || (length == 3 && message[2] > 127))
        return -1;
    switch (command) {
    case 0x80:
        tsf_channel_note_off(synth->font, (int)channel, message[1]);
        return 0;
    case 0x90:
        if (message[2] == 0) {
            tsf_channel_note_off(synth->font, (int)channel, message[1]);
            return 0;
        }
        return tsf_channel_note_on(synth->font, (int)channel,
                                   message[1], message[2] / 127.0f) ? 0 : -1;
    case 0xb0:
        return tsf_channel_midi_control(synth->font, (int)channel,
                                        message[1], message[2]) ? 0 : -1;
    case 0xc0:
        return tsf_channel_set_presetnumber(synth->font, (int)channel,
                                            message[1], channel == 9) ? 0 : -1;
    case 0xe0:
        return tsf_channel_set_pitchwheel(synth->font, (int)channel,
                                          message[1] | (message[2] << 7)) ? 0 : -1;
    default:
        return 1;
    }
}

void mh_synth_render(struct mh_synth *synth, int16_t *samples,
                     size_t sample_count)
{
    if (!synth || !samples) return;
    while (sample_count) {
        int block = sample_count > 16384 ? 16384 : (int)sample_count;
        tsf_render_short(synth->font, (short *)samples, block, 0);
        samples += block;
        sample_count -= (size_t)block;
    }
}
