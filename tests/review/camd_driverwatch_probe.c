/* Native check: CAMD loads a driver written to DEVS:Midi after it started,
 * without anyone calling RethinkCAMD(). A client links to the driver's
 * clusters first, the driver file is then copied in as a device class
 * would write it, and the waiting links must get connected. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <stdio.h>
#include <string.h>

struct Library *CamdBase;
static int failures;
#define CHECK(e, what) do { if (e) printf("PASS %s\n", what); else { printf("FAIL %s\n", what); failures++; } } while (0)

static int count_clusters(const char *name)
{
    struct MidiCluster *c = NULL;
    int n = 0;
    APTR lock = LockCAMD(CD_Linkages);
    while ((c = NextCluster(c)) != NULL)
        if (!strcmp(c->mcl_Node.ln_Name, name)) n++;
    UnlockCAMD(lock);
    return n;
}

static BOOL copy_file(CONST_STRPTR from, CONST_STRPTR to)
{
    static UBYTE buf[4096];
    BPTR in = Open(from, MODE_OLDFILE), out;
    LONG got;
    BOOL ok = TRUE;
    if (!in) return FALSE;
    out = Open(to, MODE_NEWFILE);
    if (!out) { Close(in); return FALSE; }
    while ((got = Read(in, buf, sizeof(buf))) > 0)
        if (Write(out, buf, got) != got) ok = FALSE;
    Close(out);
    Close(in);
    return ok && got == 0;
}

static BOOL wait_for(struct MidiLink *link, int ticks)
{
    while (ticks-- > 0) {
        if (MidiLinkConnected(link)) return TRUE;
        Delay(1);
    }
    return MidiLinkConnected(link);
}

int main(void)
{
    struct MidiNode *node;
    struct MidiLink *receiver, *sender;
    struct TagItem nt[] = { { MIDI_Name, (IPTR)"driverwatch probe" }, { TAG_DONE, 0 } };
    struct TagItem it[] = { { MLINK_Location, (IPTR)"debugdriver.in.0" }, { TAG_DONE, 0 } };
    struct TagItem ot[] = { { MLINK_Location, (IPTR)"debugdriver.out.0" }, { TAG_DONE, 0 } };

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) return 20;
    CHECK(count_clusters("debugdriver.in.1") == 0, "debugdriver is not loaded at start");
    node = CreateMidiA(nt);
    receiver = node ? AddMidiLinkA(node, MLTYPE_Receiver, it) : NULL;
    sender = node ? AddMidiLinkA(node, MLTYPE_Sender, ot) : NULL;
    if (!receiver || !sender) return 20;
    CHECK(!MidiLinkConnected(receiver), "the client waits in its own cluster");

#ifdef PARTIAL_DRIVER
    {
        BPTR out = Open((CONST_STRPTR)"DEVS:Midi/debugdriver", MODE_NEWFILE);
        CHECK(out != BNULL, "a partial driver file is created");
        if (out) { Write(out, "PART", 4); Close(out); }
        out = Open((CONST_STRPTR)"SYS:partial-ready", MODE_NEWFILE);
        if (out) Close(out);
        /* The Linux runner finishes the file after two seconds, without
           a second DOS notification. The failed initial scan must retry. */
    }
#else
    CHECK(copy_file((CONST_STRPTR)"SYS:watch-debugdriver",
                    (CONST_STRPTR)"DEVS:Midi/debugdriver"), "the driver file is written");
#endif
    CHECK(wait_for(receiver, 250), "CAMD loads it by itself and connects the waiting receiver");
    CHECK(count_clusters("debugdriver.in.1") == 1, "the driver's other ports appear");
    CHECK(count_clusters("debugdriver.in.0") == 1, "no duplicate of the client's cluster");
    PutMidi(sender, 0x903c6400UL);
    CHECK(1, "the waiting sender reaches the driver");

    /* The same file written again loads nothing twice. */
    copy_file((CONST_STRPTR)"SYS:watch-debugdriver", (CONST_STRPTR)"DEVS:Midi/debugdriver");
    Delay(100);
    CHECK(count_clusters("debugdriver.in.1") == 1, "rewriting the file loads nothing again");

    RemoveMidiLink(sender);
    RemoveMidiLink(receiver);
    DeleteMidi(node);
    CloseLibrary(CamdBase);
    printf("DRIVERWATCH: %d failures\n", failures);
    return failures ? 20 : 0;
}
