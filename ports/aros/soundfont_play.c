/* Render one General MIDI note with TinySoundFont and play it via ahi.device. */
#include <devices/ahi.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <stdio.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

enum { SAMPLE_RATE = 44100, SECONDS = 2, NOTE = 60 };

int main(int argc, char **argv)
{
    const ULONG sample_count = SAMPLE_RATE * SECONDS;
    struct MsgPort *port = NULL;
    struct AHIRequest *request = NULL;
    short *samples = NULL;
    tsf *synth = NULL;
    int device_open = 0;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s BANK.sf2\n", argv[0]);
        return 2;
    }
    synth = tsf_load_filename(argv[1]);
    if (!synth) {
        fprintf(stderr, "Could not load SoundFont: %s\n", argv[1]);
        goto cleanup;
    }
    samples = AllocVec(sample_count * sizeof(*samples), MEMF_PUBLIC);
    if (!samples) {
        fputs("Could not allocate PCM buffer\n", stderr);
        goto cleanup;
    }
    tsf_set_output(synth, TSF_MONO, SAMPLE_RATE, 0);
    if (!tsf_note_on(synth, 0, NOTE, 1.0f)) {
        fputs("Could not start note\n", stderr);
        goto cleanup;
    }
    tsf_render_short(synth, samples, SAMPLE_RATE, 0);
    tsf_note_off(synth, 0, NOTE);
    tsf_render_short(synth, samples + SAMPLE_RATE, SAMPLE_RATE, 0);

    port = CreateMsgPort();
    if (!port) {
        fputs("Could not create AHI message port\n", stderr);
        goto cleanup;
    }
    request = (struct AHIRequest *)CreateIORequest(port, sizeof(*request));
    if (!request) {
        fputs("Could not create AHI request\n", stderr);
        goto cleanup;
    }
    request->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, 0,
                   (struct IORequest *)request, 0) != 0) {
        fputs("Could not open ahi.device unit 0; check AHI Preferences\n",
              stderr);
        goto cleanup;
    }
    device_open = 1;
    request->ahir_Std.io_Command = CMD_WRITE;
    request->ahir_Std.io_Data = samples;
    request->ahir_Std.io_Length = sample_count * sizeof(*samples);
    request->ahir_Std.io_Offset = 0;
    request->ahir_Frequency = SAMPLE_RATE;
    request->ahir_Type = AHIST_M16S;
    request->ahir_Volume = 0x10000;
    request->ahir_Position = 0x8000;
    request->ahir_Link = NULL;
    if (DoIO((struct IORequest *)request) != 0) {
        fprintf(stderr, "AHI playback failed: %d\n",
                (int)request->ahir_Std.io_Error);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (device_open) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    if (samples) FreeVec(samples);
    if (synth) tsf_close(synth);
    return result;
}
