/* Small, portable TinySoundFont smoke test. Output is always little-endian WAV. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

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
    tsf *synth;
    FILE *out;
    short samples[BLOCK_SAMPLES];
    unsigned sample_index = 0;
    unsigned nonzero = 0;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s BANK.sf2 OUTPUT.wav\n", argv[0]);
        return 2;
    }
    synth = tsf_load_filename(argv[1]);
    if (!synth) {
        fprintf(stderr, "Could not load SoundFont: %s\n", argv[1]);
        return 1;
    }
    tsf_set_output(synth, TSF_MONO, SAMPLE_RATE, 0);
    if (!tsf_note_on(synth, 0, 60, 1.0f)) {
        fprintf(stderr, "Could not start note\n");
        tsf_close(synth);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        tsf_close(synth);
        return 1;
    }
    if (!write_header(out)) goto finish;
    while (sample_index < SAMPLE_RATE * SECONDS) {
        unsigned count = SAMPLE_RATE * SECONDS - sample_index;
        unsigned i;
        if (sample_index == SAMPLE_RATE) tsf_note_off(synth, 0, 60);
        if (count > BLOCK_SAMPLES) count = BLOCK_SAMPLES;
        if (sample_index < SAMPLE_RATE && count > SAMPLE_RATE - sample_index)
            count = SAMPLE_RATE - sample_index;
        tsf_render_short(synth, samples, (int)count, 0);
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
    tsf_close(synth);
    return result;
}
