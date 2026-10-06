/* Native CAMD diagnostic.

   MIDIHubCAMDProbe                     round trip through MIDIHub In/Out
   MIDIHubCAMDProbe --synth             a note to MIDIHub Synth
   MIDIHubCAMDProbe --send CLUSTER      a C major scale and a SysEx Identity
                                        Request to any cluster, e.g.
                                        "BLE MIDI Out"
   MIDIHubCAMDProbe --monitor CLUSTER [SECONDS]
                                        print what arrives on a cluster, e.g.
                                        "BLE MIDI In" (default 60 s,
                                        Ctrl-C stops)
   MIDIHubCAMDProbe --rethink           have CAMD load drivers added to
                                        DEVS:Midi, listing the clusters
                                        before and after */
#include <dos/dos.h>
#include <exec/libraries.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *CamdBase;

static int send_to(struct MidiNode *node, char *cluster)
{
    static UBYTE identity_request[] = {0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};
    static const UBYTE scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
    struct TagItem tags[] = {
        {MLINK_Location, (IPTR)cluster},
        {TAG_DONE, 0}
    };
    struct MidiLink *sender = AddMidiLinkA(node, MLTYPE_Sender, tags);
    unsigned int i;

    if (!sender) {
        printf("CAMDProbe: cannot send to %s\n", cluster);
        return 20;
    }
    Delay(25);
    if (!MidiLinkConnected(sender))
        printf("CAMDProbe: nothing receives on %s yet; sending anyway\n", cluster);
    printf("CAMDProbe: playing a C major scale to %s\n", cluster);
    for (i = 0; i < sizeof(scale); ++i) {
        PutMidi(sender, 0x90000000UL | ((ULONG)scale[i] << 16) | (100UL << 8));
        Delay(15);
        PutMidi(sender, 0x80000000UL | ((ULONG)scale[i] << 16));
        Delay(3);
    }
    puts("CAMDProbe: sending SysEx F0 7E 7F 06 01 F7 (Identity Request)");
    PutSysEx(sender, identity_request);
    Delay(10);
    RemoveMidiLink(sender);
    return 0;
}

static int monitor(struct MidiNode *node, char *cluster, LONG seconds)
{
    static UBYTE sysex[4096];
    struct TagItem tags[] = {
        {MLINK_Location, (IPTR)cluster},
        {TAG_DONE, 0}
    };
    struct MidiLink *receiver = AddMidiLinkA(node, MLTYPE_Receiver, tags);
    MidiMsg message;
    LONG ticks = seconds * 50, count = 0;

    if (!receiver) {
        printf("CAMDProbe: cannot listen on %s\n", cluster);
        return 20;
    }
    printf("CAMDProbe: listening on %s for %ld s (Ctrl-C stops)\n",
           cluster, (long)seconds);
    while (ticks-- > 0 && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C)) {
        while (GetMidi(node, &message)) {
            ULONG length, i;
            count++;
            if (message.mm_Status != 0xf0) {
                printf("CAMDProbe: %02x %02x %02x\n",
                       (unsigned int)message.mm_Status,
                       (unsigned int)message.mm_Data1,
                       (unsigned int)message.mm_Data2);
                continue;
            }
            length = QuerySysEx(node);
            if (length > sizeof(sysex) || GetSysEx(node, sysex, length) != length) {
                printf("CAMDProbe: SysEx of %lu bytes skipped\n", (unsigned long)length);
                SkipSysEx(node);
                continue;
            }
            printf("CAMDProbe: SysEx %lu bytes:", (unsigned long)length);
            for (i = 0; i < length && i < 24; ++i)
                printf(" %02x", (unsigned int)sysex[i]);
            puts(length > 24 ? " ..." : "");
        }
        Delay(1);
    }
    printf("CAMDProbe: %ld messages\n", (long)count);
    RemoveMidiLink(receiver);
    return 0;
}

static void list_clusters(void)
{
    struct MidiCluster *cluster = NULL;
    APTR lock = LockCAMD(CD_Linkages);

    while ((cluster = NextCluster(cluster)))
        printf("CAMDProbe:   %s\n", cluster->mcl_Node.ln_Name);
    UnlockCAMD(lock);
}

static int rethink(void)
{
    LONG error;

    puts("CAMDProbe: clusters before RethinkCAMD():");
    list_clusters();
    error = RethinkCAMD();
    printf("CAMDProbe: RethinkCAMD() returned %ld; clusters after:\n", (long)error);
    list_clusters();
    return error ? 10 : 0;
}

int main(int argc, char **argv)
{
    static char name[] = "MIDIHub CAMD Probe";
    static char input[] = "MIDIHub In";
    static char output[] = "MIDIHub Out";
    static char synth_output[] = "MIDIHub Synth";
    struct TagItem node_tags[] = {
        {MIDI_Name, (IPTR)name},
        {MIDI_MsgQueue, 256},
        {MIDI_SysExSize, 4096},
        {TAG_DONE, 0}
    };
    struct TagItem receiver_tags[] = {
        {MLINK_Name, (IPTR)name},
        {MLINK_Location, (IPTR)input},
        {TAG_DONE, 0}
    };
    struct TagItem sender_tags[] = {
        {MLINK_Name, (IPTR)name},
        {MLINK_Location, (IPTR)output},
        {TAG_DONE, 0}
    };
    struct MidiNode *node = NULL;
    struct MidiLink *receiver = NULL;
    struct MidiLink *sender = NULL;
    MidiMsg message;
    int got_on = 0;
    int got_off = 0;
    unsigned int tick;
    int result = 20;
    int synth_mode = argc == 2 && strcmp(argv[1], "--synth") == 0;
    int send_mode = argc == 3 && strcmp(argv[1], "--send") == 0;
    int monitor_mode = (argc == 3 || argc == 4) && strcmp(argv[1], "--monitor") == 0;
    int rethink_mode = argc == 2 && strcmp(argv[1], "--rethink") == 0;

    if (argc != 1 && !synth_mode && !send_mode && !monitor_mode && !rethink_mode) {
        puts("Usage: MIDIHubCAMDProbe [--synth | --send CLUSTER |"
             " --monitor CLUSTER [SECONDS] | --rethink]");
        return 20;
    }
    if (synth_mode)
        sender_tags[1].ti_Data = (IPTR)synth_output;

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) {
        puts("CAMDProbe: camd.library unavailable");
        return 20;
    }
    if (rethink_mode) {
        result = rethink();
        goto cleanup;
    }
    node = CreateMidiA(node_tags);
    if (!node) goto cleanup;
    if (send_mode || monitor_mode) {
        result = send_mode ? send_to(node, argv[2]) :
                 monitor(node, argv[2], argc == 4 ? atol(argv[3]) : 60);
        goto cleanup;
    }
    if (!synth_mode)
        receiver = AddMidiLinkA(node, MLTYPE_Receiver, receiver_tags);
    sender = AddMidiLinkA(node, MLTYPE_Sender, sender_tags);
    if ((!synth_mode && !receiver) || !sender) goto cleanup;
    /* Allow the CAMD links to settle before the first event. */
    Delay(25);
    if (synth_mode) {
        if (!MidiLinkConnected(sender)) {
            puts("CAMDProbe: MIDIHub Synth receiver is not connected");
            goto cleanup;
        }
        puts("CAMDProbe: sending middle C to MIDIHub Synth");
        PutMidi(sender, 0x903c6400UL);
        Delay(25);
        PutMidi(sender, 0x803c0000UL);
        result = 0;
        goto cleanup;
    }
    puts("CAMDProbe: sending Note On to MIDIHub Out");
    PutMidi(sender, 0x903c6400UL);
    Delay(10);
    puts("CAMDProbe: sending Note Off to MIDIHub Out");
    PutMidi(sender, 0x803c0000UL);
    for (tick = 0; tick < 250 && (!got_on || !got_off); ++tick) {
        while (GetMidi(node, &message)) {
            printf("CAMDProbe: received %02x%02x%02x from MIDIHub In\n",
                   (unsigned int)message.mm_Status,
                   (unsigned int)message.mm_Data1,
                   (unsigned int)message.mm_Data2);
            if (message.mm_Status == 0x90 && message.mm_Data1 == 60 &&
                message.mm_Data2 == 100)
                got_on = 1;
            if ((message.mm_Status == 0x80 ||
                 message.mm_Status == 0x90) &&
                message.mm_Data1 == 60 && message.mm_Data2 == 0)
                got_off = 1;
        }
        if (!got_on || !got_off) Delay(1);
    }
    result = got_on && got_off ? 0 : 20;
    printf("CAMDProbe: %s\n", result == 0 ? "round trip passed" :
                                      "round trip failed");

cleanup:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (node) DeleteMidi(node);
    CloseLibrary(CamdBase);
    return result;
}
