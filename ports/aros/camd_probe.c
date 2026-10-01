/* Native CAMD round-trip diagnostic for the MIDIHub In/Out clusters. */
#include <exec/libraries.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>

struct Library *CamdBase;

int main(void)
{
    static char name[] = "MIDIHub CAMD Probe";
    static char input[] = "MIDIHub In";
    static char output[] = "MIDIHub Out";
    struct TagItem node_tags[] = {
        {MIDI_Name, (IPTR)name},
        {MIDI_MsgQueue, 64},
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

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) {
        puts("CAMDProbe: camd.library unavailable");
        return 20;
    }
    node = CreateMidiA(node_tags);
    if (!node) goto cleanup;
    receiver = AddMidiLinkA(node, MLTYPE_Receiver, receiver_tags);
    sender = AddMidiLinkA(node, MLTYPE_Sender, sender_tags);
    if (!receiver || !sender) goto cleanup;
    /* Allow the CAMD links to settle before the first event. */
    Delay(25);
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
