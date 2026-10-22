/* Bisects the Avail FLUSH stall: each letter of argv[1] is one action.
 * R: receiver link to a cluster no driver has; S: sender link likewise;
 * L: LockCAMD + NextCluster walk; P: PutMidi on the sender; W: wait 5 s;
 * C: MidiLinkConnected on both links; D: use debugdriver.* names;
 * F: Open() a file that is not there; O: write a file in DEVS:Midi. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <stdio.h>
#include <string.h>

struct Library *CamdBase;

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    struct TagItem nt[] = { { MIDI_Name, (IPTR)"stall probe" }, { TAG_DONE, 0 } };
    struct TagItem it[] = { { MLINK_Location, (IPTR)"nodriver.in.0" }, { TAG_DONE, 0 } };
    struct TagItem ot[] = { { MLINK_Location, (IPTR)"nodriver.out.0" }, { TAG_DONE, 0 } };
    struct MidiNode *node;
    struct MidiLink *r = NULL, *s = NULL;

    if (strchr(what, 'D')) { it[0].ti_Data = (IPTR)"debugdriver.in.0"; ot[0].ti_Data = (IPTR)"debugdriver.out.0"; }
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) return 20;
    node = CreateMidiA(nt);
    if (!node) return 20;
    if (strchr(what, 'R')) r = AddMidiLinkA(node, MLTYPE_Receiver, it);
    if (strchr(what, 'S')) s = AddMidiLinkA(node, MLTYPE_Sender, ot);
    if (strchr(what, 'L')) {
        struct MidiCluster *c = NULL;
        APTR lock = LockCAMD(CD_Linkages);
        while ((c = NextCluster(c)) != NULL) ;
        UnlockCAMD(lock);
    }
    if (strchr(what, 'P') && s) PutMidi(s, 0x903c6400UL);
    if (strchr(what, 'C')) { int i; for (i = 0; i < 50; i++) { if (r) MidiLinkConnected(r); if (s) MidiLinkConnected(s); Delay(1); } }
    if (strchr(what, 'W')) Delay(250);
    if (strchr(what, 'F')) { BPTR f = Open((CONST_STRPTR)"SYS:not-there", MODE_OLDFILE); if (f) Close(f); }
    if (strchr(what, 'O')) { BPTR f = Open((CONST_STRPTR)"DEVS:Midi/stallfile", MODE_NEWFILE); if (f) { Write(f, "x", 1); Close(f); } }
    if (s) RemoveMidiLink(s);
    if (r) RemoveMidiLink(r);
    DeleteMidi(node);
    CloseLibrary(CamdBase);
    printf("stall probe %s done\n", what);
    return 0;
}
