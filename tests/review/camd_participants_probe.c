/* Native check: MIDI_PartSignal, MIDI_PartHook and the participant counts.
 * A watcher node links to a cluster; another node's link joins, turns
 * private and leaves. The watcher must be told each time, the changing node
 * never, and mcl_Participants/mcl_PublicParticipants must follow. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <utility/hooks.h>
#include <stdio.h>

struct Library *CamdBase;
static int failures, hookcalls, badargs;
static struct MidiNode *expected_node;
static WORD hook_all,hook_public;
#define CHECK(e, what) do { if (e) printf("PASS %s\n", what); else { printf("FAIL %s\n", what); failures++; } } while (0)

static AROS_UFH3(IPTR, part_hook, AROS_UFHA(struct Hook *, hook, A0),
                 AROS_UFHA(APTR, node, A2), AROS_UFHA(APTR, msg, A1))
{
    AROS_USERFUNC_INIT
    struct MidiCluster *cluster=msg;
    hookcalls++;
    if(!hook || node!=expected_node || !cluster) badargs++;
    if(cluster){hook_all=cluster->mcl_Participants;hook_public=cluster->mcl_PublicParticipants;}
    return 0;
    AROS_USERFUNC_EXIT
}

static void counts(WORD *all, WORD *pub)
{
    struct MidiCluster *c;
    APTR lock = LockCAMD(CD_Linkages);
    c = FindCluster((STRPTR)"partprobe.cluster");
    *all = c ? c->mcl_Participants : -1;
    *pub = c ? c->mcl_PublicParticipants : -1;
    UnlockCAMD(lock);
}

static BOOL signalled(ULONG mask)
{
    return (SetSignal(0, mask) & mask) != 0;
}

int main(void)
{
    struct Hook hook = { { NULL, NULL }, (HOOKFUNC)part_hook, NULL, NULL };
    BYTE bit = AllocSignal(-1), otherbit = AllocSignal(-1);
    ULONG mask = 1UL << bit, othermask = 1UL << otherbit;
    struct MidiNode *watcher, *other;
    struct MidiLink *watch, *link;
    WORD all, pub;

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase || bit < 0 || otherbit < 0) return 20;
    {
        struct TagItem wt[] = { { MIDI_Name, (IPTR)"partprobe watcher" },
            { MIDI_PartSignal, bit }, { MIDI_PartHook, (IPTR)&hook }, { TAG_DONE, 0 } };
        struct TagItem ot[] = { { MIDI_Name, (IPTR)"partprobe other" },
            { MIDI_PartSignal, otherbit }, { TAG_DONE, 0 } };
        struct TagItem lt[] = { { MLINK_Location, (IPTR)"partprobe.cluster" }, { TAG_DONE, 0 } };
        watcher = CreateMidiA(wt);
        expected_node=watcher;
        other = CreateMidiA(ot);
        watch = watcher ? AddMidiLinkA(watcher, MLTYPE_Receiver, lt) : NULL;
        if (!other || !watch) return 20;
        SetSignal(0, mask | othermask);
        hookcalls = 0;
        link = AddMidiLinkA(other, MLTYPE_Sender, lt);
    }
    counts(&all, &pub);
    CHECK(link != NULL, "a second node links to the cluster");
    CHECK(signalled(mask), "the watcher's PartSignal fires when it joins");
    CHECK(hookcalls == 1, "the watcher's PartHook runs once when it joins");
    CHECK(!badargs && hook_all==2 && hook_public==2, "hook arguments and counts match the documented contract");
    CHECK(!signalled(othermask), "the joining node itself is not told");
    CHECK(all == 2 && pub == 2, "two participants, both public");

    hookcalls = 0;
    {
        struct TagItem pt[] = { { MLINK_Private, TRUE }, { TAG_DONE, 0 } };
        SetMidiLinkAttrsA(link, pt);
    }
    counts(&all, &pub);
    CHECK(all == 2 && pub == 1, "a private link is not a public participant");
    CHECK(!badargs && hook_all==2 && hook_public==1, "hook sees visibility counts already updated");
    CHECK(signalled(mask) && hookcalls == 1, "the watcher is told of the change");

    hookcalls = 0;
    RemoveMidiLink(link);
    counts(&all, &pub);
    CHECK(signalled(mask), "the watcher's PartSignal fires when it leaves");
    CHECK(hookcalls == 1, "the watcher's PartHook runs once when it leaves");
    CHECK(all == 1 && pub == 1, "one participant left");

    RemoveMidiLink(watch);
    DeleteMidi(other);
    DeleteMidi(watcher);
    FreeSignal(bit);
    FreeSignal(otherbit);
    CloseLibrary(CamdBase);
    printf("PARTPROBE: %d failures\n", failures);
    return failures ? 20 : 0;
}
