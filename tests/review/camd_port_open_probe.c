/* Native check: a driver port must stay open while a receiver is linked.
 * Uses the test-only DEVS:Midi/portwatch driver, which logs OpenPort and
 * ClosePort to the serial log; each step here is logged there too, so the
 * order shows whether the port closed under the receiver. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <stdio.h>

struct Library *CamdBase;
extern void kprintf(char *fmt, ...);

int main(void)
{
    struct MidiNode *node;
    struct MidiLink *out, *in;
    struct TagItem nodetags[] = { { MIDI_Name, (IPTR)"PortOpenProbe" }, { TAG_DONE, 0 } };
    struct TagItem outtags[] = { { MLINK_Location, (IPTR)"portwatch.out.0" }, { TAG_DONE, 0 } };
    struct TagItem intags[] = { { MLINK_Location, (IPTR)"portwatch.in.0" }, { TAG_DONE, 0 } };

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) { puts("PORTOPEN: no camd.library"); return 20; }
    node = CreateMidiA(nodetags);
    if (!node) { CloseLibrary(CamdBase); return 20; }
    out = AddMidiLinkA(node, MLTYPE_Sender, outtags);
    kprintf("PORTWATCH-STEP: sender linked %s\n", out ? "yes" : "no");
    in = AddMidiLinkA(node, MLTYPE_Receiver, intags);
    kprintf("PORTWATCH-STEP: receiver linked %s\n", in ? "yes" : "no");
    if (out) RemoveMidiLink(out);
    kprintf("PORTWATCH-STEP: sender removed\n");
    if (in) RemoveMidiLink(in);
    kprintf("PORTWATCH-STEP: receiver removed\n");
    DeleteMidi(node);
    CloseLibrary(CamdBase);
    puts(out && in ? "PORTOPEN: done" : "PORTOPEN: link failed");
    return out && in ? 0 : 20;
}
