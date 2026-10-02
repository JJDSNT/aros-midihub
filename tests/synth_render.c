/* Portable MIDIHub synthesizer smoke test. Output is little-endian WAV. */
#include <midihub/synth.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SAMPLE_RATE = 44100, SECONDS = 2, BLOCK_SAMPLES = 512 };

static int put_u16(FILE *out, unsigned value)
{
    return fputc(value & 255, out) != EOF &&
           fputc((value >> 8) & 255, out) != EOF;
}

static int put_u32(FILE *out, uint32_t value)
{
    return put_u16(out, value & 65535u) && put_u16(out, value >> 16);
}

static int write_header(FILE *out)
{
    const uint32_t data_bytes = SAMPLE_RATE * SECONDS * 2u;
    return fwrite("RIFF", 1, 4, out) == 4 &&
           put_u32(out, data_bytes + 36u) &&
           fwrite("WAVEfmt ", 1, 8, out) == 8 &&
           put_u32(out, 16) && put_u16(out, 1) && put_u16(out, 1) &&
           put_u32(out, SAMPLE_RATE) && put_u32(out, SAMPLE_RATE * 2u) &&
           put_u16(out, 2) && put_u16(out, 16) &&
           fwrite("data", 1, 4, out) == 4 && put_u32(out, data_bytes);
}

int main(int argc, char **argv)
{
    struct mh_synth *synth;
    FILE *out;
    int16_t samples[BLOCK_SAMPLES];
    const uint8_t program[] = {0xc0, 0};
    const uint8_t note_on[] = {0x90, 60, 127};
    const uint8_t note_off[] = {0x80, 60, 0};
    const uint8_t key_pressure[] = {0xa0, 60, 64};
    const uint8_t channel_pressure[] = {0xd0, 64};
    const uint8_t gm_system_on[] = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
    unsigned sample_index = 0;
    unsigned nonzero = 0;
    int result = 1;
    enum mh_synth_backend backend = MH_SYNTH_TINY;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s BANK.sf2 OUTPUT.wav [tiny|fluid]\n",
                argv[0]);
        return 2;
    }
    if (argc == 4) {
        if (strcmp(argv[3], "fluid") == 0)
            backend = MH_SYNTH_FLUID;
        else if (strcmp(argv[3], "tiny") != 0) {
            fprintf(stderr, "Unknown backend: %s\n", argv[3]);
            return 2;
        }
    }
    if (!mh_synth_has_backend(backend)) {
        fprintf(stderr, "Backend unavailable: %s\n",
                mh_synth_backend_name(backend));
        return 2;
    }
    synth = mh_synth_open_backend(argv[1], SAMPLE_RATE, backend);
    if (!synth) {
        fprintf(stderr, "Could not load SoundFont: %s\n", argv[1]);
        return 1;
    }
    if (backend == MH_SYNTH_FLUID &&
        mh_synth_send_sysex(synth, gm_system_on, sizeof(gm_system_on)) != 0) {
        fprintf(stderr, "FluidSynth SysEx failed\n");
        mh_synth_close(synth);
        return 1;
    }
    if (backend == MH_SYNTH_TINY &&
        mh_synth_send_sysex(synth, gm_system_on, sizeof(gm_system_on)) != 1) {
        fprintf(stderr, "TinySoundFont accepted unsupported SysEx\n");
        mh_synth_close(synth);
        return 1;
    }
    if (mh_synth_send(synth, program, sizeof(program)) != 0 ||
        mh_synth_send(synth, note_on, sizeof(note_on)) != 0) {
        fprintf(stderr, "Could not start note\n");
        mh_synth_close(synth);
        return 1;
    }
    if (backend == MH_SYNTH_FLUID &&
        (mh_synth_send(synth, key_pressure, sizeof(key_pressure)) != 0 ||
         mh_synth_send(synth, channel_pressure,
                       sizeof(channel_pressure)) != 0)) {
        fprintf(stderr, "FluidSynth extended MIDI messages failed\n");
        mh_synth_close(synth);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        mh_synth_close(synth);
        return 1;
    }
    if (!write_header(out)) goto finish;
    while (sample_index < SAMPLE_RATE * SECONDS) {
        unsigned count = SAMPLE_RATE * SECONDS - sample_index;
        unsigned i;
        if (sample_index == SAMPLE_RATE &&
            mh_synth_send(synth, note_off, sizeof(note_off)) != 0)
            goto finish;
        if (count > BLOCK_SAMPLES) count = BLOCK_SAMPLES;
        if (sample_index < SAMPLE_RATE && count > SAMPLE_RATE - sample_index)
            count = SAMPLE_RATE - sample_index;
        mh_synth_render(synth, samples, count);
        for (i = 0; i < count; ++i) {
            if (samples[i] != 0) ++nonzero;
            if (!put_u16(out, (uint16_t) samples[i])) goto finish;
        }
        sample_index += count;
    }
    if (!nonzero) {
        fprintf(stderr, "SoundFont rendered silence\n");
        goto finish;
    }
    result = 0;
    printf("Rendered %u nonzero samples to %s\n", nonzero, argv[2]);
finish:
    if (fclose(out) != 0) result = 1;
    mh_synth_close(synth);
    return result;
}
