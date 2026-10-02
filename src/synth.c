#include <midihub/synth.h>

#include <limits.h>
#include <stdlib.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

struct mh_synth {
    enum mh_synth_backend backend;
    tsf *font;
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    fluid_settings_t *fluid_settings;
    fluid_synth_t *fluid;
#endif
};

struct mh_synth *mh_synth_open(const char *bank_path, int sample_rate)
{
    return mh_synth_open_backend(bank_path, sample_rate, MH_SYNTH_TINY);
}

int mh_synth_has_backend(enum mh_synth_backend backend)
{
    if (backend == MH_SYNTH_TINY) return 1;
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (backend == MH_SYNTH_FLUID) return 1;
#endif
    return 0;
}

const char *mh_synth_backend_name(enum mh_synth_backend backend)
{
    if (backend == MH_SYNTH_TINY) return "tiny";
    if (backend == MH_SYNTH_FLUID) return "fluid";
    return NULL;
}

struct mh_synth *mh_synth_open_backend(const char *bank_path, int sample_rate,
                                       enum mh_synth_backend backend)
{
    struct mh_synth *synth;
    int channel;
    if (!bank_path || sample_rate <= 0 || !mh_synth_has_backend(backend))
        return NULL;
    synth = calloc(1, sizeof(*synth));
    if (!synth) return NULL;
    synth->backend = backend;
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (backend == MH_SYNTH_FLUID) {
        synth->fluid_settings = new_fluid_settings();
        if (!synth->fluid_settings ||
            fluid_settings_setnum(synth->fluid_settings, "synth.sample-rate",
                                  (double)sample_rate) != FLUID_OK)
            goto fail;
        synth->fluid = new_fluid_synth(synth->fluid_settings);
        if (!synth->fluid || fluid_synth_sfload(synth->fluid, bank_path, 1) < 0)
            goto fail;
        return synth;
    }
#endif
    synth->font = tsf_load_filename(bank_path);
    if (!synth->font) goto fail;
    tsf_set_output(synth->font, TSF_MONO, sample_rate, 0);
    for (channel = 0; channel < 16; ++channel)
        tsf_channel_set_presetnumber(synth->font, channel, 0, channel == 9);
    return synth;
fail:
    mh_synth_close(synth);
    return NULL;
}

void mh_synth_close(struct mh_synth *synth)
{
    if (!synth) return;
    if (synth->font) tsf_close(synth->font);
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (synth->fluid) delete_fluid_synth(synth->fluid);
    if (synth->fluid_settings) delete_fluid_settings(synth->fluid_settings);
#endif
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
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (synth->backend == MH_SYNTH_FLUID) {
        int result;
        switch (command) {
        case 0x80:
            result = fluid_synth_noteoff(synth->fluid, (int)channel,
                                         message[1]);
            break;
        case 0x90:
            result = fluid_synth_noteon(synth->fluid, (int)channel,
                                        message[1], message[2]);
            break;
        case 0xa0:
            result = fluid_synth_key_pressure(synth->fluid, (int)channel,
                                              message[1], message[2]);
            break;
        case 0xb0:
            result = fluid_synth_cc(synth->fluid, (int)channel,
                                    message[1], message[2]);
            break;
        case 0xc0:
            result = fluid_synth_program_change(synth->fluid, (int)channel,
                                                message[1]);
            break;
        case 0xd0:
            result = fluid_synth_channel_pressure(synth->fluid, (int)channel,
                                                  message[1]);
            break;
        case 0xe0:
            result = fluid_synth_pitch_bend(synth->fluid, (int)channel,
                                            message[1] | (message[2] << 7));
            break;
        default:
            return 1;
        }
        return result == FLUID_OK ? 0 : -1;
    }
#endif
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

int mh_synth_send_sysex(struct mh_synth *synth, const uint8_t *message,
                        size_t length)
{
    if (!synth || !message || length < 2 || message[0] != 0xf0 ||
        message[length - 1] != 0xf7 || length - 2 > INT_MAX)
        return -1;
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (synth->backend == MH_SYNTH_FLUID) {
        int handled = 0;
        int result = fluid_synth_sysex(synth->fluid,
                                       (const char *)(message + 1),
                                       (int)(length - 2), NULL, NULL,
                                       &handled, 0);
        if (result != FLUID_OK)
            return -1;
        return handled ? 0 : 1;
    }
#endif
    return 1;
}

void mh_synth_render(struct mh_synth *synth, int16_t *samples,
                     size_t sample_count)
{
    if (!synth || !samples) return;
#ifdef MIDIHUB_ENABLE_FLUIDSYNTH
    if (synth->backend == MH_SYNTH_FLUID) {
        int16_t stereo[1024];
        while (sample_count) {
            size_t count = sample_count > 512 ? 512 : sample_count;
            size_t index;
            if (fluid_synth_write_s16(synth->fluid, (int)count, stereo, 0, 2,
                                      stereo, 1, 2) != FLUID_OK) {
                for (index = 0; index < sample_count; ++index)
                    samples[index] = 0;
                return;
            }
            for (index = 0; index < count; ++index)
                samples[index] = (int16_t)(((int)stereo[index * 2] +
                                            (int)stereo[index * 2 + 1]) / 2);
            samples += count;
            sample_count -= count;
        }
        return;
    }
#endif
    while (sample_count) {
        int block = sample_count > 16384 ? 16384 : (int)sample_count;
        tsf_render_short(synth->font, (short *)samples, block, 0);
        samples += block;
        sample_count -= (size_t)block;
    }
}
