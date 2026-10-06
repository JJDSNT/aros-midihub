/* CAMD SoundFont instrument using the default ahi.device output. */
#include <midihub/synth.h>
#include <midihub/midi.h>

#include <devices/ahi.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

enum { SAMPLE_RATE = 44100, BLOCK_FRAMES = 2048 };

struct Library *CamdBase;

struct synth_runtime {
    struct mh_synth *synth;
    struct MidiNode *node;
    struct MidiLink *input;
    struct MsgPort *audio_port;
    struct AHIRequest *audio[2];
    int16_t *samples[2];
    int audio_open;
    int pending[2];
    uint8_t sysex[MH_SYSEX_MAX];
    LONG midi_signal;
    unsigned long messages;
    /* Public, so MIDIHub.prefs can tell the synth runs and stop it with
       Ctrl-C to the port's task. No messages are sent to it. */
    struct MsgPort *service_port;
    int service_port_public;
};

#define MIDIHUB_SYNTH_PORT "MIDIHub.Synth"

static int read_bank_path(const char *file_name, char *path, size_t capacity)
{
    FILE *file = fopen(file_name, "r");
    char *start;
    size_t length;
    if (!file)
        return -1;
    if (!fgets(path, (int)capacity, file)) {
        fclose(file);
        return -1;
    }
    if (!strchr(path, '\n') && !feof(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    start = path;
    while (*start == ' ' || *start == '\t')
        ++start;
    length = strlen(start);
    while (length && (start[length - 1] == '\n' ||
                      start[length - 1] == '\r' ||
                      start[length - 1] == ' ' ||
                      start[length - 1] == '\t'))
        --length;
    if (!length)
        return -1;
    start[length] = '\0';
    memmove(path, start, length + 1);
    return 0;
}

static int parse_backend(const char *name, enum mh_synth_backend *backend)
{
    if (strcmp(name, "tiny") == 0) {
        *backend = MH_SYNTH_TINY;
        return 0;
    }
    if (strcmp(name, "fluid") == 0) {
        *backend = MH_SYNTH_FLUID;
        return 0;
    }
    return -1;
}

static void receive_midi(struct synth_runtime *rt)
{
    MidiMsg msg;
    uint8_t bytes[3];
    size_t length;
    ULONG sysex_length;

    while (GetMidi(rt->node, &msg)) {
        bytes[0] = msg.mm_Status;
        bytes[1] = msg.mm_Data1;
        bytes[2] = msg.mm_Data2;
        if (bytes[0] == 0xf0) {
            sysex_length = QuerySysEx(rt->node);
            if (sysex_length >= 2 && sysex_length <= sizeof(rt->sysex) &&
                GetSysEx(rt->node, rt->sysex, sysex_length) == sysex_length) {
                if (mh_synth_send_sysex(rt->synth, rt->sysex,
                                        sysex_length) == 0)
                    rt->messages++;
            } else {
                SkipSysEx(rt->node);
            }
            continue;
        }
        if (bytes[0] < 0x80 || bytes[0] > 0xef)
            continue;
        length = (bytes[0] & 0xf0) == 0xc0 ||
                 (bytes[0] & 0xf0) == 0xd0 ? 2u : 3u;
        if (mh_synth_send(rt->synth, bytes, length) == 0)
            rt->messages++;
    }
}

static void queue_audio(struct synth_runtime *rt, unsigned index,
                        struct AHIRequest *previous)
{
    struct AHIRequest *request = rt->audio[index];
    mh_synth_render(rt->synth, rt->samples[index], BLOCK_FRAMES);
    request->ahir_Std.io_Command = CMD_WRITE;
    request->ahir_Std.io_Data = rt->samples[index];
    request->ahir_Std.io_Length = BLOCK_FRAMES * sizeof(int16_t);
    request->ahir_Std.io_Offset = 0;
    request->ahir_Frequency = SAMPLE_RATE;
    request->ahir_Type = AHIST_M16S;
    request->ahir_Volume = 0x10000;
    request->ahir_Position = 0x8000;
    request->ahir_Link = previous;
    SendIO((struct IORequest *)request);
    rt->pending[index] = 1;
}

static int complete_audio(struct synth_runtime *rt, unsigned index)
{
    if (!rt->pending[index] || !CheckIO((struct IORequest *)rt->audio[index]))
        return 0;
    rt->pending[index] = 0;
    if (WaitIO((struct IORequest *)rt->audio[index])) {
        printf("AHI playback failed: %d\n",
               (int)rt->audio[index]->ahir_Std.io_Error);
        return -1;
    }
    return 1;
}

static void cleanup(struct synth_runtime *rt)
{
    unsigned index;
    if (rt->service_port) {
        if (rt->service_port_public)
            RemPort(rt->service_port);
        DeleteMsgPort(rt->service_port);
    }
    if (rt->input)
        RemoveMidiLink(rt->input);
    if (rt->node)
        DeleteMidi(rt->node);
    if (rt->midi_signal >= 0)
        FreeSignal(rt->midi_signal);
    if (CamdBase) {
        CloseLibrary(CamdBase);
        CamdBase = NULL;
    }
    for (index = 0; index < 2; ++index) {
        if (rt->pending[index]) {
            AbortIO((struct IORequest *)rt->audio[index]);
            WaitIO((struct IORequest *)rt->audio[index]);
        }
    }
    if (rt->audio_open)
        CloseDevice((struct IORequest *)rt->audio[0]);
    if (rt->audio[1])
        FreeMem(rt->audio[1], sizeof(*rt->audio[1]));
    if (rt->audio[0])
        DeleteIORequest((struct IORequest *)rt->audio[0]);
    if (rt->audio_port)
        DeleteMsgPort(rt->audio_port);
    for (index = 0; index < 2; ++index)
        if (rt->samples[index])
            FreeVec(rt->samples[index]);
    mh_synth_close(rt->synth);
}

int main(int argc, char **argv)
{
    static char node_name[] = "MIDIHub Synth";
    static char port_name[] = "MIDIHub Synth";
    struct TagItem node_tags[] = {
        {MIDI_Name, (IPTR)node_name},
        {MIDI_MsgQueue, 256},
        {MIDI_SysExSize, MH_SYSEX_MAX},
        {MIDI_RecvSignal, 0},
        {TAG_DONE, 0}
    };
    struct TagItem input_tags[] = {
        {MLINK_Name, (IPTR)port_name},
        {MLINK_Location, (IPTR)port_name},
        {TAG_DONE, 0}
    };
    struct synth_runtime rt;
    char configured_bank[1024];
    char configured_backend[32];
    const char *bank_path = NULL;
    enum mh_synth_backend backend = MH_SYNTH_TINY;
    int backend_on_command_line = 0;
    int arg;
    ULONG signals;
    int done[2];
    int result = 20;
    unsigned index;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--backend") == 0) {
            if (++arg >= argc || parse_backend(argv[arg], &backend)) {
                puts("Usage: MIDIHubSynth [--backend tiny|fluid] [BANK.sf2]");
                return 20;
            }
            backend_on_command_line = 1;
        } else if (!bank_path) {
            bank_path = argv[arg];
        } else {
            puts("Usage: MIDIHubSynth [--backend tiny|fluid] [BANK.sf2]");
            return 20;
        }
    }
    if (!backend_on_command_line &&
        (read_bank_path("ENV:MidiHub/Backend", configured_backend,
                        sizeof(configured_backend)) == 0 ||
         read_bank_path("ENVARC:MidiHub/Backend", configured_backend,
                        sizeof(configured_backend)) == 0) &&
        parse_backend(configured_backend, &backend)) {
        printf("Unknown synth backend: %s\n", configured_backend);
        return 20;
    }
    if (!mh_synth_has_backend(backend)) {
        printf("Synth backend '%s' is unavailable in this build\n",
               mh_synth_backend_name(backend));
        return 20;
    }
    if (!bank_path) {
        if (read_bank_path("ENV:MidiHub/SoundFont", configured_bank,
                           sizeof(configured_bank)) == 0 ||
            read_bank_path("ENVARC:MidiHub/SoundFont", configured_bank,
                           sizeof(configured_bank)) == 0)
            bank_path = configured_bank;
        else {
            puts("Set ENV:MidiHub/SoundFont or pass BANK.sf2");
            return 20;
        }
    }
    memset(&rt, 0, sizeof(rt));
    rt.midi_signal = -1;
    rt.service_port = CreateMsgPort();
    if (!rt.service_port)
        return 20;
    rt.service_port->mp_Node.ln_Name = (char *)MIDIHUB_SYNTH_PORT;
    rt.service_port->mp_Node.ln_Pri = 0;
    Forbid();
    if (FindPort((CONST_STRPTR)MIDIHUB_SYNTH_PORT)) {
        Permit();
        DeleteMsgPort(rt.service_port);
        puts("MIDIHub Synth is already running");
        return 20;
    }
    Permit();
    rt.synth = mh_synth_open_backend(bank_path, SAMPLE_RATE, backend);
    if (!rt.synth) {
        printf("Cannot load SoundFont: %s\n", bank_path);
        goto out;
    }
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) {
        puts("camd.library is unavailable");
        goto out;
    }
    rt.midi_signal = AllocSignal(-1);
    if (rt.midi_signal < 0)
        goto out;
    node_tags[3].ti_Data = (IPTR)rt.midi_signal;
    rt.node = CreateMidiA(node_tags);
    if (!rt.node)
        goto out;
    rt.input = AddMidiLinkA(rt.node, MLTYPE_Receiver, input_tags);
    if (!rt.input)
        goto out;
    rt.audio_port = CreateMsgPort();
    if (!rt.audio_port)
        goto out;
    rt.audio[0] = (struct AHIRequest *)CreateIORequest(rt.audio_port,
                                                       sizeof(*rt.audio[0]));
    if (!rt.audio[0])
        goto out;
    rt.audio[0]->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, 0,
                   (struct IORequest *)rt.audio[0], 0)) {
        puts("Cannot open ahi.device unit 0; check AHI Preferences");
        goto out;
    }
    rt.audio_open = 1;
    rt.audio[1] = AllocMem(sizeof(*rt.audio[1]), MEMF_PUBLIC);
    if (!rt.audio[1])
        goto out;
    CopyMem(rt.audio[0], rt.audio[1], sizeof(*rt.audio[1]));
    for (index = 0; index < 2; ++index) {
        rt.samples[index] = AllocVec(BLOCK_FRAMES * sizeof(int16_t),
                                      MEMF_PUBLIC);
        if (!rt.samples[index])
            goto out;
    }
    queue_audio(&rt, 0, NULL);
    queue_audio(&rt, 1, rt.audio[0]);
    /* Published once the bank is loaded and AHI plays, so that a running
       MIDIHub.Synth port means a working synth. */
    AddPort(rt.service_port);
    rt.service_port_public = 1;
    printf("MIDIHub Synth: CAMD port '%s', backend %s, bank %s\n",
           port_name, mh_synth_backend_name(backend), bank_path);
    result = 0;
    for (;;) {
        signals = Wait((1UL << rt.midi_signal) |
                       (1UL << rt.audio_port->mp_SigBit) |
                       SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C)
            break;
        if (signals & (1UL << rt.midi_signal))
            receive_midi(&rt);
        if (!(signals & (1UL << rt.audio_port->mp_SigBit)))
            continue;
        for (index = 0; index < 2; ++index) {
            done[index] = complete_audio(&rt, index);
            if (done[index] < 0) {
                result = 20;
                goto out;
            }
        }
        for (index = 0; index < 2; ++index) {
            if (done[index]) {
                struct AHIRequest *previous = rt.pending[1 - index]
                                            ? rt.audio[1 - index] : NULL;
                queue_audio(&rt, index, previous);
            }
        }
    }
    printf("MIDIHub Synth: %lu MIDI messages received\n", rt.messages);
out:
    cleanup(&rt);
    return result;
}
