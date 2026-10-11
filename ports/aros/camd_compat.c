/* CAMD compatibility suite.

   Checks the behaviour of camd.library 41.1 that existing programs rely on,
   so a changed camd.library can be run against it before it is released.
   Run it against the original library and the changed one: a test that
   passes on the original must pass on the changed one too.

   MIDIHubCAMDCompat            the contract of the 41.1 API
   MIDIHubCAMDCompat --v42      what camd.library 42 adds: participant
                                notification, GetClusterAttrsA(),
                                CamdTime(), MIDI_SystemClock and cluster
                                watches; skipped before 42
   MIDIHubCAMDCompat --v43      the provisional endpoint functions of
                                camd.library 43: snapshots and native
                                MIDI 1.0 output sessions; skipped before 43
   MIDIHubCAMDCompat --v43-leak exits with a snapshot, a session and a
                                published endpoint left behind; closing
                                camd.library releases them, so the runner's
                                memory flush expunges the library
   MIDIHubCAMDCompat --v43-abandon
                                the same without closing camd.library, as a
                                task that just ends: the library then stays
   MIDIHubCAMDCompat --router SOURCE DESTINATION
                                how long MIDIHubRouter, routing SOURCE to
                                DESTINATION, takes to forward a message
   MIDIHubCAMDCompat --rethink  RethinkCAMD() loading DEVS:Midi/debugdriver
                                at run time; needs the driver moved to
                                SYS:camdcompat-debugdriver before CAMD starts
                                (tools/camd-compat-hosted.sh does it)

   Prints one PASS, FAIL or SKIP line per check and a summary; returns 0
   when nothing failed, 5 otherwise. */
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/hooks.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct Library *CamdBase;

static int passed, failed, skipped;

static void check(int ok, const char *name, const char *detail)
{
    if (ok) {
        passed++;
        printf("PASS %s\n", name);
    } else {
        failed++;
        printf("FAIL %s%s%s\n", name, detail ? ": " : "", detail ? detail : "");
    }
}

#define RUN(test) (printf("-- %s\n", #test), test())

static void skip(const char *name, const char *why)
{
    skipped++;
    printf("SKIP %s: %s\n", name, why);
}

static struct MidiNode *new_node(char *name, ULONG queue, ULONG sysex)
{
    struct TagItem tags[] = {
        {MIDI_Name, (IPTR)name},
        {MIDI_MsgQueue, queue},
        {MIDI_SysExSize, sysex},
        {TAG_DONE, 0}
    };
    return CreateMidiA(tags);
}

static struct MidiLink *link_to(struct MidiNode *node, LONG type, char *cluster)
{
    struct TagItem tags[] = {
        {MLINK_Location, (IPTR)cluster},
        {TAG_DONE, 0}
    };
    return AddMidiLinkA(node, type, tags);
}

static int drain(struct MidiNode *node)
{
    MidiMsg msg;
    int count = 0;

    while (GetMidi(node, &msg)) {
        if (msg.mm_Status == 0xf0)
            SkipSysEx(node);
        count++;
    }
    return count;
}

static int count_clusters(const char *name)
{
    struct MidiCluster *cluster = NULL;
    APTR lock = LockCAMD(CD_Linkages);
    int count = 0;

    while ((cluster = NextCluster(cluster)))
        if (!strcmp(cluster->mcl_Node.ln_Name, name))
            count++;
    UnlockCAMD(lock);
    return count;
}

static void test_nodes(void)
{
    static char name[] = "camdcompat node";
    struct MidiNode *node = new_node(name, 32, 256);
    struct MidiNode *scan;
    IPTR got_name = 0, queue = 0, sysex = 0, part = 0, stamp = 0;
    struct TagItem query[] = {
        {MIDI_Name, (IPTR)&got_name},
        {MIDI_MsgQueue, (IPTR)&queue},
        {MIDI_SysExSize, (IPTR)&sysex},
        {MIDI_PartSignal, (IPTR)&part},
        {MIDI_TimeStamp, (IPTR)&stamp},
        {TAG_USER + 0x7fff, 0},
        {TAG_DONE, 0}
    };
    ULONG counted;
    int found = 0;

    check(node != NULL, "CreateMidiA", NULL);
    if (!node)
        return;
    counted = GetMidiAttrsA(node, query);
    check(counted == 5, "GetMidiAttrsA counts known tags only", NULL);
    check((char *)got_name == name, "MIDI_Name is the caller's pointer", NULL);
    check(queue == 32 && sysex == 256, "MIDI_MsgQueue and MIDI_SysExSize", NULL);
    check((LONG)part == -1, "MIDI_PartSignal defaults to -1", NULL);
    check(stamp != 0, "MIDI_TimeStamp defaults to a valid pointer", NULL);
    check(FindMidi(name) == node, "FindMidi", NULL);
    for (scan = NextMidi(NULL); scan; scan = NextMidi(scan))
        if (scan == node)
            found = 1;
    check(found, "NextMidi enumerates the node", NULL);
    DeleteMidi(node);
    check(FindMidi(name) == NULL, "DeleteMidi removes the node", NULL);
}

static void test_links(void)
{
    static char cluster_a[] = "camdcompat.A";
    static char cluster_b[] = "camdcompat.B";
    static char link_name[] = "camdcompat link";
    struct MidiNode *node = new_node("camdcompat links", 32, 256);
    struct MidiLink *receiver, *sender;
    IPTR name = 0, location = 0, chmask = 0, evmask = 0, pri = 99, comment = 0;
    struct TagItem query[] = {
        {MLINK_Name, (IPTR)&name},
        {MLINK_Location, (IPTR)&location},
        {MLINK_ChannelMask, (IPTR)&chmask},
        {MLINK_EventMask, (IPTR)&evmask},
        {MLINK_Priority, (IPTR)&pri},
        {MLINK_Comment, (IPTR)&comment},
        {TAG_DONE, 0}
    };
    struct TagItem rename[] = {
        {MLINK_Name, (IPTR)link_name},
        {TAG_DONE, 0}
    };
    struct TagItem move[] = {
        {MLINK_Location, (IPTR)cluster_b},
        {TAG_DONE, 0}
    };
    struct MidiCluster *cluster, *scan;
    ULONG counted;
    int found = 0;

    if (!node) {
        check(0, "links: CreateMidiA", NULL);
        return;
    }
    receiver = link_to(node, MLTYPE_Receiver, cluster_a);
    check(receiver != NULL, "AddMidiLinkA receiver creates a cluster", NULL);
    if (!receiver)
        goto out;
    cluster = FindCluster(cluster_a);
    check(cluster != NULL, "FindCluster", NULL);
    for (scan = NextCluster(NULL); scan; scan = NextCluster(scan))
        if (scan == cluster)
            found = 1;
    check(found, "NextCluster enumerates the cluster", NULL);
    check(!MidiLinkConnected(receiver), "MidiLinkConnected: receiver alone", NULL);

    SetMidiLinkAttrsA(receiver, rename);
    counted = GetMidiLinkAttrsA(receiver, query);
    check((char *)name == link_name, "MLINK_Name is the caller's pointer", NULL);
    check(location && !strcmp((char *)location, cluster_a), "MLINK_Location", NULL);
    check(chmask == 0xffff && (ULONG)evmask == 0xffffffffUL,
          "channel and event masks default to all", NULL);
    check(pri == 0, "MLINK_Priority defaults to 0", NULL);
    if (CamdBase->lib_Version >= 42) {
        check(counted == 6, "GetMidiLinkAttrsA counts MLINK_Comment", NULL);
        check(comment == 0, "MLINK_Comment is NULL without a comment", NULL);
    } else {
        check(counted == 5, "41.1 does not count MLINK_Comment", NULL);
    }

    sender = link_to(node, MLTYPE_Sender, cluster_a);
    check(sender && MidiLinkConnected(sender) && MidiLinkConnected(receiver),
          "MidiLinkConnected: sender and receiver", NULL);
    if (sender)
        RemoveMidiLink(sender);

    SetMidiLinkAttrsA(receiver, move);
    check(FindCluster(cluster_b) != NULL && FindCluster(cluster_a) == NULL,
          "MLINK_Location moves a link; the empty cluster goes", NULL);
    RemoveMidiLink(receiver);
    check(FindCluster(cluster_b) == NULL, "RemoveMidiLink removes the empty cluster", NULL);
out:
    DeleteMidi(node);
}

static void test_messages(void)
{
    static char cluster[] = "camdcompat.msg";
    static ULONG clock = 12345;
    struct TagItem stamp_tags[] = {
        {MIDI_TimeStamp, (IPTR)&clock},
        {TAG_DONE, 0}
    };
    struct TagItem ch2_tags[] = {
        {MLINK_ChannelMask, 1 << 1},
        {TAG_DONE, 0}
    };
    struct TagItem note_tags[] = {
        {MLINK_EventMask, CMF_Note},
        {TAG_DONE, 0}
    };
    struct MidiNode *out = new_node("camdcompat out", 32, 256);
    struct MidiNode *in = new_node("camdcompat in", 32, 256);
    struct MidiNode *in2 = new_node("camdcompat in2", 32, 256);
    struct MidiLink *sender = NULL, *receiver = NULL, *receiver2 = NULL;
    MidiMsg msg;
    int i, ok;

    if (!out || !in || !in2) {
        check(0, "messages: CreateMidiA", NULL);
        goto out;
    }
    /* A message is stamped with the receiving node's clock. */
    SetMidiAttrsA(in, stamp_tags);
    sender = link_to(out, MLTYPE_Sender, cluster);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    receiver2 = link_to(in2, MLTYPE_Receiver, cluster);
    if (!sender || !receiver || !receiver2) {
        check(0, "messages: AddMidiLinkA", NULL);
        goto out;
    }

    PutMidi(sender, 0x903c6400UL);
    ok = GetMidi(in, &msg);
    check(ok && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x3c && msg.mm_Data2 == 0x64,
          "PutMidi reaches a receiver", NULL);
    check(ok && msg.mm_Time == 12345, "the receiver's MIDI_TimeStamp stamps the message", NULL);
    check(GetMidi(in2, &msg) && msg.mm_Status == 0x90, "every receiver gets it", NULL);
    check(!GetMidi(in, &msg), "GetMidi is empty afterwards", NULL);

    for (i = 0; i < 10; i++)
        PutMidi(sender, 0x90000000UL | ((ULONG)(40 + i) << 16) | (100UL << 8));
    ok = 1;
    for (i = 0; i < 10; i++)
        if (!GetMidi(in, &msg) || msg.mm_Data1 != 40 + i)
            ok = 0;
    check(ok, "messages arrive in order", NULL);
    drain(in2);

    SetMidiLinkAttrsA(receiver, ch2_tags);
    PutMidi(sender, 0x903c6400UL);
    PutMidi(sender, 0x913c6400UL);
    ok = GetMidi(in, &msg) && msg.mm_Status == 0x91 && !GetMidi(in, &msg);
    check(ok, "MLINK_ChannelMask filters channels", NULL);
    drain(in2);

    ch2_tags[0].ti_Data = 0xffff;
    SetMidiLinkAttrsA(receiver, ch2_tags);
    SetMidiLinkAttrsA(receiver, note_tags);
    PutMidi(sender, 0xc0050000UL);
    PutMidi(sender, 0x903c6400UL);
    ok = GetMidi(in, &msg) && msg.mm_Status == 0x90 && !GetMidi(in, &msg);
    check(ok, "MLINK_EventMask filters message types", NULL);
    drain(in2);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (receiver2) RemoveMidiLink(receiver2);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
    if (in2) DeleteMidi(in2);
}

static void test_sysex(void)
{
    static char cluster[] = "camdcompat.sysex";
    static UBYTE identity[] = {0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};
    static UBYTE exact[] = {0xf0, 0x01, 0x02, 0xf7};
    UBYTE buffer[16];
    struct MidiNode *out = new_node("camdcompat sysex out", 32, 256);
    struct MidiNode *in = new_node("camdcompat sysex in", 32, 256);
    struct MidiNode *small = new_node("camdcompat sysex small", 32, 4);
    struct MidiLink *sender = NULL, *receiver = NULL, *small_receiver = NULL;
    MidiMsg msg;
    ULONG length;

    if (!out || !in || !small) {
        check(0, "sysex: CreateMidiA", NULL);
        goto out;
    }
    sender = link_to(out, MLTYPE_Sender, cluster);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    small_receiver = link_to(small, MLTYPE_Receiver, cluster);
    if (!sender || !receiver || !small_receiver) {
        check(0, "sysex: AddMidiLinkA", NULL);
        goto out;
    }
    PutSysEx(sender, identity);
    check(GetMidi(in, &msg) && msg.mm_Status == 0xf0, "PutSysEx delivers an F0 message", NULL);
    length = QuerySysEx(in);
    check(length == sizeof(identity), "QuerySysEx gives the length", NULL);
    memset(buffer, 0, sizeof(buffer));
    check(GetSysEx(in, buffer, sizeof(buffer)) == sizeof(identity) &&
          !memcmp(buffer, identity, sizeof(identity)), "GetSysEx copies the message", NULL);

    PutSysEx(sender, identity);
    PutMidi(sender, 0x903c6400UL);
    GetMidi(in, &msg);
    SkipSysEx(in);
    check(GetMidi(in, &msg) && msg.mm_Status == 0x90, "SkipSysEx skips to the next message", NULL);

    GetMidiErr(small);
    drain(small);
    PutSysEx(sender, exact);
    check(GetMidi(small, &msg) && msg.mm_Status == 0xf0 && QuerySysEx(small) == sizeof(exact),
          "SysEx exactly matching MIDI_SysExSize fits", NULL);
    GetSysEx(small, buffer, sizeof(buffer));

    PutSysEx(sender, identity);
    check((GetMidiErr(small) & (CMEF_SysExTooBig | CMEF_SysExFull)) == CMEF_SysExTooBig,
          "oversized SysEx reports CMEF_SysExTooBig, not CMEF_SysExFull", NULL);
    check(!GetMidi(small, &msg), "oversized SysEx leaves no partial message", NULL);
    PutMidi(sender, 0x903c6400UL);
    check(GetMidi(small, &msg) && msg.mm_Status == 0x90,
          "a normal message arrives after oversized SysEx", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (small_receiver) RemoveMidiLink(small_receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
    if (small) DeleteMidi(small);
}

static void test_errors(void)
{
    static char cluster[] = "camdcompat.err";
    struct MidiNode *out = new_node("camdcompat err out", 32, 256);
    struct MidiNode *in = new_node("camdcompat err in", 8, 256);
    struct MidiLink *sender = NULL, *receiver = NULL;
    MidiMsg msg;
    UBYTE error;
    BYTE bit = AllocSignal(-1);
    int i;

    if (!out || !in) {
        check(0, "errors: CreateMidiA", NULL);
        goto out;
    }
    sender = link_to(out, MLTYPE_Sender, cluster);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    if (!sender || !receiver) {
        check(0, "errors: AddMidiLinkA", NULL);
        goto out;
    }
    for (i = 0; i < 20; i++)
        PutMidi(sender, 0x903c6400UL);
    /* WaitMidi() must return FALSE at once, not wait, while an error is
       pending. */
    check(!WaitMidi(in, &msg), "WaitMidi returns FALSE with an error pending", NULL);
    error = GetMidiErr(in);
    check(error & CMEF_BufferFull, "a full queue sets CMEF_BufferFull", NULL);
    check(GetMidiErr(in) == 0, "GetMidiErr clears the error", NULL);
    drain(in);

    if (bit < 0) {
        check(0, "MIDI_ErrFilter: signal", NULL);
    } else {
        struct TagItem filter_tags[] = {
            {MIDI_ErrFilter, CMEF_BufferFull},
            {MIDI_RecvSignal, (IPTR)bit},
            {TAG_DONE, 0}
        };
        ULONG mask = 1UL << bit;

        SetMidiAttrsA(in, filter_tags);
        SetSignal(0, mask);
        for (i = 0; i < 20; i++)
            PutMidi(sender, 0x903c6400UL);
        check(WaitMidi(in, &msg), "MIDI_ErrFilter hides an error from WaitMidi", NULL);
        check(GetMidiErr(in) == 0, "MIDI_ErrFilter hides an error from GetMidiErr", NULL);
        drain(in);
    }
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
    if (bit >= 0) FreeSignal(bit);
}

static void test_parse(void)
{
    static char cluster[] = "camdcompat.parse";
    static UBYTE bytes[] = {0x90, 0x3c, 0x64, 0x3e, 0x64, 0xc1, 0x05};
    static UBYTE oversized[] = {0xf0, 0x01, 0x02, 0x03, 0x04, 0xf7};
    static UBYTE note[] = {0x90, 0x3c, 0x64};
    struct TagItem parse_tags[] = {
        {MLINK_Location, (IPTR)cluster},
        {MLINK_Parse, TRUE},
        {TAG_DONE, 0}
    };
    struct MidiNode *out = new_node("camdcompat parse out", 32, 256);
    struct MidiNode *in = new_node("camdcompat parse in", 32, 256);
    struct MidiNode *small = new_node("camdcompat parse small", 32, 4);
    struct MidiLink *sender = NULL, *receiver = NULL, *small_receiver = NULL;
    MidiMsg msg;
    int ok;

    if (!out || !in || !small) {
        check(0, "parse: CreateMidiA", NULL);
        goto out;
    }
    sender = AddMidiLinkA(out, MLTYPE_Sender, parse_tags);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    small_receiver = link_to(small, MLTYPE_Receiver, cluster);
    if (!sender || !receiver || !small_receiver) {
        check(0, "parse: AddMidiLinkA", NULL);
        goto out;
    }
    ParseMidi(sender, bytes, sizeof(bytes));
    ok = GetMidi(in, &msg) && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x3c;
    ok = ok && GetMidi(in, &msg) && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x3e;
    ok = ok && GetMidi(in, &msg) && msg.mm_Status == 0xc1 && msg.mm_Data1 == 0x05;
    check(ok, "ParseMidi with running status", NULL);
    drain(small);

    ParseMidi(sender, oversized, sizeof(oversized));
    check((GetMidiErr(small) & (CMEF_SysExTooBig | CMEF_SysExFull)) == CMEF_SysExTooBig,
          "byte-wise oversized SysEx reports CMEF_SysExTooBig", NULL);
    check(!GetMidi(small, &msg), "byte-wise oversized SysEx leaves no partial message", NULL);
    ParseMidi(sender, note, sizeof(note));
    check(GetMidi(small, &msg) && msg.mm_Status == 0x90,
          "byte-wise input recovers after oversized SysEx", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (small_receiver) RemoveMidiLink(small_receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
    if (small) DeleteMidi(small);
}

static void test_msgtypes(void)
{
    static const struct { ULONG msg; WORD len; } lengths[] = {
        {0x90000000UL, 3}, {0x80000000UL, 3}, {0xb0000000UL, 3},
        {0xc0000000UL, 2}, {0xd0000000UL, 2}, {0xe0000000UL, 3},
        {0xf1000000UL, 2}, {0xf2000000UL, 3}, {0xf3000000UL, 2},
        {0xf6000000UL, 1}, {0xf8000000UL, 1}, {0xf0000000UL, 0},
        {0x40000000UL, 0}
    };
    MidiMsg msg;
    unsigned int i;
    int ok = 1;

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
        if (MidiMsgLen(lengths[i].msg) != lengths[i].len) {
            printf("     MidiMsgLen(%08lx) = %d\n", (unsigned long)lengths[i].msg,
                   (int)MidiMsgLen(lengths[i].msg));
            ok = 0;
        }
    check(ok, "MidiMsgLen", NULL);

    msg.mm_Msg = 0x903c6400UL;
    ok = MidiMsgType(&msg) == CMB_Note;
    msg.mm_Msg = 0xc0050000UL;
    ok = ok && MidiMsgType(&msg) == CMB_Prog;
    msg.mm_Msg = 0xe0000000UL;
    ok = ok && MidiMsgType(&msg) == CMB_PitchBend;
    msg.mm_Msg = 0xf8000000UL;
    ok = ok && MidiMsgType(&msg) == CMB_RealTime;
    msg.mm_Msg = 0xf0000000UL;
    ok = ok && MidiMsgType(&msg) == -1;
    check(ok, "MidiMsgType", NULL);
}

static void test_notify(void)
{
    static char cluster[] = "camdcompat.notify";
    struct ClusterNotifyNode cnn;
    struct MidiNode *node = new_node("camdcompat notify", 32, 256);
    struct MidiLink *first = NULL, *second = NULL;
    BYTE bit = AllocSignal(-1);
    ULONG mask;

    if (!node || bit < 0) {
        check(0, "notify: setup", NULL);
        goto out;
    }
    mask = 1UL << bit;
    memset(&cnn, 0, sizeof(cnn));
    cnn.cnn_Task = FindTask(NULL);
    cnn.cnn_SigBit = bit;
    SetSignal(0, mask);
    StartClusterNotify(&cnn);

    first = link_to(node, MLTYPE_Receiver, cluster);
    check(SetSignal(0, mask) & mask, "ClusterNotify signals a new cluster", NULL);
    second = link_to(node, MLTYPE_Sender, cluster);
    /* 41.1 signals only clusters coming and going. */
    check(!(SetSignal(0, mask) & mask),
          "ClusterNotify does not signal a link joining a cluster", NULL);
    if (second) RemoveMidiLink(second);
    second = NULL;
    check(!(SetSignal(0, mask) & mask),
          "ClusterNotify does not signal a link leaving a cluster", NULL);
    if (first) RemoveMidiLink(first);
    first = NULL;
    check(SetSignal(0, mask) & mask, "ClusterNotify signals a cluster going", NULL);

    EndClusterNotify(&cnn);
    first = link_to(node, MLTYPE_Receiver, cluster);
    check(!(SetSignal(0, mask) & mask), "EndClusterNotify stops signals", NULL);
out:
    if (second) RemoveMidiLink(second);
    if (first) RemoveMidiLink(first);
    if (node) DeleteMidi(node);
    if (bit >= 0) FreeSignal(bit);
}

/* A program may start and end cluster notification while it holds
   LockCAMD(). Run it in a process of its own so that a deadlock fails the
   check instead of hanging the suite. */
static struct Task *locked_parent;
static volatile int locked_done;

static void locked_notify(void)
{
    struct ClusterNotifyNode cnn;
    APTR lock;

    memset(&cnn, 0, sizeof(cnn));
    cnn.cnn_Task = FindTask(NULL);
    cnn.cnn_SigBit = SIGBREAKB_CTRL_F;
    lock = LockCAMD(CD_Linkages);
    StartClusterNotify(&cnn);
    EndClusterNotify(&cnn);
    UnlockCAMD(lock);
    locked_done = 1;
    Signal(locked_parent, SIGBREAKF_CTRL_E);
}

static void test_notify_locked(void)
{
    struct TagItem tags[] = {
        {NP_Entry, (IPTR)locked_notify},
        {NP_Name, (IPTR)"camdcompat locked notify"},
        {TAG_DONE, 0}
    };
    int ticks;

    locked_parent = FindTask(NULL);
    locked_done = 0;
    SetSignal(0, SIGBREAKF_CTRL_E);
    if (!CreateNewProc(tags)) {
        check(0, "StartClusterNotify under LockCAMD", "no process");
        return;
    }
    for (ticks = 0; ticks < 150 && !locked_done; ticks++)
        Delay(1);
    check(locked_done, "Start/EndClusterNotify while holding LockCAMD",
          locked_done ? NULL : "deadlock (the process is left waiting)");
}

static void test_driver(void)
{
    static char out_cluster[] = "debugdriver.out.0";
    static char in_cluster[] = "debugdriver.in.0";
    struct MidiNode *node;
    struct MidiLink *sender, *receiver;

    if (!FindCluster(out_cluster)) {
        skip("debugdriver ports", "no DEVS:Midi/debugdriver");
        return;
    }
    node = new_node("camdcompat driver", 32, 256);
    if (!node) {
        check(0, "driver: CreateMidiA", NULL);
        return;
    }
    check(count_clusters("debugdriver.in.3") == 1, "a driver's ports have their clusters", NULL);
    sender = link_to(node, MLTYPE_Sender, out_cluster);
    check(sender && MidiLinkConnected(sender), "a sender to a driver port opens it", NULL);
    if (sender) {
        PutMidi(sender, 0x903c6400UL);
        PutMidi(sender, 0x803c4000UL);
        check(1, "PutMidi to a driver port returns", NULL);
    }
    receiver = link_to(node, MLTYPE_Receiver, in_cluster);
    check(receiver && MidiLinkConnected(receiver), "a receiver on a driver port is connected", NULL);
    if (receiver) RemoveMidiLink(receiver);
    if (sender) RemoveMidiLink(sender);
    check(FindCluster(out_cluster) != NULL, "a driver's cluster outlives its links", NULL);
    DeleteMidi(node);
}

/* A driver hands over what it receives in its own process, so wait a little. */
static int get_soon(struct MidiNode *node, MidiMsg *msg)
{
    int tries;

    for (tries = 0; tries < 100; tries++) {
        if (GetMidi(node, msg)) return 1;
        Delay(1);
    }
    return 0;
}

/* The loopback driver: what goes to loopback.out.<n> comes from loopback.in.<n>. */
static void test_loopback(void)
{
    static char out_cluster[] = "loopback.out.0";
    static char in_cluster[] = "loopback.in.0";
    static char other_in[] = "loopback.in.1";
    static UBYTE identity[] = {0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};
    UBYTE buffer[16];
    struct MidiNode *out, *in, *other;
    struct MidiLink *sender = NULL, *receiver = NULL, *other_receiver = NULL;
    MidiMsg msg;

    if (!FindCluster(out_cluster)) {
        skip("loopback ports", "no DEVS:Midi/loopback");
        return;
    }
    out = new_node("camdcompat loop out", 32, 256);
    in = new_node("camdcompat loop in", 32, 256);
    other = new_node("camdcompat loop other", 32, 256);
    if (!out || !in || !other) {
        check(0, "loopback: CreateMidiA", NULL);
        goto out;
    }
    receiver = link_to(in, MLTYPE_Receiver, in_cluster);
    other_receiver = link_to(other, MLTYPE_Receiver, other_in);
    sender = link_to(out, MLTYPE_Sender, out_cluster);
    check(sender && receiver && other_receiver, "links to a loopback port", NULL);
    if (!sender || !receiver || !other_receiver) goto out;

    PutMidi(sender, 0x903c6400UL);
    check(get_soon(in, &msg) && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x3c && msg.mm_Data2 == 0x64,
          "a message sent to a driver port is received from it", NULL);
    PutMidi(sender, 0x803c4000UL);
    PutMidi(sender, 0xc0050000UL);
    check(get_soon(in, &msg) && msg.mm_Status == 0x80 && get_soon(in, &msg) && msg.mm_Status == 0xc0 &&
          msg.mm_Data1 == 0x05, "driver input keeps its order", NULL);
    PutSysEx(sender, identity);
    memset(buffer, 0, sizeof(buffer));
    check(get_soon(in, &msg) && msg.mm_Status == 0xf0 && QuerySysEx(in) == sizeof(identity) &&
          GetSysEx(in, buffer, sizeof(buffer)) == sizeof(identity) && !memcmp(buffer, identity, sizeof(identity)),
          "SysEx sent to a driver port is received from it", NULL);
    Delay(5);
    check(!GetMidi(other, &msg), "another port of the driver receives nothing", NULL);

    /* More realtime messages than the port's realtime ring holds at once. */
    {
        int i, seen = 0;

        for (i = 0; i < 2000; i++) {
            PutMidi(sender, 0xf8000000UL);
            while (GetMidi(in, &msg)) seen++;
        }
        Delay(5);
        while (GetMidi(in, &msg)) seen++;
        GetMidiErr(in);
        PutMidi(sender, 0x90404000UL);
        check(seen == 2000 && get_soon(in, &msg) && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x40,
              "two thousand realtime messages pass a driver port and leave it working", NULL);
    }

    /* A burst the port's receive ring cannot hold: the sender outranks the
       port's receiver process, so nothing is read while it arrives. */
    {
        static UBYTE burst[3000];
        BYTE old;
        int i;

        burst[0] = 0xf0;
        for (i = 1; i < (int)sizeof(burst) - 1; i++) burst[i] = 0x11;
        burst[sizeof(burst) - 1] = 0xf7;
        GetMidiErr(in);
        old = SetTaskPri(FindTask(NULL), 40);
        PutSysEx(sender, burst);
        SetTaskPri(FindTask(NULL), old);
        Delay(10);
        while (GetMidi(in, &msg)) if (msg.mm_Status == 0xf0) SkipSysEx(in);
        check(GetMidiErr(in) & CMEF_RecvOverflow, "a burst past the receive ring is reported as CMEF_RecvOverflow", NULL);
        PutMidi(sender, 0x90414000UL);
        check(get_soon(in, &msg) && msg.mm_Status == 0x90 && msg.mm_Data1 == 0x41,
              "and the port receives correctly afterwards", NULL);
    }
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (other_receiver) RemoveMidiLink(other_receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
    if (other) DeleteMidi(other);
}

static void test_rethink_stub(void)
{
    LONG result = RethinkCAMD();
    check(result == 0, "RethinkCAMD returns 0", NULL);
}

/* camd.library 42 */

static struct MidiNode *part_node(char *name, BYTE bit)
{
    struct TagItem tags[] = {
        {MIDI_Name, (IPTR)name},
        {MIDI_MsgQueue, 32},
        {MIDI_SysExSize, 256},
        {MIDI_PartSignal, (IPTR)bit},
        {TAG_DONE, 0}
    };
    return CreateMidiA(tags);
}

static ULONG cluster_attr(char *cluster, ULONG tag)
{
    IPTR value = 0;
    struct TagItem query[] = {
        {tag, (IPTR)&value},
        {TAG_DONE, 0}
    };
    APTR lock = LockCAMD(CD_Linkages);
    struct MidiCluster *found = FindCluster(cluster);

    if (found)
        GetClusterAttrsA(found, query);
    UnlockCAMD(lock);
    return (ULONG)value;
}

static int hook_calls;
static APTR hook_object, hook_message;

AROS_UFH3(IPTR, part_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, message, A1))
{
    AROS_USERFUNC_INIT

    (void)hook;
    hook_calls++;
    hook_object = object;
    hook_message = message;
    return 0;

    AROS_USERFUNC_EXIT
}

static void test_participants(void)
{
    static char cluster[] = "camdcompat.part";
    static char other[] = "camdcompat.part2";
    static struct Hook hook;
    struct TagItem hook_tags[] = {
        {MIDI_PartHook, (IPTR)&hook},
        {TAG_DONE, 0}
    };
    struct TagItem private_tags[] = {
        {MLINK_Location, (IPTR)cluster},
        {MLINK_Private, TRUE},
        {TAG_DONE, 0}
    };
    BYTE abit = AllocSignal(-1), bbit = AllocSignal(-1);
    ULONG amask = 1UL << abit, bmask = 1UL << bbit;
    struct MidiNode *a = NULL, *b = NULL, *c = NULL;
    struct MidiLink *alink = NULL, *alink2 = NULL, *blink = NULL, *clink = NULL, *olink = NULL;
    APTR lock;

    if (abit < 0 || bbit < 0) {
        check(0, "participants: signals", NULL);
        goto out;
    }
    a = part_node("camdcompat part A", abit);
    b = part_node("camdcompat part B", bbit);
    c = new_node("camdcompat part C", 32, 256);
    if (!a || !b || !c) {
        check(0, "participants: CreateMidiA", NULL);
        goto out;
    }
    alink = link_to(a, MLTYPE_Receiver, cluster);
    SetSignal(0, amask | bmask);
    blink = link_to(b, MLTYPE_Sender, cluster);
    check(SetSignal(0, amask) & amask, "MIDI_PartSignal: another node joins", NULL);
    check(!(SetSignal(0, bmask) & bmask), "MIDI_PartSignal: not for the node's own link", NULL);
    check(cluster_attr(cluster, MCLA_Participants) == 2, "MCLA_Participants counts links", NULL);

    alink2 = link_to(a, MLTYPE_Sender, cluster);
    check(SetSignal(0, bmask) & bmask, "MIDI_PartSignal: once per node with two links", NULL);
    check(!(SetSignal(0, amask) & amask), "MIDI_PartSignal: own second link is silent", NULL);

    olink = link_to(b, MLTYPE_Receiver, other);
    check(!(SetSignal(0, amask) & amask),
          "MIDI_PartSignal: not for a cluster the node is not in", NULL);

    hook.h_Entry = (HOOKFUNC)part_hook;
    SetMidiAttrsA(a, hook_tags);
    hook_calls = 0;
    clink = AddMidiLinkA(c, MLTYPE_Receiver, private_tags);
    lock = LockCAMD(CD_Linkages);
    check(hook_calls == 1 && hook_object == a && hook_message == FindCluster(cluster),
          "MIDI_PartHook gets the node and the cluster", NULL);
    UnlockCAMD(lock);
    check(cluster_attr(cluster, MCLA_Participants) == 4 &&
          cluster_attr(cluster, MCLA_PublicParticipants) == 3,
          "MCLA_PublicParticipants leaves out MLINK_Private links", NULL);

    SetSignal(0, amask);
    RemoveMidiLink(blink);
    blink = NULL;
    check(SetSignal(0, amask) & amask, "MIDI_PartSignal: another node leaves", NULL);
    check(cluster_attr(cluster, MCLA_Participants) == 3, "MCLA_Participants follows", NULL);
out:
    if (olink) RemoveMidiLink(olink);
    if (clink) RemoveMidiLink(clink);
    if (blink) RemoveMidiLink(blink);
    if (alink2) RemoveMidiLink(alink2);
    if (alink) RemoveMidiLink(alink);
    if (c) DeleteMidi(c);
    if (b) DeleteMidi(b);
    if (a) DeleteMidi(a);
    if (bbit >= 0) FreeSignal(bbit);
    if (abit >= 0) FreeSignal(abit);
}

static void test_cluster_attrs(void)
{
    static char cluster[] = "camdcompat.attrs";
    struct MidiNode *out = new_node("camdcompat attrs out", 32, 256);
    struct MidiNode *in = new_node("camdcompat attrs in", 8, 256);
    struct MidiLink *sender = NULL, *receiver = NULL;
    IPTR name = 0, comment = 1, hasdriver = 1;
    struct TagItem query[] = {
        {MCLA_Name, (IPTR)&name},
        {MCLA_Comment, (IPTR)&comment},
        {MCLA_HasDriver, (IPTR)&hasdriver},
        {TAG_USER + 0x7fff, 0},
        {TAG_DONE, 0}
    };
    APTR lock;
    ULONG counted = 0;
    int i;

    if (!out || !in) {
        check(0, "attrs: CreateMidiA", NULL);
        goto out;
    }
    sender = link_to(out, MLTYPE_Sender, cluster);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    if (!sender || !receiver) {
        check(0, "attrs: AddMidiLinkA", NULL);
        goto out;
    }
    lock = LockCAMD(CD_Linkages);
    if (FindCluster(cluster))
        counted = GetClusterAttrsA(FindCluster(cluster), query);
    UnlockCAMD(lock);
    check(counted == 3, "GetClusterAttrsA counts known tags only", NULL);
    check(name && !strcmp((char *)name, cluster), "MCLA_Name", NULL);
    check(comment == 0, "MCLA_Comment is NULL without a comment", NULL);
    check(hasdriver == 0, "MCLA_HasDriver: a client cluster", NULL);
    if (FindCluster("debugdriver.out.0"))
        check(cluster_attr("debugdriver.out.0", MCLA_HasDriver),
              "MCLA_HasDriver: a driver port's cluster", NULL);

    check(cluster_attr(cluster, MCLA_Overflows) == 0, "MCLA_Overflows starts at 0", NULL);
    for (i = 0; i < 20; i++)
        PutMidi(sender, 0x903c6400UL);
    check(cluster_attr(cluster, MCLA_Overflows) > 0, "MCLA_Overflows counts dropped messages", NULL);
    GetMidiErr(in);
    drain(in);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
}

static void test_link_comments(void)
{
    static char cluster_name[] = "camdcompat.comments";
    static char receiver_comment[] = "Receiver comment";
    static char sender_comment[] = "Sender comment";
    static char long_comment[] = "1234567890123456789012345678901234567890";
    static char truncated_comment[] = "1234567890123456789012345678901234";
    struct TagItem receiver_tags[] = {
        {MLINK_Location, (IPTR)cluster_name},
        {MLINK_Comment, (IPTR)receiver_comment},
        {MLINK_Priority, 5},
        {TAG_DONE, 0}
    };
    struct TagItem sender_tags[] = {
        {MLINK_Location, (IPTR)cluster_name},
        {MLINK_Comment, (IPTR)sender_comment},
        {MLINK_Priority, 5},
        {TAG_DONE, 0}
    };
    struct TagItem promote_sender[] = {
        {MLINK_Comment, (IPTR)long_comment},
        {MLINK_Priority, 6},
        {TAG_DONE, 0}
    };
    struct TagItem clear_comment[] = {
        {MLINK_Comment, 0},
        {TAG_DONE, 0}
    };
    IPTR link_comment = 0, cluster_comment = 0;
    struct TagItem link_query[] = {
        {MLINK_Comment, (IPTR)&link_comment},
        {TAG_DONE, 0}
    };
    struct TagItem cluster_query[] = {
        {MCLA_Comment, (IPTR)&cluster_comment},
        {TAG_DONE, 0}
    };
    struct MidiNode *node = new_node("camdcompat comments", 32, 256);
    struct MidiLink *receiver = NULL, *sender = NULL;
    struct MidiCluster *cluster;
    APTR lock;

    if (!node) {
        check(0, "comments: CreateMidiA", NULL);
        return;
    }
    receiver = AddMidiLinkA(node, MLTYPE_Receiver, receiver_tags);
    sender = AddMidiLinkA(node, MLTYPE_Sender, sender_tags);
    if (!receiver || !sender) {
        check(0, "comments: AddMidiLinkA", NULL);
        goto out;
    }

    GetMidiLinkAttrsA(sender, link_query);
    check(link_comment && !strcmp((char *)link_comment, receiver_comment),
          "MLINK_Comment: receiver wins an equal-priority tie", NULL);

    lock = LockCAMD(CD_Linkages);
    cluster = FindCluster(cluster_name);
    if (cluster)
        GetClusterAttrsA(cluster, cluster_query);
    UnlockCAMD(lock);
    check(cluster_comment && !strcmp((char *)cluster_comment, receiver_comment),
          "MCLA_Comment matches the cluster comment", NULL);

    SetMidiLinkAttrsA(sender, promote_sender);
    link_comment = 0;
    GetMidiLinkAttrsA(receiver, link_query);
    check(link_comment && !strcmp((char *)link_comment, truncated_comment),
          "MLINK_Comment follows priority and is limited to 34 characters", NULL);

    RemoveMidiLink(sender);
    sender = NULL;
    link_comment = 0;
    GetMidiLinkAttrsA(receiver, link_query);
    check(link_comment && !strcmp((char *)link_comment, receiver_comment),
          "MLINK_Comment falls back when the preferred link leaves", NULL);

    SetMidiLinkAttrsA(receiver, clear_comment);
    link_comment = 1;
    GetMidiLinkAttrsA(receiver, link_query);
    check(link_comment == 0, "MLINK_Comment can clear the cluster comment", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    DeleteMidi(node);
}

static void test_clock(void)
{
    static char cluster[] = "camdcompat.clock";
    struct TagItem clock_tags[] = {
        {MIDI_SystemClock, TRUE},
        {TAG_DONE, 0}
    };
    IPTR flag = 0;
    struct TagItem query[] = {
        {MIDI_SystemClock, (IPTR)&flag},
        {TAG_DONE, 0}
    };
    struct MidiNode *out = new_node("camdcompat clock out", 32, 256);
    struct MidiNode *in = new_node("camdcompat clock in", 32, 256);
    struct MidiLink *sender = NULL, *receiver = NULL;
    ULONG t1, t2, before, after;
    MidiMsg msg;

    t1 = CamdTime();
    Delay(50);
    t2 = CamdTime();
    if (t2 - t1 < 800 || t2 - t1 > 1500)
        printf("     one second took %lu ms\n", (unsigned long)(t2 - t1));
    check(t2 - t1 >= 800 && t2 - t1 <= 1500, "CamdTime counts milliseconds", NULL);

    if (!out || !in) {
        check(0, "clock: CreateMidiA", NULL);
        goto out;
    }
    SetMidiAttrsA(in, clock_tags);
    GetMidiAttrsA(in, query);
    check(flag, "MIDI_SystemClock reads back", NULL);
    sender = link_to(out, MLTYPE_Sender, cluster);
    receiver = link_to(in, MLTYPE_Receiver, cluster);
    if (!sender || !receiver) {
        check(0, "clock: AddMidiLinkA", NULL);
        goto out;
    }
    before = CamdTime();
    PutMidi(sender, 0x903c6400UL);
    after = CamdTime();
    check(GetMidi(in, &msg) && msg.mm_Time - before <= after - before,
          "MIDI_SystemClock stamps with CamdTime", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
}

static int next_event(APTR watch, ULONG type, const char *name)
{
    struct ClusterWatchEvent event;

    while (GetClusterWatchEvent(watch, &event))
        if (event.cwe_Type == type && !strcmp(event.cwe_Name, name))
            return 1;
    return 0;
}

static void test_watch(void)
{
    static char cluster[] = "camdcompat.watch";
    struct ClusterWatchEvent event;
    struct TagItem no_bit[] = {{TAG_DONE, 0}};
    BYTE bit = AllocSignal(-1);
    ULONG mask = 1UL << bit;
    struct TagItem tags[] = {
        {CWA_SigBit, (IPTR)bit},
        {TAG_DONE, 0}
    };
    struct MidiNode *node = new_node("camdcompat watch", 32, 256);
    struct MidiLink *first = NULL, *second = NULL;
    APTR watch = NULL;
    char name[32];
    int i, ok;

    check(StartClusterWatchA(no_bit) == NULL, "StartClusterWatchA needs CWA_SigBit", NULL);
    if (bit < 0 || !node || !(watch = StartClusterWatchA(tags))) {
        check(0, "watch: setup", NULL);
        goto out;
    }
    SetSignal(0, mask);
    first = link_to(node, MLTYPE_Receiver, cluster);
    check(SetSignal(0, mask) & mask, "a cluster watch signals", NULL);
    ok = next_event(watch, CWE_Added, cluster);
    ok = ok && next_event(watch, CWE_Participants, cluster);
    check(ok, "CWE_Added, then CWE_Participants", NULL);
    second = link_to(node, MLTYPE_Sender, cluster);
    check(next_event(watch, CWE_Participants, cluster), "CWE_Participants for a link joining", NULL);
    RemoveMidiLink(second);
    second = NULL;
    RemoveMidiLink(first);
    first = NULL;
    check(next_event(watch, CWE_Removed, cluster), "CWE_Removed", NULL);
    check(!GetClusterWatchEvent(watch, &event), "GetClusterWatchEvent is empty afterwards", NULL);

    for (i = 0; i < 20; i++) {
        sprintf(name, "camdcompat.watch%d", i);
        first = link_to(node, MLTYPE_Receiver, name);
        if (first) RemoveMidiLink(first);
    }
    first = NULL;
    check(GetClusterWatchEvent(watch, &event) && event.cwe_Type == CWE_Lost,
          "a full watch reports CWE_Lost first", NULL);
    while (GetClusterWatchEvent(watch, &event))
        ;
    EndClusterWatch(watch);
    watch = NULL;
    EndClusterWatch(NULL);
    SetSignal(0, mask);
    first = link_to(node, MLTYPE_Receiver, cluster);
    check(!(SetSignal(0, mask) & mask), "EndClusterWatch stops signals", NULL);
out:
    if (second) RemoveMidiLink(second);
    if (first) RemoveMidiLink(first);
    if (watch) EndClusterWatch(watch);
    if (node) DeleteMidi(node);
    if (bit >= 0) FreeSignal(bit);
}

static struct Task *watch_parent;
static volatile int watch_done;

static void locked_watch(void)
{
    struct TagItem tags[] = {
        {CWA_SigBit, SIGBREAKB_CTRL_F},
        {TAG_DONE, 0}
    };
    APTR lock = LockCAMD(CD_Linkages);
    APTR watch = StartClusterWatchA(tags);

    EndClusterWatch(watch);
    UnlockCAMD(lock);
    watch_done = 1;
    Signal(watch_parent, SIGBREAKF_CTRL_E);
}

static void test_watch_locked(void)
{
    struct TagItem tags[] = {
        {NP_Entry, (IPTR)locked_watch},
        {NP_Name, (IPTR)"camdcompat locked watch"},
        {TAG_DONE, 0}
    };
    int ticks;

    watch_parent = FindTask(NULL);
    watch_done = 0;
    if (!CreateNewProc(tags)) {
        check(0, "StartClusterWatchA under LockCAMD", "no process");
        return;
    }
    for (ticks = 0; ticks < 150 && !watch_done; ticks++)
        Delay(1);
    check(watch_done, "Start/EndClusterWatch while holding LockCAMD",
          watch_done ? NULL : "deadlock (the process is left waiting)");
}

/* camd.library 43 (provisional) */

static struct CAMDMIDI1EventV1 midi1(UBYTE status, UBYTE data1, UBYTE data2)
{
    struct CAMDMIDI1EventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.Length = 3;
    event.Bytes[0] = status;
    event.Bytes[1] = data1;
    event.Bytes[2] = data2;
    return event;
}

/* Sends on debugdriver port 2 only, so that the runner can compare what the
   driver printed for that port: a legacy message, the session's two messages
   and SysEx, then a second legacy message. */
static void test_endpoints(void)
{
    static char out_cluster[] = "debugdriver.out.2";
    static UBYTE sysex[] = { 0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7 };
    static UBYTE big[8192];
    struct CAMDHandleV1 snapshot, session, second;
    struct CAMDEndpointInfoV1 info, port;
    struct CAMDSessionRequestV1 request;
    struct CAMDSessionInfoV1 got;
    struct CAMDMIDI1EventV1 events[2];
    struct MidiNode *node;
    struct MidiLink *sender;
    ULONG count = 0, i;
    int found = 0;
    LONG result;

    if (!FindCluster(out_cluster)) {
        skip("debugdriver endpoints", "no DEVS:Midi/debugdriver");
        return;
    }
    result = ObtainEndpointSnapshot(&snapshot, &count, NULL);
    check(result == CAMD_RESULT_OK && count >= 4, "ObtainEndpointSnapshot counts the driver's ports", NULL);
    if (result != CAMD_RESULT_OK) return;

    memset(&port, 0, sizeof(port));
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        info.Version = 1;
        if (GetEndpointInfo(&snapshot, i, &info) != CAMD_RESULT_OK) break;
        if (!strcmp(info.Name, "debugdriver.2")) {
            port = info;
            found = 1;
        }
    }
    check(i == count, "GetEndpointInfo reads every endpoint", NULL);
    check(found, "a driver port is an endpoint named after it", NULL);
    check(found && (port.ID.word[0] | port.ID.word[1] | port.ID.word[2] | port.ID.word[3]) != 0,
          "the endpoint has a stable ID", NULL);
    check(found && port.State == CAMD_ENDPOINT_AVAILABLE && port.NativeDataFormats == CAMD_DATA_FORMAT_MIDI1 &&
          port.CurrentProtocol == CAMD_PROTOCOL_MIDI1 && !strcmp(port.Transport, "camd-legacy"),
          "the endpoint is an available native MIDI 1.0 port", NULL);

    memset(&info, 0xa5, sizeof(info));
    info.Size = 24;
    info.Version = 1;
    result = GetEndpointInfo(&snapshot, 0, &info);
    check(result == CAMD_RESULT_OK && info.Size == sizeof(info) && ((UBYTE *)&info)[24] == 0xa5,
          "GetEndpointInfo writes no more than the caller's Size", NULL);
    info.Size = sizeof(info);
    check(GetEndpointInfo(&snapshot, count, &info) == CAMD_RESULT_OUT_OF_RANGE, "GetEndpointInfo past the end is CAMD_RESULT_OUT_OF_RANGE", NULL);
    ReleaseEndpointSnapshot(&snapshot);
    info.Size = sizeof(info);
    check(GetEndpointInfo(&snapshot, 0, &info) == CAMD_RESULT_STALE, "a released snapshot is CAMD_RESULT_STALE", NULL);
    ReleaseEndpointSnapshot(&snapshot);
    check(1, "releasing a snapshot twice returns", NULL);
    if (!found) return;

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = port.ID;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_DATA_FORMAT_UMP;
    request.Protocol = CAMD_PROTOCOL_MIDI2;
    request.QueueCapacity = 8;
    request.Signal = CAMD_SIGNAL_NONE;
    check(OpenEndpointSession(&request, &session, NULL) == CAMD_RESULT_UNSUPPORTED,
          "a UMP session on a MIDI 1.0 port is CAMD_RESULT_UNSUPPORTED", NULL);
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.Direction = CAMD_DIRECTION_INPUT | CAMD_DIRECTION_OUTPUT;
    check(OpenEndpointSession(&request, &session, NULL) == CAMD_RESULT_INVALID,
          "a session with two directions is CAMD_RESULT_INVALID", NULL);
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.EndpointID.word[3] ^= 0x5a5a5a5aUL;
    check(OpenEndpointSession(&request, &session, NULL) != CAMD_RESULT_OK && session.generation == 0,
          "an unknown endpoint ID does not open", NULL);
    request.EndpointID = port.ID;

    node = new_node("camdcompat endpoints", 32, 256);
    sender = node ? link_to(node, MLTYPE_Sender, out_cluster) : NULL;
    check(sender && MidiLinkConnected(sender), "a legacy sender is linked to the same port", NULL);
    if (sender) PutMidi(sender, 0x92301000UL);

    memset(&got, 0, sizeof(got));
    got.Size = sizeof(got);
    got.Version = 1;
    result = OpenEndpointSession(&request, &session, &got);
    check(result == CAMD_RESULT_OK, "OpenEndpointSession opens an output session", NULL);
    if (result != CAMD_RESULT_OK) goto out;
    check(got.Direction == CAMD_DIRECTION_OUTPUT && got.DataFormat == CAMD_DATA_FORMAT_MIDI1 &&
          got.QueueCapacity >= 8 && got.MaxSysExBytes >= sizeof(sysex),
          "the session reports its format, queue and SysEx limit", NULL);

    events[0] = midi1(0x91, 0x40, 0x7f);
    events[1] = midi1(0x81, 0x40, 0x00);
    check(PutEndpointMidi(&session, events, 2) == CAMD_RESULT_OK, "PutEndpointMidi queues a batch", NULL);
    check(PutEndpointSysEx(&session, sysex, sizeof(sysex), NULL) == CAMD_RESULT_OK, "PutEndpointSysEx queues a message", NULL);
    check(DrainEndpointSession(&session, CAMD_WAIT_FOREVER) == CAMD_RESULT_OK, "DrainEndpointSession returns once they are sent", NULL);
    if (sender) PutMidi(sender, 0x92311100UL);

    events[0].Length = 0;
    check(PutEndpointMidi(&session, events, 1) == CAMD_RESULT_INVALID, "a message without bytes is CAMD_RESULT_INVALID", NULL);
    memset(big, 0x01, sizeof(big));
    big[0] = 0xf0;
    big[sizeof(big) - 1] = 0xf7;
    check(got.MaxSysExBytes < sizeof(big) && PutEndpointSysEx(&session, big, sizeof(big), NULL) == CAMD_RESULT_TOO_LARGE,
          "SysEx past MaxSysExBytes is CAMD_RESULT_TOO_LARGE", NULL);

    check(OpenEndpointSession(&request, &second, NULL) == CAMD_RESULT_OK, "a second session opens on the same endpoint", NULL);
    check(CloseEndpointSession(&second) == CAMD_RESULT_OK, "CloseEndpointSession closes it", NULL);

    /* The session keeps the port open without the legacy link. */
    if (sender) {
        RemoveMidiLink(sender);
        sender = NULL;
    }
    events[0] = midi1(0xb1, 0x07, 0x64);
    check(PutEndpointMidi(&session, events, 1) == CAMD_RESULT_OK && DrainEndpointSession(&session, CAMD_WAIT_FOREVER) == CAMD_RESULT_OK,
          "the session still sends after the legacy link has gone", NULL);

    check(CloseEndpointSession(&session) == CAMD_RESULT_OK, "CloseEndpointSession", NULL);
    check(PutEndpointMidi(&session, events, 1) != CAMD_RESULT_OK, "a closed session does not send", NULL);
    check(CloseEndpointSession(&session) != CAMD_RESULT_OK, "closing a session twice is refused", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (node) DeleteMidi(node);
}

static int find_endpoint(const char *name, struct CAMDEndpointIDV1 *id)
{
    struct CAMDHandleV1 snapshot;
    struct CAMDEndpointInfoV1 info;
    ULONG count = 0, i;
    int found = 0;

    if (ObtainEndpointSnapshot(&snapshot, &count, NULL) != CAMD_RESULT_OK) return 0;
    for (i = 0; i < count && !found; i++) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        info.Version = 1;
        if (GetEndpointInfo(&snapshot, i, &info) == CAMD_RESULT_OK && !strcmp(info.Name, name)) {
            *id = info.ID;
            found = 1;
        }
    }
    ReleaseEndpointSnapshot(&snapshot);
    return found;
}

/* A port hands over what it receives in its own process, so wait a little. */
static LONG read_soon(struct CAMDHandleV1 *session, struct CAMDMIDI1EventV1 *event, UBYTE *sysex, ULONG size,
                      ULONG *length)
{
    LONG result = CAMD_RESULT_EMPTY;
    int tries;

    for (tries = 0; tries < 100; tries++) {
        result = GetEndpointMidi(session, event, sysex, size, length);
        if (result != CAMD_RESULT_EMPTY) break;
        Delay(1);
    }
    return result;
}

/* Input sessions, on the loopback driver's first port. */
static void test_endpoint_input(void)
{
    static char out_cluster[] = "loopback.out.0";
    static char in_cluster[] = "loopback.in.0";
    static UBYTE sysex[] = { 0xf0, 0x7d, 0x09, 0x08, 0x07, 0xf7 };
    UBYTE buffer[16];
    struct CAMDEndpointIDV1 id;
    struct CAMDSessionRequestV1 request;
    struct CAMDSessionInfoV1 got;
    struct CAMDHandleV1 input, output, small;
    struct CAMDMIDI1EventV1 event, events[8];
    struct MidiNode *node = NULL;
    struct MidiLink *sender = NULL, *receiver = NULL;
    MidiMsg msg;
    ULONG length = 99, i;
    BYTE bit;
    LONG result;
    int count;

    if (!FindCluster(out_cluster) || !find_endpoint("loopback.0", &id)) {
        skip("loopback endpoint", "no DEVS:Midi/loopback");
        return;
    }
    bit = AllocSignal(-1);
    if (bit < 0) {
        check(0, "input: AllocSignal", NULL);
        return;
    }
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.Direction = CAMD_DIRECTION_INPUT;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 16;
    request.Signal = 32;
    check(OpenEndpointSession(&request, &input, NULL) == CAMD_RESULT_INVALID, "a signal number past 31 is CAMD_RESULT_INVALID", NULL);
    request.Signal = (ULONG)bit;
    memset(&got, 0, sizeof(got));
    got.Size = sizeof(got);
    got.Version = 1;
    result = OpenEndpointSession(&request, &input, &got);
    check(result == CAMD_RESULT_OK && got.Direction == CAMD_DIRECTION_INPUT && got.QueueCapacity >= 16 &&
          got.MaxSysExBytes == CAMD_SYSEX_BYTES_DEFAULT, "OpenEndpointSession opens an input session", NULL);
    if (result != CAMD_RESULT_OK) {
        FreeSignal(bit);
        return;
    }
    check(GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY && length == 0,
          "GetEndpointMidi is CAMD_RESULT_EMPTY with nothing received", NULL);
    events[0] = midi1(0x90, 0x3c, 0x64);
    check(PutEndpointMidi(&input, events, 1) != CAMD_RESULT_OK, "an input session does not send", NULL);

    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.Signal = CAMD_SIGNAL_NONE;
    result = OpenEndpointSession(&request, &output, NULL);
    check(result == CAMD_RESULT_OK, "an output session opens on the same endpoint", NULL);
    if (result != CAMD_RESULT_OK) goto out;
    check(GetEndpointMidi(&output, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_STALE,
          "an output session does not receive", NULL);

    SetSignal(0, 1UL << bit);
    events[1] = midi1(0x80, 0x3c, 0x40);
    check(PutEndpointMidi(&output, events, 2) == CAMD_RESULT_OK, "the output session sends two messages", NULL);
    memset(&event, 0, sizeof(event));
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && length == 0 && event.Length == 3 && event.Bytes[0] == 0x90 &&
          event.Bytes[1] == 0x3c && event.Bytes[2] == 0x64, "the input session receives the first", NULL);
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && event.Bytes[0] == 0x80 && event.Bytes[2] == 0x40, "and the second, in order", NULL);
    check((SetSignal(0, 0) & (1UL << bit)) != 0, "the opening task was signalled", NULL);

    check(PutEndpointSysEx(&output, sysex, sizeof(sysex), NULL) == CAMD_RESULT_OK, "the output session sends SysEx", NULL);
    result = read_soon(&input, &event, buffer, 3, &length);
    check(result == CAMD_RESULT_TOO_LARGE && length == sizeof(sysex),
          "SysEx that does not fit is CAMD_RESULT_TOO_LARGE with its length", NULL);
    memset(buffer, 0, sizeof(buffer));
    result = GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && length == sizeof(sysex) && !memcmp(buffer, sysex, sizeof(sysex)),
          "the input session then receives the complete SysEx", NULL);

    /* Both ways between a session and the port's legacy clusters. */
    node = new_node("camdcompat endpoint input", 32, 256);
    sender = node ? link_to(node, MLTYPE_Sender, out_cluster) : NULL;
    receiver = node ? link_to(node, MLTYPE_Receiver, in_cluster) : NULL;
    check(sender && receiver, "legacy links to the same port", NULL);
    if (sender && receiver) {
        PutMidi(sender, 0xb0074000UL);
        result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
        check(result == CAMD_RESULT_OK && event.Bytes[0] == 0xb0 && event.Bytes[1] == 0x07 && event.Bytes[2] == 0x40,
              "the input session receives what a legacy link sent", NULL);
        check(get_soon(node, &msg) && msg.mm_Status == 0xb0, "and so does the legacy receiver", NULL);
        events[0] = midi1(0xc1, 0x05, 0x00);
        events[0].Length = 2;
        check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_OK && get_soon(node, &msg) && msg.mm_Status == 0xc1 &&
              msg.mm_Data1 == 0x05, "a legacy receiver gets what the output session sent", NULL);
        result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
        check(result == CAMD_RESULT_OK && event.Length == 2 && event.Bytes[0] == 0xc1, "and so does the input session", NULL);
    }

    /* A second input session with a queue of four loses the rest. */
    request.Direction = CAMD_DIRECTION_INPUT;
    request.QueueCapacity = 4;
    result = OpenEndpointSession(&request, &small, NULL);
    check(result == CAMD_RESULT_OK, "a second input session opens", NULL);
    if (result == CAMD_RESULT_OK) {
        for (i = 0; i < 8; i++) events[i] = midi1(0x92, (UBYTE)i, 0x10);
        check(PutEndpointMidi(&output, events, 8) == CAMD_RESULT_OK && DrainEndpointSession(&output, CAMD_WAIT_FOREVER) == CAMD_RESULT_OK,
              "eight messages are sent", NULL);
        Delay(10);
        check(GetEndpointMidi(&small, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_QUEUE_FULL,
              "a full input queue reports CAMD_RESULT_QUEUE_FULL once", NULL);
        for (count = 0; GetEndpointMidi(&small, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK; count++)
            if (event.Bytes[1] != (UBYTE)count) break;
        check(count == 4, "and keeps the four oldest messages in order", NULL);
        for (count = 0; GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK; count++);
        check(count == 8, "the larger session received all eight", NULL);
        check(CloseEndpointSession(&small) == CAMD_RESULT_OK, "the second input session closes", NULL);
    }
    check(CloseEndpointSession(&output) == CAMD_RESULT_OK, "the output session closes", NULL);
out:
    check(CloseEndpointSession(&input) == CAMD_RESULT_OK, "the input session closes", NULL);
    check(GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_STALE,
          "a closed input session is CAMD_RESULT_STALE", NULL);
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (node) DeleteMidi(node);
    FreeSignal(bit);
}

static volatile int hog_running;

/* Uses the processor until told to stop. */
static void hog_entry(void)
{
    while (hog_running == 1);
    hog_running = 2;
}

/* Times: received messages are stamped with CamdTime(), and a message sent
   with a time waits for it. On the loopback driver's second port. */
static void test_endpoint_time(void)
{
    struct Process *hog;
    static UBYTE sysex[] = { 0xf0, 0x7d, 0x31, 0xf7 };
    UBYTE buffer[16];
    struct CAMDEndpointIDV1 id;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 input, output;
    struct CAMDMIDI1EventV1 event, events[3];
    ULONG length, before, after, target, waited;
    LONG result;

    if (!find_endpoint("loopback.1", &id)) {
        skip("endpoint times", "no DEVS:Midi/loopback");
        return;
    }
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 16;
    request.Signal = CAMD_SIGNAL_NONE;
    request.Direction = CAMD_DIRECTION_INPUT;
    result = OpenEndpointSession(&request, &input, NULL);
    request.Direction = CAMD_DIRECTION_OUTPUT;
    if (result == CAMD_RESULT_OK) {
        result = OpenEndpointSession(&request, &output, NULL);
        if (result != CAMD_RESULT_OK) CloseEndpointSession(&input);
    }
    check(result == CAMD_RESULT_OK, "time: sessions open on a loopback port", NULL);
    if (result != CAMD_RESULT_OK) return;

    before = CamdTime();
    events[0] = midi1(0x96, 0x10, 0x40);
    check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_OK, "a message without a time is sent", NULL);
    memset(&event, 0, sizeof(event));
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    after = CamdTime();
    check(result == CAMD_RESULT_OK && (event.Flags & CAMD_EVENT_TIME_VALID) && event.ClockDomain == CAMD_CLOCK_CAMD &&
          event.TimeHigh == 0 && (LONG)(event.TimeLow - before) >= 0 && (LONG)(after - event.TimeLow) >= 0,
          "a received message carries the CamdTime() of its arrival", NULL);
    check(PutEndpointSysEx(&output, sysex, sizeof(sysex), NULL) == CAMD_RESULT_OK &&
          read_soon(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == sizeof(sysex) &&
          event.Length == 0 && (event.Flags & CAMD_EVENT_TIME_VALID) && (LONG)(event.TimeLow - before) >= 0,
          "received SysEx carries its time in the event", NULL);

    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = 99;
    check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_INVALID, "a time in an unknown clock is CAMD_RESULT_INVALID", NULL);

    /* One at once, one in 300 ms, and an overdue one sent last. */
    target = CamdTime() + 300;
    events[0] = midi1(0x96, 0x20, 0x40);
    events[1] = midi1(0x96, 0x21, 0x40);
    events[1].Flags = CAMD_EVENT_TIME_VALID;
    events[1].ClockDomain = CAMD_CLOCK_CAMD;
    events[1].TimeLow = target;
    events[2] = midi1(0x96, 0x22, 0x40);
    events[2].Flags = CAMD_EVENT_TIME_VALID;
    events[2].ClockDomain = CAMD_CLOCK_CAMD;
    events[2].TimeLow = target - 5000;
    check(PutEndpointMidi(&output, events, 3) == CAMD_RESULT_OK, "three messages are queued, the second for 300 ms later", NULL);
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && event.Bytes[1] == 0x22, "the overdue one arrives first", NULL);
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && event.Bytes[1] == 0x20, "then the one without a time", NULL);
    events[0] = midi1(0x96, 0x23, 0x40);
    check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_OK &&
          read_soon(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Bytes[1] == 0x23 &&
          (LONG)(target - CamdTime()) > 0, "one sent meanwhile does not wait behind the timed one", NULL);
    Delay(5);
    check(GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY &&
          (LONG)(target - CamdTime()) > 0, "the timed one has not arrived before its time", NULL);
    hog_running = 1;
    hog = CreateNewProcTags(NP_Entry, (IPTR)hog_entry, NP_Name, (IPTR)"camdcompat hog", NP_Priority, 0, TAG_END);
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    hog_running = 0;
    while (hog && hog_running != 2) Delay(1);
    waited = event.TimeLow - target;
    check(result == CAMD_RESULT_OK && event.Bytes[1] == 0x21 && (LONG)waited >= 0 && waited < 250,
          "it arrives when its time has come, also with a task hogging the processor", NULL);

    /* SysEx with a time. */
    target = CamdTime() + 200;
    memset(&event, 0, sizeof(event));
    event.Flags = CAMD_EVENT_TIME_VALID;
    event.ClockDomain = CAMD_CLOCK_CAMD;
    event.TimeLow = target;
    check(PutEndpointSysEx(&output, sysex, sizeof(sysex), &event) == CAMD_RESULT_OK, "SysEx is queued for 200 ms later", NULL);
    Delay(3);
    check(GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY &&
          (LONG)(target - CamdTime()) > 0, "it has not arrived before its time", NULL);
    result = read_soon(&input, &event, buffer, sizeof(buffer), &length);
    check(result == CAMD_RESULT_OK && length == sizeof(sysex) && (LONG)(CamdTime() - target) >= 0,
          "and arrives when its time has come", NULL);

    /* A message that waits can be cancelled, and does not hold up closing. */
    events[0] = midi1(0x96, 0x30, 0x40);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = CAMD_CLOCK_CAMD;
    events[0].TimeLow = CamdTime() + 60000;
    check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_OK && CancelEndpointSession(&output) == CAMD_RESULT_OK &&
          DrainEndpointSession(&output, CAMD_WAIT_FOREVER) == CAMD_RESULT_OK, "a waiting message is cancelled", NULL);
    events[0].TimeLow = CamdTime() + 60000;
    before = CamdTime();
    check(PutEndpointMidi(&output, events, 1) == CAMD_RESULT_OK && CloseEndpointSession(&output) == CAMD_RESULT_OK &&
          CamdTime() - before < 2000, "closing does not wait for a message's time", NULL);
    Delay(5);
    check(GetEndpointMidi(&input, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY, "and neither was sent", NULL);
    CloseEndpointSession(&input);
}

/* Longer SysEx for a session that asks, and counted loss for one that does
   not. On the loopback driver's first port. */
static void test_endpoint_sysex_limit(void)
{
    static UBYTE big[10000], back[10000];
    struct CAMDEndpointIDV1 id;
    struct CAMDSessionRequestV1 request;
    struct CAMDSessionInfoV1 got;
    struct CAMDSessionStatsV1 stats;
    struct CAMDHandleV1 wide, narrow, output;
    struct CAMDMIDI1EventV1 event;
    ULONG length = 0, i;
    LONG result;

    if (!find_endpoint("loopback.0", &id)) {
        skip("endpoint SysEx limit", "no DEVS:Midi/loopback");
        return;
    }
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 2;
    request.Signal = CAMD_SIGNAL_NONE;
    request.Direction = CAMD_DIRECTION_INPUT;
    request.MaxSysExBytes = CAMD_SYSEX_BYTES_LIMIT + 1;
    check(OpenEndpointSession(&request, &wide, NULL) == CAMD_RESULT_UNSUPPORTED, "more SysEx than CAMD_SYSEX_BYTES_LIMIT is CAMD_RESULT_UNSUPPORTED", NULL);
    request.MaxSysExBytes = 16384;
    memset(&got, 0, sizeof(got));
    got.Size = sizeof(got);
    got.Version = 1;
    result = OpenEndpointSession(&request, &wide, &got);
    check(result == CAMD_RESULT_OK && got.MaxSysExBytes == 16384, "a session opens with a 16 KB SysEx limit", NULL);
    if (result != CAMD_RESULT_OK) return;
    request.MaxSysExBytes = 0;
    result = OpenEndpointSession(&request, &narrow, NULL);
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.MaxSysExBytes = 16384;
    if (result == CAMD_RESULT_OK) {
        result = OpenEndpointSession(&request, &output, NULL);
        if (result != CAMD_RESULT_OK) CloseEndpointSession(&narrow);
    }
    if (result != CAMD_RESULT_OK) {
        check(0, "SysEx limit: sessions open", NULL);
        CloseEndpointSession(&wide);
        return;
    }
    big[0] = 0xf0;
    for (i = 1; i < sizeof(big) - 1; i++) big[i] = (UBYTE)(i % 127);
    big[sizeof(big) - 1] = 0xf7;
    check(PutEndpointSysEx(&output, big, sizeof(big), NULL) == CAMD_RESULT_OK &&
          DrainEndpointSession(&output, 5000) == CAMD_RESULT_OK, "a 10000-byte SysEx message is sent", NULL);
    for (i = 0; i < 200; i++) {
        result = GetEndpointMidi(&wide, &event, back, sizeof(back), &length);
        if (result != CAMD_RESULT_EMPTY) break;
        Delay(1);
    }
    check(result == CAMD_RESULT_OK && length == sizeof(big) && !memcmp(back, big, sizeof(big)),
          "the session with the larger limit receives all of it", NULL);
    memset(&stats, 0, sizeof(stats));
    stats.Size = sizeof(stats);
    stats.Version = 1;
    check(GetEndpointMidi(&narrow, &event, back, sizeof(back), &length) == CAMD_RESULT_QUEUE_FULL &&
          GetEndpointSessionStats(&narrow, &stats) == CAMD_RESULT_OK && stats.Dropped == 1 && stats.Received == 0,
          "the session with the usual limit is told it lost a message, and counts it", NULL);
    CloseEndpointSession(&output);
    CloseEndpointSession(&narrow);
    CloseEndpointSession(&wide);
}

static void test_endpoint_watch(void)
{
    struct CAMDHandleV1 watch, other;
    struct CAMDGenerationV1 generation;
    struct CAMDEndpointWatchEventV1 event;

    check(StartEndpointWatch(&watch, 32, NULL) == CAMD_RESULT_INVALID && watch.generation == 0,
          "a watch signal number past 31 is CAMD_RESULT_INVALID", NULL);
    generation.high = generation.low = 0xffffffffUL;
    check(StartEndpointWatch(&watch, CAMD_SIGNAL_NONE, &generation) == CAMD_RESULT_OK &&
          (generation.high != 0xffffffffUL || generation.low != 0xffffffffUL),
          "StartEndpointWatch gives the current generation", NULL);
    check(StartEndpointWatch(&other, CAMD_SIGNAL_NONE, NULL) == CAMD_RESULT_OK &&
          (other.slot != watch.slot || other.generation != watch.generation), "a second watch has its own handle", NULL);
    memset(&event, 0, sizeof(event));
    check(GetEndpointWatchEvent(&watch, &event) == CAMD_RESULT_EMPTY, "a watch is empty while nothing changes", NULL);
    EndEndpointWatch(&other);
    EndEndpointWatch(&watch);
    check(GetEndpointWatchEvent(&watch, &event) == CAMD_RESULT_STALE, "an ended watch is CAMD_RESULT_STALE", NULL);
    EndEndpointWatch(&watch);
    check(1, "ending a watch twice returns", NULL);
}

/* Around test_rethink_load(): the driver RethinkCAMD() loads adds endpoints. */
static struct CAMDHandleV1 rethink_watch;
static BYTE rethink_bit = -1;
static int rethink_watching;

static void start_rethink_watch(void)
{
    rethink_bit = AllocSignal(-1);
    if (rethink_bit < 0) return;
    SetSignal(0, 1UL << rethink_bit);
    rethink_watching = StartEndpointWatch(&rethink_watch, (ULONG)rethink_bit, NULL) == CAMD_RESULT_OK;
}

static void test_rethink_watch(void)
{
    struct CAMDEndpointWatchEventV1 event, first;
    struct CAMDEndpointIDV1 port;
    int added = 0, ordered = 1, matched = 0;

    check(rethink_watching, "a watch was started before RethinkCAMD", NULL);
    if (!rethink_watching) return;
    check((SetSignal(0, 0) & (1UL << rethink_bit)) != 0, "loading a driver signals the watch", NULL);
    memset(&first, 0, sizeof(first));
    while (GetEndpointWatchEvent(&rethink_watch, &event) == CAMD_RESULT_OK) {
        if (event.Type != CAMD_ENDPOINT_EVENT_ADDED) continue;
        if (added == 0) first = event;
        else if (event.Generation.high < first.Generation.high ||
                 (event.Generation.high == first.Generation.high && event.Generation.low <= first.Generation.low))
            ordered = 0;
        if (find_endpoint("debugdriver.0", &port) && !memcmp(&port, &event.EndpointID, sizeof(port))) matched = 1;
        added++;
    }
    check(added == 4, "the watch has one CAMD_ENDPOINT_EVENT_ADDED per port", NULL);
    check(ordered, "later events have later generations", NULL);
    check(matched, "an event carries the ID the snapshot shows", NULL);
    EndEndpointWatch(&rethink_watch);
    FreeSignal(rethink_bit);
}

static ULONG endpoint_state(const char *name)
{
    struct CAMDHandleV1 snapshot;
    struct CAMDEndpointInfoV1 info;
    ULONG count = 0, i, state = 0;

    if (ObtainEndpointSnapshot(&snapshot, &count, NULL) != CAMD_RESULT_OK) return 0;
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        info.Version = 1;
        if (GetEndpointInfo(&snapshot, i, &info) == CAMD_RESULT_OK && !strcmp(info.Name, name) &&
            !strcmp(info.Transport, "camd-app"))
            state = info.State;
    }
    ReleaseEndpointSnapshot(&snapshot);
    return state;
}

/* An endpoint this program publishes, and this program as its client too. */
static void test_published_endpoint(void)
{
    static char name[] = "camdcompat port";
    static UBYTE sysex[] = { 0xf0, 0x7d, 0x21, 0x22, 0xf7 };
    UBYTE buffer[16];
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 port, again, to_port, from_port, late;
    struct CAMDEndpointIDV1 id, found, republished;
    static char out_cluster[] = "camdcompat port.out.0";
    static char in_cluster[] = "camdcompat port.in.0";
    struct CAMDMIDI1EventV1 event, events[2];
    struct MidiNode *node = NULL;
    struct MidiLink *sender = NULL, *receiver = NULL;
    MidiMsg msg;
    ULONG length = 99;
    BYTE bit = AllocSignal(-1);
    LONG result;

    if (bit < 0) {
        check(0, "publish: AllocSignal", NULL);
        return;
    }
    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = CAMD_DIRECTION_INPUT | CAMD_DIRECTION_OUTPUT;
    publish.Signal = (ULONG)bit;
    publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    publish.Protocol = CAMD_PROTOCOL_MIDI1;
    check(PublishEndpoint(&publish, &port, &id) == CAMD_RESULT_INVALID && port.generation == 0,
          "an endpoint without a name is CAMD_RESULT_INVALID", NULL);
    strcpy(publish.Name, name);
    strcpy(publish.ProductInstance, "MIDIHubCAMDCompat");
    memset(&id, 0, sizeof(id));
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK && (id.word[0] | id.word[1] | id.word[2] | id.word[3]) != 0,
          "PublishEndpoint gives a handle and a stable ID", NULL);
    if (result != CAMD_RESULT_OK) {
        FreeSignal(bit);
        return;
    }
    check(endpoint_state(name) == CAMD_ENDPOINT_AVAILABLE && find_endpoint(name, &found) && !memcmp(&found, &id, sizeof(id)),
          "the published endpoint is in the snapshot with that ID", NULL);
    check(PublishEndpoint(&publish, &again, NULL) == CAMD_RESULT_DUPLICATE && again.generation == 0,
          "a second endpoint of the same name is CAMD_RESULT_DUPLICATE", NULL);
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY && length == 0,
          "GetPublishedMidi is CAMD_RESULT_EMPTY with nothing sent", NULL);

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 8;
    request.Signal = CAMD_SIGNAL_NONE;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    result = OpenEndpointSession(&request, &to_port, NULL);
    request.Direction = CAMD_DIRECTION_INPUT;
    if (result == CAMD_RESULT_OK) result = OpenEndpointSession(&request, &from_port, NULL);
    check(result == CAMD_RESULT_OK, "clients open sessions with it in both directions", NULL);
    if (result != CAMD_RESULT_OK) goto out;

    /* Client to publisher. */
    SetSignal(0, 1UL << bit);
    events[0] = midi1(0x93, 0x45, 0x50);
    events[1] = midi1(0x83, 0x45, 0x00);
    check(PutEndpointMidi(&to_port, events, 2) == CAMD_RESULT_OK && PutEndpointSysEx(&to_port, sysex, sizeof(sysex), NULL) == CAMD_RESULT_OK,
          "a client sends two messages and SysEx", NULL);
    check((SetSignal(0, 0) & (1UL << bit)) != 0, "the publisher was signalled", NULL);
    memset(&event, 0, sizeof(event));
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == 0 &&
          event.Bytes[0] == 0x93 && event.Bytes[1] == 0x45 && event.Bytes[2] == 0x50, "the publisher takes the first", NULL);
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Bytes[0] == 0x83,
          "and the second, in order", NULL);
    check(GetPublishedMidi(&port, &event, buffer, 2, &length) == CAMD_RESULT_TOO_LARGE && length == sizeof(sysex),
          "SysEx that does not fit is CAMD_RESULT_TOO_LARGE with its length", NULL);
    memset(buffer, 0, sizeof(buffer));
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == sizeof(sysex) &&
          !memcmp(buffer, sysex, sizeof(sysex)), "the publisher then takes the SysEx", NULL);
    check(DrainEndpointSession(&to_port, CAMD_WAIT_FOREVER) == CAMD_RESULT_OK, "the client's session is drained", NULL);

    /* Publisher to client: handed over at once, in this task. */
    events[0] = midi1(0xb3, 0x0a, 0x7f);
    check(PutPublishedMidi(&port, events, 1) == CAMD_RESULT_OK && PutPublishedSysEx(&port, sysex, sizeof(sysex)) == CAMD_RESULT_OK,
          "the publisher sends a message and SysEx", NULL);
    check(GetEndpointMidi(&from_port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == 0 &&
          event.Bytes[0] == 0xb3 && event.Bytes[1] == 0x0a, "the client receives the message", NULL);
    memset(buffer, 0, sizeof(buffer));
    check(GetEndpointMidi(&from_port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == sizeof(sysex) &&
          !memcmp(buffer, sysex, sizeof(sysex)), "and the SysEx", NULL);
    {
        struct CAMDSessionStatsV1 sent, got;

        memset(&sent, 0, sizeof(sent));
        memset(&got, 0, sizeof(got));
        sent.Size = got.Size = sizeof(sent);
        sent.Version = got.Version = 1;
        check(GetEndpointSessionStats(&to_port, &sent) == CAMD_RESULT_OK && sent.Sent == 3 && sent.Rejected == 0 &&
              GetEndpointSessionStats(&from_port, &got) == CAMD_RESULT_OK && got.Received == 2 && got.Dropped == 0,
              "GetEndpointSessionStats counts what the sessions carried", NULL);
    }
    events[0] = midi1(0x93, 0x47, 0x50);
    events[1] = midi1(0x83, 0x47, 0x00);
    check(PutEndpointMidi(&to_port, events, 2) == CAMD_RESULT_OK && CancelEndpointSession(&to_port) == CAMD_RESULT_OK &&
          GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY,
          "CancelEndpointSession drops what was not taken yet", NULL);
    strcpy(publish.Name, "debugdriver");
    check(!FindCluster("debugdriver.out.0") || PublishEndpoint(&publish, &again, NULL) == CAMD_RESULT_DUPLICATE,
          "a loaded driver's name cannot be published", NULL);
    strcpy(publish.Name, name);
    events[0].Length = 0;
    check(PutPublishedMidi(&port, events, 1) == CAMD_RESULT_INVALID, "the publisher cannot send a message without bytes", NULL);
    events[0] = midi1(0x93, 0x46, 0x50);

    /* Its legacy clusters: old programs reach it by linking, as to a port. */
    check(count_clusters(out_cluster) == 1 && count_clusters(in_cluster) == 1, "the endpoint has legacy clusters named after it", NULL);
    node = new_node("camdcompat published", 32, 256);
    sender = node ? link_to(node, MLTYPE_Sender, out_cluster) : NULL;
    receiver = node ? link_to(node, MLTYPE_Receiver, in_cluster) : NULL;
    check(sender && receiver && MidiLinkConnected(sender) && MidiLinkConnected(receiver), "legacy links to them are connected", NULL);
    if (sender && receiver) {
        SetSignal(0, 1UL << bit);
        PutMidi(sender, 0x94373300UL);
        check((SetSignal(0, 0) & (1UL << bit)) != 0 &&
              GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == 0 &&
              event.Length == 3 && event.Bytes[0] == 0x94 && event.Bytes[1] == 0x37 && event.Bytes[2] == 0x33,
              "the publisher takes what a legacy link sent", NULL);
        PutMidi(sender, 0xc4090000UL);
        check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Length == 2 &&
              event.Bytes[0] == 0xc4 && event.Bytes[1] == 0x09, "a two-byte message keeps its length", NULL);
        PutSysEx(sender, sysex);
        memset(buffer, 0, sizeof(buffer));
        check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && length == sizeof(sysex) &&
              !memcmp(buffer, sysex, sizeof(sysex)), "and legacy SysEx", NULL);
        events[0] = midi1(0x95, 0x38, 0x44);
        check(PutPublishedMidi(&port, events, 1) == CAMD_RESULT_OK && GetMidi(node, &msg) && msg.mm_Status == 0x95 &&
              msg.mm_Data1 == 0x38 && msg.mm_Data2 == 0x44, "a legacy receiver gets what the publisher sent", NULL);
        memset(buffer, 0, sizeof(buffer));
        check(PutPublishedSysEx(&port, sysex, sizeof(sysex)) == CAMD_RESULT_OK && GetMidi(node, &msg) && msg.mm_Status == 0xf0 &&
              QuerySysEx(node) == sizeof(sysex) && GetSysEx(node, buffer, sizeof(buffer)) == sizeof(sysex) &&
              !memcmp(buffer, sysex, sizeof(sysex)), "and the publisher's SysEx", NULL);
        /* The client session that receives got both as well. */
        while (GetEndpointMidi(&from_port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK);
    }
    events[0] = midi1(0x93, 0x46, 0x50);

    /* Offline and back. */
    check(SetPublishedEndpointState(&port, CAMD_ENDPOINT_RETIRED) == CAMD_RESULT_INVALID, "only available and offline can be set", NULL);
    check(SetPublishedEndpointState(&port, CAMD_ENDPOINT_OFFLINE) == CAMD_RESULT_OK && endpoint_state(name) == CAMD_ENDPOINT_OFFLINE,
          "the endpoint goes offline and stays in the snapshot", NULL);
    check(PutEndpointMidi(&to_port, events, 1) == CAMD_RESULT_WRONG_STATE, "a session to an offline endpoint is CAMD_RESULT_WRONG_STATE", NULL);
    if (sender && receiver) {
        check(!MidiLinkConnected(sender) && FindCluster(out_cluster) != NULL, "offline it leaves its clusters, where the links wait", NULL);
        PutMidi(sender, 0x94030300UL);
    }
    request.Direction = CAMD_DIRECTION_OUTPUT;
    check(OpenEndpointSession(&request, &late, NULL) == CAMD_RESULT_WRONG_STATE, "no session opens with an offline endpoint", NULL);
    check(SetPublishedEndpointState(&port, CAMD_ENDPOINT_AVAILABLE) == CAMD_RESULT_OK && find_endpoint(name, &found) &&
          !memcmp(&found, &id, sizeof(id)), "it comes back with the same ID", NULL);
    check(PutEndpointMidi(&to_port, events, 1) == CAMD_RESULT_OK &&
          GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Bytes[1] == 0x46,
          "and the open session carries messages again, with nothing from while it was offline", NULL);
    if (sender) check(MidiLinkConnected(sender), "back online it is in its clusters again", NULL);

    /* The publisher goes while its clients still hold sessions. */
    WithdrawEndpoint(&port);
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_STALE &&
          PutPublishedMidi(&port, events, 1) == CAMD_RESULT_STALE, "a withdrawn endpoint's handle is CAMD_RESULT_STALE", NULL);
    check(endpoint_state(name) != CAMD_ENDPOINT_AVAILABLE, "it is not available in the snapshot any more", NULL);
    check(PutEndpointMidi(&to_port, events, 1) == CAMD_RESULT_RETIRED, "a client's session is CAMD_RESULT_RETIRED", NULL);
    check(DrainEndpointSession(&to_port, CAMD_WAIT_FOREVER) != CAMD_RESULT_QUEUE_FULL, "draining it does not wait for the publisher", NULL);
    check(OpenEndpointSession(&request, &late, NULL) != CAMD_RESULT_OK, "no session opens with it", NULL);
    check(PublishEndpoint(&publish, &again, NULL) != CAMD_RESULT_OK, "the name cannot be published while clients hold the old one", NULL);
    check(CloseEndpointSession(&to_port) == CAMD_RESULT_OK && CloseEndpointSession(&from_port) == CAMD_RESULT_OK,
          "the clients close their sessions", NULL);
    WithdrawEndpoint(&port);
    check(1, "withdrawing twice returns", NULL);
    if (sender) {
        check(FindCluster(out_cluster) != NULL, "a legacy link keeps the withdrawn endpoint's cluster", NULL);
        PutMidi(sender, 0x94010100UL);
        check(1, "sending to it returns", NULL);
    }

    memset(&republished, 0, sizeof(republished));
    result = PublishEndpoint(&publish, &again, &republished);
    check(result == CAMD_RESULT_OK && !memcmp(&republished, &id, sizeof(id)), "published again, the name has the same ID", NULL);
    if (result == CAMD_RESULT_OK) {
        request.Direction = CAMD_DIRECTION_OUTPUT;
        check(OpenEndpointSession(&request, &late, NULL) == CAMD_RESULT_OK && CloseEndpointSession(&late) == CAMD_RESULT_OK,
              "and a session opens with the ID a client kept", NULL);
        if (sender) {
            PutMidi(sender, 0x94020200UL);
            check(GetPublishedMidi(&again, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK &&
                  event.Bytes[0] == 0x94 && event.Bytes[1] == 0x02, "the waiting legacy link reaches it without linking again", NULL);
        }
        WithdrawEndpoint(&again);
    }
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (node) DeleteMidi(node);
    check(!FindCluster(out_cluster) && !FindCluster(in_cluster), "its clusters go with the last link", NULL);
    FreeSignal(bit);
    return;
out:
    WithdrawEndpoint(&port);
    FreeSignal(bit);
}

static struct CAMDUMPEventV1 ump(ULONG word0, ULONG word1, ULONG words)
{
    struct CAMDUMPEventV1 event;

    memset(&event, 0, sizeof(event));
    event.Size = sizeof(event);
    event.Version = 1;
    event.WordCount = words;
    event.Words[0] = word0;
    event.Words[1] = word1;
    return event;
}

/* The place of a named endpoint in a snapshot, or -1. */
static LONG endpoint_index(struct CAMDHandleV1 *snapshot, ULONG count, const char *name, struct CAMDEndpointInfoV1 *info)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        memset(info, 0, sizeof(*info));
        info->Size = sizeof(*info);
        info->Version = 1;
        if (GetEndpointInfo(snapshot, i, info) == CAMD_RESULT_OK && !strcmp(info->Name, name)) return (LONG)i;
    }
    return -1;
}

/* A published UMP endpoint: complete packets end to end, nothing converted. */
static void test_ump_endpoint(void)
{
    static char name[] = "camdcompat ump";
    UBYTE buffer[8];
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 port, snapshot, to_port, from_port, refused;
    struct CAMDEndpointIDV1 id;
    struct CAMDEndpointInfoV1 info;
    struct CAMDGroupInfoV1 groups[2], group;
    struct CAMDFunctionBlockInfoV1 block, got_block;
    struct CAMDUMPEventV1 events[3], event;
    struct CAMDMIDI1EventV1 midi;
    ULONG count = 0, length = 0;
    LONG index, result;

    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = CAMD_DIRECTION_INPUT | CAMD_DIRECTION_OUTPUT;
    publish.Signal = CAMD_SIGNAL_NONE;
    strcpy(publish.Name, name);
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "an endpoint without a data format is CAMD_RESULT_INVALID", NULL);
    publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    publish.Protocol = CAMD_PROTOCOL_MIDI2;
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "MIDI 2.0 protocol without UMP is CAMD_RESULT_INVALID", NULL);
    publish.DataFormat = CAMD_DATA_FORMAT_UMP;
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "PublishEndpoint publishes a UMP endpoint", NULL);
    if (result != CAMD_RESULT_OK) return;
    check(!FindCluster("camdcompat ump.out.0") && !FindCluster("camdcompat ump.in.0"), "a UMP endpoint has no legacy clusters", NULL);

    /* Groups and Function Blocks. */
    memset(groups, 0, sizeof(groups));
    groups[0].Size = groups[1].Size = sizeof(groups[0]);
    groups[0].Version = groups[1].Version = 1;
    groups[0].Group = 0;
    groups[1].Group = 5;
    strcpy(groups[0].Name, "Main");
    strcpy(groups[1].Name, "Aux");
    memset(&block, 0, sizeof(block));
    block.Size = sizeof(block);
    block.Version = 1;
    block.Number = 3;
    block.FirstGroup = 5;
    block.GroupCount = 1;
    strcpy(block.Name, "Synth");
    check(SetPublishedEndpointTopology(&port, groups, 2, &block, 1) == CAMD_RESULT_OK, "SetPublishedEndpointTopology sets Groups and a Function Block", NULL);
    block.FirstGroup = 15;
    block.GroupCount = 2;
    check(SetPublishedEndpointTopology(&port, groups, 2, &block, 1) == CAMD_RESULT_INVALID, "a block that spans past Group 15 is CAMD_RESULT_INVALID", NULL);
    groups[1].Group = 0;
    check(SetPublishedEndpointTopology(&port, groups, 2, NULL, 0) == CAMD_RESULT_DUPLICATE, "the same Group twice is CAMD_RESULT_DUPLICATE", NULL);

    if (ObtainEndpointSnapshot(&snapshot, &count, NULL) == CAMD_RESULT_OK) {
        index = endpoint_index(&snapshot, count, name, &info);
        check(index >= 0 && info.NativeDataFormats == CAMD_DATA_FORMAT_UMP && info.CurrentProtocol == CAMD_PROTOCOL_MIDI2,
              "the snapshot shows it as UMP with the MIDI 2.0 protocol", NULL);
        if (index >= 0) {
            memset(&group, 0, sizeof(group));
            group.Size = sizeof(group);
            group.Version = 1;
            check(GetEndpointGroup(&snapshot, (ULONG)index, 1, &group) == CAMD_RESULT_OK && group.Group == 5 &&
                  !strcmp(group.Name, "Aux") && !memcmp(&group.EndpointID, &id, sizeof(id)),
                  "GetEndpointGroup reads the Groups that were set before the refused ones", NULL);
            group.Size = sizeof(group);
            check(GetEndpointGroup(&snapshot, (ULONG)index, 2, &group) == CAMD_RESULT_OUT_OF_RANGE, "past the last Group is CAMD_RESULT_OUT_OF_RANGE", NULL);
            memset(&got_block, 0, sizeof(got_block));
            got_block.Size = sizeof(got_block);
            got_block.Version = 1;
            check(GetEndpointFunctionBlock(&snapshot, (ULONG)index, 0, &got_block) == CAMD_RESULT_OK && got_block.Number == 3 &&
                  got_block.FirstGroup == 5 && got_block.GroupCount == 1 && !strcmp(got_block.Name, "Synth"),
                  "GetEndpointFunctionBlock reads the block", NULL);
            got_block.Size = sizeof(got_block);
            check(GetEndpointFunctionBlock(&snapshot, (ULONG)index, 1, &got_block) == CAMD_RESULT_OUT_OF_RANGE, "past the last block is CAMD_RESULT_OUT_OF_RANGE", NULL);
        }
        index = endpoint_index(&snapshot, count, "loopback.0", &info);
        group.Size = sizeof(group);
        check(index < 0 || GetEndpointGroup(&snapshot, (ULONG)index, 0, &group) == CAMD_RESULT_OUT_OF_RANGE, "a MIDI 1.0 port has no Groups", NULL);
        ReleaseEndpointSnapshot(&snapshot);
    }

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.QueueCapacity = 8;
    request.Signal = CAMD_SIGNAL_NONE;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    check(OpenEndpointSession(&request, &refused, NULL) == CAMD_RESULT_UNSUPPORTED, "a MIDI 1.0 session on a UMP endpoint is CAMD_RESULT_UNSUPPORTED", NULL);
    request.DataFormat = CAMD_DATA_FORMAT_UMP;
    request.Protocol = CAMD_PROTOCOL_MIDI2;
    result = OpenEndpointSession(&request, &to_port, NULL);
    request.Direction = CAMD_DIRECTION_INPUT;
    if (result == CAMD_RESULT_OK) {
        result = OpenEndpointSession(&request, &from_port, NULL);
        if (result != CAMD_RESULT_OK) CloseEndpointSession(&to_port);
    }
    check(result == CAMD_RESULT_OK, "UMP sessions open in both directions", NULL);
    if (result != CAMD_RESULT_OK) goto out;

    /* Client to publisher: a MIDI 2.0 note on and off, 64 bits each. */
    events[0] = ump(0x40903c00UL, 0xc0000000UL, 2);
    events[1] = ump(0x40803c00UL, 0x40000000UL, 2);
    check(PutEndpointUMP(&to_port, events, 2) == CAMD_RESULT_OK, "PutEndpointUMP queues two messages", NULL);
    memset(&event, 0, sizeof(event));
    check(GetPublishedUMP(&port, &event) == CAMD_RESULT_OK && event.WordCount == 2 && event.Words[0] == 0x40903c00UL &&
          event.Words[1] == 0xc0000000UL, "GetPublishedUMP takes the first, word for word", NULL);
    check(GetPublishedUMP(&port, &event) == CAMD_RESULT_OK && event.Words[0] == 0x40803c00UL && event.Words[1] == 0x40000000UL &&
          GetPublishedUMP(&port, &event) == CAMD_RESULT_EMPTY, "and the second, then nothing", NULL);
    events[0].WordCount = 1;
    check(PutEndpointUMP(&to_port, events, 1) == CAMD_RESULT_INVALID, "a word count that does not fit the message type is CAMD_RESULT_INVALID", NULL);
    /* Channel voice messages of the other protocol are refused (B18);
       a system message is of neither and passes. */
    events[0] = ump(0x20903c40UL, 0, 1);
    check(PutEndpointUMP(&to_port, events, 1) == CAMD_RESULT_UNSUPPORTED &&
          PutPublishedUMP(&port, events, 1) == CAMD_RESULT_UNSUPPORTED,
          "a MIDI 1.0 channel voice message on a MIDI 2.0 protocol endpoint is CAMD_RESULT_UNSUPPORTED", NULL);
    events[0] = ump(0x10f80000UL, 0, 1);
    check(PutEndpointUMP(&to_port, events, 1) == CAMD_RESULT_OK &&
          GetPublishedUMP(&port, &event) == CAMD_RESULT_OK && event.Words[0] == 0x10f80000UL,
          "a system message passes on it", NULL);
    midi = midi1(0x90, 0x3c, 0x40);
    check(PutEndpointMidi(&to_port, &midi, 1) == CAMD_RESULT_UNSUPPORTED &&
          GetPublishedMidi(&port, &midi, buffer, sizeof(buffer), &length) == CAMD_RESULT_UNSUPPORTED,
          "MIDI 1.0 messages do not pass a UMP session or endpoint", NULL);

    /* Publisher to client, with a 128-bit and a 32-bit message too. */
    events[0] = ump(0x40b00740UL, 0x12345678UL, 2);
    events[1] = ump(0xf0010000UL, 0x00000001UL, 4);
    events[1].Words[2] = 0xaabbccddUL;
    events[1].Words[3] = 0x11223344UL;
    events[2] = ump(0x10f80000UL, 0, 1);
    check(PutPublishedUMP(&port, events, 3) == CAMD_RESULT_OK, "PutPublishedUMP sends 64, 128 and 32 bit messages", NULL);
    memset(&event, 0, sizeof(event));
    check(GetEndpointUMP(&from_port, &event) == CAMD_RESULT_OK && event.WordCount == 2 && event.Words[1] == 0x12345678UL &&
          (event.Flags & CAMD_EVENT_TIME_VALID) && event.ClockDomain == CAMD_CLOCK_CAMD,
          "GetEndpointUMP receives the first, with its arrival time", NULL);
    check(GetEndpointUMP(&from_port, &event) == CAMD_RESULT_OK && event.WordCount == 4 && event.Words[0] == 0xf0010000UL &&
          event.Words[2] == 0xaabbccddUL && event.Words[3] == 0x11223344UL, "the 128-bit message keeps all four words", NULL);
    check(GetEndpointUMP(&from_port, &event) == CAMD_RESULT_OK && event.WordCount == 1 && event.Words[0] == 0x10f80000UL &&
          GetEndpointUMP(&from_port, &event) == CAMD_RESULT_EMPTY, "and the 32-bit one follows, then nothing", NULL);
    check(GetEndpointMidi(&from_port, &midi, buffer, sizeof(buffer), &length) == CAMD_RESULT_UNSUPPORTED,
          "GetEndpointMidi on a UMP session is CAMD_RESULT_UNSUPPORTED", NULL);
    check(PutPublishedMidi(&port, &midi, 1) == CAMD_RESULT_INVALID, "the publisher cannot send MIDI 1.0 messages on it", NULL);

    check(CloseEndpointSession(&to_port) == CAMD_RESULT_OK && CloseEndpointSession(&from_port) == CAMD_RESULT_OK,
          "the UMP sessions close", NULL);
out:
    WithdrawEndpoint(&port);
}

static struct CAMDPublishRequestV1 publish_request(const char *name, ULONG directions)
{
    struct CAMDPublishRequestV1 publish;

    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = directions;
    publish.Signal = CAMD_SIGNAL_NONE;
    publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    publish.Protocol = CAMD_PROTOCOL_MIDI1;
    strcpy(publish.Name, name);
    return publish;
}

/* What the snapshot tells about a named endpoint. */
static int endpoint_info(const char *name, struct CAMDEndpointInfoV1 *info, struct CAMDGenerationV1 *generation)
{
    struct CAMDHandleV1 snapshot;
    ULONG count = 0;
    LONG index;

    if (ObtainEndpointSnapshot(&snapshot, &count, generation) != CAMD_RESULT_OK) return 0;
    index = endpoint_index(&snapshot, count, name, info);
    ReleaseEndpointSnapshot(&snapshot);
    return index >= 0;
}

static int later(const struct CAMDGenerationV1 *a, const struct CAMDGenerationV1 *b)
{
    return a->high > b->high || (a->high == b->high && a->low > b->low);
}

/* Names, directions, generations, bounded drains, counters and temporary IDs. */
static void test_endpoint_limits(void)
{
    static char long_name[] = "camdcompat a name that is too long to make cluster names from";
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDPublishedStatsV1 stats;
    struct CAMDEndpointInfoV1 info;
    struct CAMDEndpointWatchEventV1 change;
    struct CAMDGenerationV1 before, after;
    struct CAMDHandleV1 port, other, session, watch;
    struct CAMDEndpointIDV1 id, second;
    struct CAMDMIDI1EventV1 event;
    struct MidiNode *node;
    struct MidiLink *sender;
    UBYTE buffer[8];
    ULONG length = 0, started, i, stored = 0, temporary = 0;
    LONG result;

    /* Names. */
    publish = publish_request("camdcompat\x01" "bell", CAMD_DIRECTION_OUTPUT);
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "a name with a control character is CAMD_RESULT_INVALID", NULL);
    publish = publish_request(" camdcompat", CAMD_DIRECTION_OUTPUT);
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "a name that starts with a space is CAMD_RESULT_INVALID", NULL);
    publish = publish_request(long_name, CAMD_DIRECTION_OUTPUT);
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "a MIDI 1.0 name too long for its cluster names is CAMD_RESULT_INVALID", NULL);
    publish.DataFormat = CAMD_DATA_FORMAT_UMP;
    result = PublishEndpoint(&publish, &port, NULL);
    check(result == CAMD_RESULT_OK, "the same name is fine for a UMP endpoint, which has no clusters", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&port);
    publish = publish_request("camdcompat limits", CAMD_DIRECTION_OUTPUT);
    publish.Flags = 0x80;
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "an unknown publish flag is CAMD_RESULT_INVALID", NULL);

    /* Generations and directions. */
    publish.Flags = 0;
    if (StartEndpointWatch(&watch, CAMD_SIGNAL_NONE, NULL) != CAMD_RESULT_OK || !endpoint_info("loopback.0", &info, &before)) {
        check(0, "limits: watch and snapshot", NULL);
        return;
    }
    check(info.Flags == (CAMD_ENDPOINT_FLAG_INPUT | CAMD_ENDPOINT_FLAG_OUTPUT), "a driver port's Flags show both directions", NULL);
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "limits: an output-only endpoint is published", NULL);
    if (result != CAMD_RESULT_OK) {
        EndEndpointWatch(&watch);
        return;
    }
    check(endpoint_info("camdcompat limits", &info, &after) && info.Flags == CAMD_ENDPOINT_FLAG_OUTPUT &&
          info.IdentityKind == CAMD_IDENTITY_STORED_BY_NAME, "its Flags show output only, and its ID is stored by name", NULL);
    memset(&change, 0, sizeof(change));
    check(later(&after, &before) && GetEndpointWatchEvent(&watch, &change) == CAMD_RESULT_OK &&
          change.Type == CAMD_ENDPOINT_EVENT_ADDED && later(&change.Generation, &before) && !later(&change.Generation, &after),
          "the snapshot's generation places it before and after the watch's event", NULL);
    EndEndpointWatch(&watch);

    /* A drain with a limit, on a publisher that does not read. */
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 4;
    request.Signal = CAMD_SIGNAL_NONE;
    result = OpenEndpointSession(&request, &session, NULL);
    event = midi1(0x97, 0x01, 0x01);
    if (result == CAMD_RESULT_OK) result = PutEndpointMidi(&session, &event, 1);
    check(result == CAMD_RESULT_OK, "limits: a client sends to it", NULL);
    if (result == CAMD_RESULT_OK) {
        started = CamdTime();
        check(DrainEndpointSession(&session, 0) == CAMD_RESULT_QUEUE_FULL && CamdTime() - started < 100,
              "a drain of 0 ms only looks", NULL);
        started = CamdTime();
        check(DrainEndpointSession(&session, 150) == CAMD_RESULT_QUEUE_FULL && CamdTime() - started >= 150 &&
              CamdTime() - started < 1500, "a drain of 150 ms gives up after that", NULL);
        check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK &&
              DrainEndpointSession(&session, 150) == CAMD_RESULT_OK, "and succeeds once the publisher has read", NULL);
        CloseEndpointSession(&session);
    }

    /* A drain by a caller that holds Forbid(): its waits let the session's
       worker run, so it ends; it must not spin with the system stopped. */
    if (find_endpoint("debugdriver.1", &second)) {
        request.EndpointID = second;
        request.QueueCapacity = 8;
        result = OpenEndpointSession(&request, &session, NULL);
        if (result == CAMD_RESULT_OK) {
            for (i = 0; i < 6; i++) {
                event = midi1(0x98, (UBYTE)i, 0x01);
                event.Flags = CAMD_EVENT_TIME_VALID;
                event.ClockDomain = CAMD_CLOCK_CAMD;
                event.TimeLow = CamdTime() + 60;
                PutEndpointMidi(&session, &event, 1);
            }
            started = CamdTime();
            Forbid();
            result = DrainEndpointSession(&session, 3000);
            Permit();
            check(result == CAMD_RESULT_OK && CamdTime() - started >= 50 && CamdTime() - started < 2000,
                  "a drain under Forbid() waits for the session's worker and ends", NULL);
            CloseEndpointSession(&session);
        } else {
            check(0, "limits: a session to a driver port", NULL);
        }
        request.EndpointID = id;
        request.QueueCapacity = 4;
    }

    /* A legacy link floods the publisher: the loss is counted. */
    node = new_node("camdcompat limits", 32, 256);
    sender = node ? link_to(node, MLTYPE_Sender, "camdcompat limits.out.0") : NULL;
    if (sender) {
        for (i = 0; i < 70; i++) PutMidi(sender, 0x97000000UL | (i << 16) | 0x100);
        for (i = 0; GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK; i++);
        memset(&stats, 0, sizeof(stats));
        stats.Size = sizeof(stats);
        stats.Version = 1;
        check(GetPublishedEndpointStats(&port, &stats) == CAMD_RESULT_OK && i == 64 && stats.LegacyLost == 6 &&
              stats.Taken == 65 && stats.Emitted == 0, "GetPublishedEndpointStats counts what legacy links lost and what was taken", NULL);
        RemoveMidiLink(sender);
    } else {
        check(0, "limits: a legacy link to the endpoint", NULL);
    }
    if (node) DeleteMidi(node);
    WithdrawEndpoint(&port);

    /* Temporary IDs: asked for, and when the store is full. */
    publish = publish_request("camdcompat temporary", CAMD_DIRECTION_OUTPUT);
    publish.Flags = CAMD_PUBLISH_TEMPORARY;
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK && endpoint_info("camdcompat temporary", &info, NULL) &&
          info.IdentityKind == CAMD_IDENTITY_TEMPORARY, "an endpoint published as temporary says so", NULL);
    if (result == CAMD_RESULT_OK) {
        WithdrawEndpoint(&port);
        result = PublishEndpoint(&publish, &other, &second);
        check(result == CAMD_RESULT_OK && memcmp(&id, &second, sizeof(id)) != 0, "and gets another ID the next time", NULL);
        if (result == CAMD_RESULT_OK) WithdrawEndpoint(&other);
    }
    for (i = 0; i < 300; i++) {
        publish = publish_request("", CAMD_DIRECTION_OUTPUT);
        sprintf(publish.Name, "camdcompat fill %03lu", (unsigned long)i);
        if (PublishEndpoint(&publish, &port, NULL) != CAMD_RESULT_OK) break;
        if (endpoint_info(publish.Name, &info, NULL)) {
            if (info.IdentityKind == CAMD_IDENTITY_TEMPORARY) temporary++;
            else stored++;
        }
        WithdrawEndpoint(&port);
    }
    check(i == 300, "three hundred names can be published one after another", NULL);
    check(temporary > 0 && stored + temporary == 300 && info.IdentityKind == CAMD_IDENTITY_TEMPORARY,
          "once the store of IDs is full the rest get temporary ones", NULL);
    check(endpoint_info("loopback.0", &info, NULL) && info.IdentityKind == CAMD_IDENTITY_STORED_BY_PATH,
          "a driver port keeps its stored ID", NULL);
}

/* A publisher in another task: it takes what clients sent a little later. */
static struct CAMDHandleV1 taker_port;
static volatile int taker_done;

static void taker_entry(void)
{
    struct CAMDMIDI1EventV1 event;
    UBYTE buffer[8];
    ULONG length;

    Delay(6);
    while (GetPublishedMidi(&taker_port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK);
    taker_done = 1;
}

/* A published endpoint that has CAMD keep messages until their time, and a
   drain that is woken when another task has read. */
static void test_published_hold(void)
{
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 port, session;
    struct CAMDEndpointIDV1 id;
    struct CAMDMIDI1EventV1 event, events[2];
    UBYTE buffer[8];
    ULONG length, target, started, took;
    BYTE bit = AllocSignal(-1);
    LONG result;
    int tries;

    if (bit < 0) {
        check(0, "hold: AllocSignal", NULL);
        return;
    }
    publish = publish_request("camdcompat hold", CAMD_DIRECTION_OUTPUT);
    publish.Flags = CAMD_PUBLISH_TEMPORARY | CAMD_PUBLISH_HOLD_UNTIL_DUE;
    publish.DataFormat = CAMD_DATA_FORMAT_UMP;
    check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_INVALID, "holding until due is CAMD_RESULT_INVALID for a UMP endpoint", NULL);
    publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    publish.Signal = (ULONG)bit;
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "an endpoint that holds messages until they are due is published", NULL);
    if (result != CAMD_RESULT_OK) {
        FreeSignal(bit);
        return;
    }
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 8;
    request.Signal = CAMD_SIGNAL_NONE;
    result = OpenEndpointSession(&request, &session, NULL);
    check(result == CAMD_RESULT_OK, "hold: a client opens a session", NULL);
    if (result != CAMD_RESULT_OK) goto out;

    /* One for 250 ms from now, then one without a time. */
    SetSignal(0, 1UL << bit);
    target = CamdTime() + 250;
    events[0] = midi1(0x99, 0x01, 0x40);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = CAMD_CLOCK_CAMD;
    events[0].TimeLow = target;
    events[1] = midi1(0x99, 0x02, 0x40);
    check(PutEndpointMidi(&session, events, 2) == CAMD_RESULT_OK, "the client sends a timed and an untimed message", NULL);
    result = CAMD_RESULT_EMPTY;
    for (tries = 0; tries < 50 && result == CAMD_RESULT_EMPTY; tries++) {
        result = GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length);
        if (result == CAMD_RESULT_EMPTY) Delay(1);
    }
    check(result == CAMD_RESULT_OK && event.Bytes[1] == 0x02 && (LONG)(target - CamdTime()) > 0,
          "the publisher takes the untimed one at once", NULL);
    SetSignal(0, 1UL << bit);
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_EMPTY &&
          DrainEndpointSession(&session, 0) == CAMD_RESULT_QUEUE_FULL && (LONG)(target - CamdTime()) > 0,
          "the timed one is not there to take before its time, and the session is not drained", NULL);
    Wait(1UL << bit);
    took = CamdTime();
    check(GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Bytes[1] == 0x01 &&
          (LONG)(took - target) >= 0 && took - target < 250, "the publisher is signalled and takes it when its time has come", NULL);
    check(DrainEndpointSession(&session, 500) == CAMD_RESULT_OK, "the session is then drained", NULL);

    /* Another task reads 120 ms from now: the drain returns then, not at
       the end of its three seconds or of a one-second look. */
    events[0] = midi1(0x99, 0x03, 0x40);
    taker_port = port;
    taker_done = 0;
    if (PutEndpointMidi(&session, events, 1) == CAMD_RESULT_OK &&
        CreateNewProcTags(NP_Entry, (IPTR)taker_entry, NP_Name, (IPTR)"camdcompat taker", NP_Priority, 0, TAG_END)) {
        started = CamdTime();
        result = DrainEndpointSession(&session, 3000);
        took = CamdTime() - started;
        check(result == CAMD_RESULT_OK && took >= 80 && took < 700, "a drain is woken when another task has taken the message", NULL);
        for (tries = 0; tries < 100 && !taker_done; tries++) Delay(1);
    } else {
        check(0, "hold: the taking task starts", NULL);
    }

    /* Withdrawing wakes a drain too. */
    events[0] = midi1(0x99, 0x04, 0x40);
    events[0].Flags = CAMD_EVENT_TIME_VALID;
    events[0].ClockDomain = CAMD_CLOCK_CAMD;
    events[0].TimeLow = CamdTime() + 60000;
    check(PutEndpointMidi(&session, events, 1) == CAMD_RESULT_OK, "a message for a minute from now is queued", NULL);
    WithdrawEndpoint(&port);
    started = CamdTime();
    check(DrainEndpointSession(&session, 3000) != CAMD_RESULT_QUEUE_FULL && CamdTime() - started < 700,
          "a drain does not wait for a withdrawn endpoint", NULL);
    check(CloseEndpointSession(&session) == CAMD_RESULT_OK, "hold: the session closes with a message still waiting", NULL);
    FreeSignal(bit);
    return;
out:
    WithdrawEndpoint(&port);
    FreeSignal(bit);
}

/* Stress: several tasks use the endpoint functions at once. Each counts the
   rounds it finished and the results it did not expect. */
#define STRESS_TASKS 6
#define STRESS_ROUNDS 150

static struct CAMDEndpointIDV1 stress_port;
static volatile ULONG stress_rounds[STRESS_TASKS], stress_errors[STRESS_TASKS], stress_done[STRESS_TASKS];
static volatile ULONG stress_received, stress_sent, stress_refused;
static volatile LONG stress_next;

static struct CAMDSessionRequestV1 stress_request(ULONG direction)
{
    struct CAMDSessionRequestV1 request;

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = stress_port;
    request.Direction = direction;
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 16;
    request.Signal = CAMD_SIGNAL_NONE;
    return request;
}

/* Opens an output session on the loopback port, sends and closes. */
static void stress_sender(ULONG me)
{
    struct CAMDSessionRequestV1 request = stress_request(CAMD_DIRECTION_OUTPUT);
    struct CAMDHandleV1 session;
    struct CAMDMIDI1EventV1 events[4];
    ULONG round, i;
    LONG result;

    for (round = 0; round < STRESS_ROUNDS; round++) {
        if (OpenEndpointSession(&request, &session, NULL) != CAMD_RESULT_OK) {
            stress_errors[me]++;
            continue;
        }
        for (i = 0; i < 4; i++) events[i] = midi1(0x90 | (UBYTE)me, (UBYTE)(round & 0x7f), (UBYTE)i);
        result = PutEndpointMidi(&session, events, 4);
        if (result == CAMD_RESULT_OK) stress_sent += 4;
        else if (result == CAMD_RESULT_QUEUE_FULL) stress_refused += 4;
        else stress_errors[me]++;
        result = DrainEndpointSession(&session, 2000);
        if (result != CAMD_RESULT_OK) stress_errors[me]++;
        if (CloseEndpointSession(&session) != CAMD_RESULT_OK) stress_errors[me]++;
        stress_rounds[me]++;
    }
}

/* Opens an input session on the loopback port, reads what there is, closes. */
static void stress_receiver(ULONG me)
{
    struct CAMDSessionRequestV1 request = stress_request(CAMD_DIRECTION_INPUT);
    struct CAMDHandleV1 session;
    struct CAMDSessionStatsV1 stats;
    struct CAMDMIDI1EventV1 event;
    UBYTE buffer[16];
    ULONG round, length, tries;
    LONG result;

    request.QueueCapacity = 64;
    for (round = 0; round < STRESS_ROUNDS; round++) {
        if (OpenEndpointSession(&request, &session, NULL) != CAMD_RESULT_OK) {
            stress_errors[me]++;
            continue;
        }
        for (tries = 0; tries < 20; tries++) {
            result = GetEndpointMidi(&session, &event, buffer, sizeof(buffer), &length);
            if (result == CAMD_RESULT_OK) {
                if (event.Length != 3 || (event.Bytes[0] & 0xf0) != 0x90) stress_errors[me]++;
                stress_received++;
            } else if (result != CAMD_RESULT_EMPTY && result != CAMD_RESULT_QUEUE_FULL) {
                stress_errors[me]++;
            }
        }
        memset(&stats, 0, sizeof(stats));
        stats.Size = sizeof(stats);
        stats.Version = 1;
        if (GetEndpointSessionStats(&session, &stats) != CAMD_RESULT_OK) stress_errors[me]++;
        if (CloseEndpointSession(&session) != CAMD_RESULT_OK) stress_errors[me]++;
        stress_rounds[me]++;
    }
}

/* Publishes an endpoint, is its own client both ways, withdraws it. */
static void stress_publisher(ULONG me)
{
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 port, out, in;
    struct CAMDEndpointIDV1 id;
    struct CAMDMIDI1EventV1 event;
    UBYTE buffer[16];
    ULONG round, length;

    for (round = 0; round < STRESS_ROUNDS; round++) {
        publish = publish_request("", CAMD_DIRECTION_INPUT | CAMD_DIRECTION_OUTPUT);
        sprintf(publish.Name, "camdcompat stress %lu", (unsigned long)me);
        publish.Flags = CAMD_PUBLISH_TEMPORARY;
        if (PublishEndpoint(&publish, &port, &id) != CAMD_RESULT_OK) {
            stress_errors[me]++;
            continue;
        }
        request = stress_request(CAMD_DIRECTION_OUTPUT);
        request.EndpointID = id;
        if (OpenEndpointSession(&request, &out, NULL) == CAMD_RESULT_OK) {
            event = midi1(0x95, (UBYTE)(round & 0x7f), 1);
            if (PutEndpointMidi(&out, &event, 1) != CAMD_RESULT_OK) stress_errors[me]++;
            if (GetPublishedMidi(&port, &event, buffer, sizeof(buffer), &length) != CAMD_RESULT_OK ||
                event.Bytes[1] != (UBYTE)(round & 0x7f))
                stress_errors[me]++;
        } else {
            stress_errors[me]++;
            out.generation = 0;
        }
        request.Direction = CAMD_DIRECTION_INPUT;
        if (OpenEndpointSession(&request, &in, NULL) == CAMD_RESULT_OK) {
            event = midi1(0x96, (UBYTE)(round & 0x7f), 2);
            if (PutPublishedMidi(&port, &event, 1) != CAMD_RESULT_OK) stress_errors[me]++;
            if (GetEndpointMidi(&in, &event, buffer, sizeof(buffer), &length) != CAMD_RESULT_OK ||
                event.Bytes[0] != 0x96)
                stress_errors[me]++;
        } else {
            stress_errors[me]++;
            in.generation = 0;
        }
        /* Withdraw with the sessions still open on every other round. */
        if (round & 1) {
            if (out.generation) CloseEndpointSession(&out);
            if (in.generation) CloseEndpointSession(&in);
            WithdrawEndpoint(&port);
        } else {
            WithdrawEndpoint(&port);
            if (out.generation && CloseEndpointSession(&out) != CAMD_RESULT_OK) stress_errors[me]++;
            if (in.generation && CloseEndpointSession(&in) != CAMD_RESULT_OK) stress_errors[me]++;
        }
        stress_rounds[me]++;
    }
}

/* A UMP endpoint of the MIDI 1.0 protocol does not take MIDI 2.0 channel
   voice messages (backlog B18). */
static void test_ump_protocol(void)
{
    struct CAMDPublishRequestV1 publish;
    struct CAMDSessionRequestV1 request;
    struct CAMDHandleV1 port, to_port;
    struct CAMDEndpointIDV1 id;
    struct CAMDUMPEventV1 event;
    LONG result;

    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = CAMD_DIRECTION_OUTPUT;
    publish.Signal = CAMD_SIGNAL_NONE;
    publish.DataFormat = CAMD_DATA_FORMAT_UMP;
    publish.Protocol = CAMD_PROTOCOL_MIDI1;
    publish.Flags = CAMD_PUBLISH_TEMPORARY;
    strcpy(publish.Name, "camdcompat ump midi1");
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "a UMP endpoint of the MIDI 1.0 protocol is published", NULL);
    if (result != CAMD_RESULT_OK) return;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = 1;
    request.EndpointID = id;
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.DataFormat = CAMD_DATA_FORMAT_UMP;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.QueueCapacity = 4;
    request.Signal = CAMD_SIGNAL_NONE;
    result = OpenEndpointSession(&request, &to_port, NULL);
    check(result == CAMD_RESULT_OK, "and a session with it opens", NULL);
    if (result == CAMD_RESULT_OK) {
        event = ump(0x40903c00UL, 0xc0000000UL, 2);
        check(PutEndpointUMP(&to_port, &event, 1) == CAMD_RESULT_UNSUPPORTED,
              "a MIDI 2.0 channel voice message on it is CAMD_RESULT_UNSUPPORTED", NULL);
        event = ump(0x20903c40UL, 0, 1);
        check(PutEndpointUMP(&to_port, &event, 1) == CAMD_RESULT_OK &&
              GetPublishedUMP(&port, &event) == CAMD_RESULT_OK && event.Words[0] == 0x20903c40UL &&
              GetPublishedUMP(&port, &event) == CAMD_RESULT_EMPTY,
              "a MIDI 1.0 channel voice message passes, and nothing of the refused one arrived", NULL);
        CloseEndpointSession(&to_port);
    }
    WithdrawEndpoint(&port);
}

/* Group and Function Block flags have defined values and are checked
   (backlog B17); endpoint names are one whatever their case (B23). */
static void test_flags_and_names(void)
{
    struct CAMDPublishRequestV1 publish;
    struct CAMDHandleV1 port, other, snapshot;
    struct CAMDEndpointIDV1 id, again;
    struct CAMDEndpointInfoV1 info;
    struct CAMDGroupInfoV1 group;
    struct CAMDFunctionBlockInfoV1 block;
    ULONG count = 0;
    LONG index, result;
    const ULONG block_flags = CAMD_BLOCK_FLAG_OUTPUT | CAMD_BLOCK_FLAG_ACTIVE | CAMD_BLOCK_FLAG_STATIC |
                              CAMD_BLOCK_FLAG_UI_OUTPUT | CAMD_BLOCK_FLAG_MIDI1 | CAMD_BLOCK_FLAG_MIDI1_31250;

    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = CAMD_DIRECTION_OUTPUT;
    publish.Signal = CAMD_SIGNAL_NONE;
    publish.DataFormat = CAMD_DATA_FORMAT_UMP;
    publish.Protocol = CAMD_PROTOCOL_MIDI2;
    publish.Flags = CAMD_PUBLISH_TEMPORARY;
    strcpy(publish.Name, "camdcompat flags");
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "flags: a UMP endpoint is published", NULL);
    if (result == CAMD_RESULT_OK) {
        memset(&group, 0, sizeof(group));
        group.Size = sizeof(group);
        group.Version = 1;
        group.EndpointID = id;
        group.Group = 0;
        group.Flags = CAMD_GROUP_FLAG_INPUT | CAMD_GROUP_FLAG_OUTPUT;
        group.Protocol = CAMD_PROTOCOL_MIDI2;
        strcpy(group.Name, "Main");
        memset(&block, 0, sizeof(block));
        block.Size = sizeof(block);
        block.Version = 1;
        block.EndpointID = id;
        block.Number = 1;
        block.FirstGroup = 0;
        block.GroupCount = 1;
        block.Flags = block_flags;
        strcpy(block.Name, "Keys");
        check(SetPublishedEndpointTopology(&port, &group, 1, &block, 1) == CAMD_RESULT_OK,
              "a Group and a Function Block with every defined flag are accepted", NULL);
        if (ObtainEndpointSnapshot(&snapshot, &count, NULL) == CAMD_RESULT_OK) {
            index = endpoint_index(&snapshot, count, "camdcompat flags", &info);
            memset(&group, 0, sizeof(group));
            group.Size = sizeof(group);
            group.Version = 1;
            memset(&block, 0, sizeof(block));
            block.Size = sizeof(block);
            block.Version = 1;
            check(index >= 0 && GetEndpointGroup(&snapshot, (ULONG)index, 0, &group) == CAMD_RESULT_OK &&
                  group.Flags == (CAMD_GROUP_FLAG_INPUT | CAMD_GROUP_FLAG_OUTPUT) &&
                  GetEndpointFunctionBlock(&snapshot, (ULONG)index, 0, &block) == CAMD_RESULT_OK &&
                  block.Flags == block_flags,
                  "the snapshot gives the flags back as they were set", NULL);
            ReleaseEndpointSnapshot(&snapshot);
        }
        group.Size = sizeof(group);
        group.EndpointID = id;
        block.Size = sizeof(block);
        block.EndpointID = id;
        block.Flags = block_flags | (1UL << 8);
        check(SetPublishedEndpointTopology(&port, &group, 1, &block, 1) == CAMD_RESULT_INVALID,
              "an undefined Function Block flag is CAMD_RESULT_INVALID", NULL);
        block.Flags = CAMD_BLOCK_FLAG_MIDI1_31250;
        check(SetPublishedEndpointTopology(&port, &group, 1, &block, 1) == CAMD_RESULT_INVALID,
              "the 31.25 kbit/s flag without the MIDI 1.0 flag is CAMD_RESULT_INVALID", NULL);
        block.Flags = 0;
        group.Flags = 1UL << 2;
        check(SetPublishedEndpointTopology(&port, &group, 1, &block, 1) == CAMD_RESULT_INVALID,
              "an undefined Group flag is CAMD_RESULT_INVALID", NULL);
        WithdrawEndpoint(&port);
    }

    memset(&publish, 0, sizeof(publish));
    publish.Size = sizeof(publish);
    publish.Version = 1;
    publish.Directions = CAMD_DIRECTION_OUTPUT;
    publish.Signal = CAMD_SIGNAL_NONE;
    publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    publish.Protocol = CAMD_PROTOCOL_MIDI1;
    strcpy(publish.Name, "CamdCompat Case");
    memset(&id, 0, sizeof(id));
    result = PublishEndpoint(&publish, &port, &id);
    check(result == CAMD_RESULT_OK, "names: an endpoint with capitals in its name is published", NULL);
    if (result != CAMD_RESULT_OK) return;
    strcpy(publish.Name, "camdcompat case");
    result = PublishEndpoint(&publish, &other, NULL);
    check(result == CAMD_RESULT_DUPLICATE, "the same name in another case is CAMD_RESULT_DUPLICATE while it is published", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&other);
    publish.Flags = CAMD_PUBLISH_TEMPORARY;
    result = PublishEndpoint(&publish, &other, NULL);
    check(result == CAMD_RESULT_DUPLICATE, "also when a temporary ID is asked for", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&other);
    WithdrawEndpoint(&port);
    publish.Flags = 0;
    strcpy(publish.Name, "CAMDCOMPAT CASE");
    memset(&again, 0, sizeof(again));
    result = PublishEndpoint(&publish, &port, &again);
    check(result == CAMD_RESULT_OK && !memcmp(&id, &again, sizeof(id)),
          "published again in another case, it has the same stable ID", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&port);
}

/* A request record may be shorter or longer than today's (backlog B15), and
   the results have fixed public values (B21). */
static void test_endpoint_records(void)
{
    union {
        struct CAMDSessionRequestV1 r;
        UBYTE bytes[sizeof(struct CAMDSessionRequestV1) + 8];
    } req;
    union {
        struct CAMDPublishRequestV1 r;
        UBYTE bytes[sizeof(struct CAMDPublishRequestV1) + 8];
    } pub;
    struct CAMDHandleV1 session, port;
    struct CAMDEndpointIDV1 id;
    struct CAMDSessionInfoV1 got;
    struct CAMDEndpointInfoV1 info;
    struct CAMDHandleV1 snapshot;
    ULONG count = 0, i;
    LONG result;
    int midi1 = 0;

    check(CAMD_RESULT_OK == 0 && CAMD_RESULT_INVALID == 1 && CAMD_RESULT_NO_MEMORY == 2 &&
          CAMD_RESULT_DUPLICATE == 3 && CAMD_RESULT_STALE == 4 && CAMD_RESULT_WRONG_STATE == 5 &&
          CAMD_RESULT_RETIRED == 6 && CAMD_RESULT_OUT_OF_RANGE == 7 && CAMD_RESULT_EMPTY == 8 &&
          CAMD_RESULT_BUSY == 9 && CAMD_RESULT_FAILED == 10 && CAMD_RESULT_UNSUPPORTED == 11 &&
          CAMD_RESULT_QUEUE_FULL == 12 && CAMD_RESULT_TOO_LARGE == 13,
          "the public results have their documented values", NULL);

    if (!find_endpoint("debugdriver.2", &id)) {
        skip("request record sizes", "no DEVS:Midi/debugdriver");
        return;
    }
    memset(&req, 0, sizeof(req));
    req.r.Size = offsetof(struct CAMDSessionRequestV1, MaxSysExBytes);
    req.r.Version = 1;
    req.r.EndpointID = id;
    req.r.Direction = CAMD_DIRECTION_OUTPUT;
    req.r.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    req.r.Protocol = CAMD_PROTOCOL_MIDI1;
    req.r.QueueCapacity = 8;
    req.r.Signal = CAMD_SIGNAL_NONE;
    req.r.MaxSysExBytes = 0xffffffff;   /* beyond Size: not the library's to read */
    memset(&got, 0, sizeof(got));
    got.Size = sizeof(got);
    got.Version = 1;
    result = OpenEndpointSession(&req.r, &session, &got);
    check(result == CAMD_RESULT_OK && got.MaxSysExBytes == CAMD_SYSEX_BYTES_DEFAULT,
          "a session request of the first layout opens, with the default for the field it lacks", NULL);
    if (result == CAMD_RESULT_OK) CloseEndpointSession(&session);

    req.r.Size -= sizeof(ULONG);
    check(OpenEndpointSession(&req.r, &session, NULL) == CAMD_RESULT_INVALID,
          "a session request shorter than the first layout is CAMD_RESULT_INVALID", NULL);

    req.r.Size = sizeof(req.bytes);
    req.r.MaxSysExBytes = 0;
    result = OpenEndpointSession(&req.r, &session, NULL);
    check(result == CAMD_RESULT_OK, "a longer session request whose extra bytes are zero opens", NULL);
    if (result == CAMD_RESULT_OK) CloseEndpointSession(&session);
    req.bytes[sizeof(req.bytes) - 1] = 1;
    result = OpenEndpointSession(&req.r, &session, NULL);
    check(result == CAMD_RESULT_INVALID, "a longer session request that asks for something unknown is CAMD_RESULT_INVALID", NULL);
    if (result == CAMD_RESULT_OK) CloseEndpointSession(&session);

    memset(&pub, 0, sizeof(pub));
    pub.r.Size = offsetof(struct CAMDPublishRequestV1, DataFormat);
    pub.r.Version = 1;
    pub.r.Directions = CAMD_DIRECTION_OUTPUT;
    pub.r.Signal = CAMD_SIGNAL_NONE;
    strcpy(pub.r.Name, "camdcompat short");
    pub.r.DataFormat = 0xffffffff;      /* beyond Size */
    pub.r.Protocol = 0xffffffff;
    pub.r.Flags = 0xffffffff;
    result = PublishEndpoint(&pub.r, &port, NULL);
    if (result == CAMD_RESULT_OK && ObtainEndpointSnapshot(&snapshot, &count, NULL) == CAMD_RESULT_OK) {
        for (i = 0; i < count; i++) {
            memset(&info, 0, sizeof(info));
            info.Size = sizeof(info);
            info.Version = 1;
            if (GetEndpointInfo(&snapshot, i, &info) != CAMD_RESULT_OK) break;
            if (!strcmp(info.Name, "camdcompat short"))
                midi1 = info.NativeDataFormats == CAMD_DATA_FORMAT_MIDI1 && info.CurrentProtocol == CAMD_PROTOCOL_MIDI1;
        }
        ReleaseEndpointSnapshot(&snapshot);
    }
    check(result == CAMD_RESULT_OK && midi1,
          "a publish request of the first layout publishes a MIDI 1.0 endpoint", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&port);

    pub.r.Size = sizeof(pub.bytes);
    pub.r.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    pub.r.Protocol = CAMD_PROTOCOL_MIDI1;
    pub.r.Flags = CAMD_PUBLISH_TEMPORARY;
    result = PublishEndpoint(&pub.r, &port, NULL);
    check(result == CAMD_RESULT_OK, "a longer publish request whose extra bytes are zero publishes", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&port);
    pub.bytes[sizeof(pub.r) + 2] = 0x40;
    result = PublishEndpoint(&pub.r, &port, NULL);
    check(result == CAMD_RESULT_INVALID, "a longer publish request that asks for something unknown is CAMD_RESULT_INVALID", NULL);
    if (result == CAMD_RESULT_OK) WithdrawEndpoint(&port);
}

/* Takes snapshots and reads a watch while the others change things. */
static void stress_observer(ULONG me)
{
    struct CAMDHandleV1 snapshot, watch;
    struct CAMDEndpointInfoV1 info;
    struct CAMDEndpointWatchEventV1 change;
    ULONG round, count, i;
    LONG result;

    if (StartEndpointWatch(&watch, CAMD_SIGNAL_NONE, NULL) != CAMD_RESULT_OK) {
        stress_errors[me]++;
        return;
    }
    for (round = 0; round < STRESS_ROUNDS; round++) {
        count = 0;
        if (ObtainEndpointSnapshot(&snapshot, &count, NULL) != CAMD_RESULT_OK) {
            stress_errors[me]++;
            continue;
        }
        for (i = 0; i < count; i++) {
            memset(&info, 0, sizeof(info));
            info.Size = sizeof(info);
            info.Version = 1;
            if (GetEndpointInfo(&snapshot, i, &info) != CAMD_RESULT_OK || info.Name[0] == 0) stress_errors[me]++;
        }
        ReleaseEndpointSnapshot(&snapshot);
        for (i = 0; i < 40; i++) {
            result = GetEndpointWatchEvent(&watch, &change);
            if (result == CAMD_RESULT_EMPTY) break;
            if (result != CAMD_RESULT_OK) stress_errors[me]++;
        }
        stress_rounds[me]++;
    }
    EndEndpointWatch(&watch);
}

static void stress_entry(void)
{
    ULONG me;

    Forbid();
    me = (ULONG)stress_next++;
    Permit();
    switch (me) {
    case 0:
    case 1: stress_sender(me); break;
    case 2: stress_receiver(me); break;
    case 3:
    case 4: stress_publisher(me); break;
    default: stress_observer(me); break;
    }
    stress_done[me] = 1;
}

static void test_endpoint_stress(void)
{
    struct CAMDHandleV1 in, out;
    struct CAMDSessionRequestV1 request;
    struct CAMDMIDI1EventV1 event;
    UBYTE buffer[16];
    ULONG length, errors = 0, rounds = 0, finished = 0, i, waited;

    if (!find_endpoint("loopback.1", &stress_port)) {
        skip("endpoint stress", "no DEVS:Midi/loopback");
        return;
    }
    stress_next = 0;
    for (i = 0; i < STRESS_TASKS; i++) {
        if (!CreateNewProcTags(NP_Entry, (IPTR)stress_entry, NP_Name, (IPTR)"camdcompat stress", NP_Priority, 0, TAG_END)) {
            check(0, "stress: CreateNewProc", NULL);
            stress_done[i] = 1;
        }
    }
    for (waited = 0; waited < 120 * 50; waited += 5) {
        finished = 0;
        for (i = 0; i < STRESS_TASKS; i++) finished += stress_done[i];
        if (finished == STRESS_TASKS) break;
        Delay(5);
    }
    for (i = 0; i < STRESS_TASKS; i++) {
        errors += stress_errors[i];
        rounds += stress_rounds[i];
    }
    printf("stress: %lu rounds, %lu sent, %lu refused, %lu received, %lu unexpected results\n", (unsigned long)rounds,
           (unsigned long)stress_sent, (unsigned long)stress_refused, (unsigned long)stress_received, (unsigned long)errors);
    check(finished == STRESS_TASKS, "six tasks using the endpoint functions at once all finish", NULL);
    check(rounds == STRESS_TASKS * STRESS_ROUNDS, "every round ran", NULL);
    check(errors == 0, "no call gave a result it should not", NULL);
    if (finished != STRESS_TASKS) return;

    /* The port and the library still work. */
    request = stress_request(CAMD_DIRECTION_INPUT);
    if (OpenEndpointSession(&request, &in, NULL) != CAMD_RESULT_OK) {
        check(0, "after the stress a session opens", NULL);
        return;
    }
    request.Direction = CAMD_DIRECTION_OUTPUT;
    event = midi1(0x9a, 0x55, 0x66);
    check(OpenEndpointSession(&request, &out, NULL) == CAMD_RESULT_OK && PutEndpointMidi(&out, &event, 1) == CAMD_RESULT_OK &&
          read_soon(&in, &event, buffer, sizeof(buffer), &length) == CAMD_RESULT_OK && event.Bytes[0] == 0x9a &&
          event.Bytes[1] == 0x55, "after the stress the port still carries a message", NULL);
    CloseEndpointSession(&out);
    CloseEndpointSession(&in);
}

/* A client that exits with a snapshot, a session and a published endpoint
   left behind. The runner flushes memory and runs --v43 again: after an
   ordinary exit the library has released them and goes, after an exit that
   did not close the library it stays. */
static void test_endpoint_leak(void)
{
    struct CAMDHandleV1 snapshot, session;
    struct CAMDEndpointInfoV1 info;
    struct CAMDSessionRequestV1 request;
    ULONG count = 0, i;
    LONG result = CAMD_RESULT_INVALID;

    if (ObtainEndpointSnapshot(&snapshot, &count, NULL) != CAMD_RESULT_OK) {
        check(0, "leak: ObtainEndpointSnapshot", NULL);
        return;
    }
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        info.Version = 1;
        if (GetEndpointInfo(&snapshot, i, &info) != CAMD_RESULT_OK || strcmp(info.Name, "debugdriver.3")) continue;
        memset(&request, 0, sizeof(request));
        request.Size = sizeof(request);
        request.Version = 1;
        request.EndpointID = info.ID;
        request.Direction = CAMD_DIRECTION_OUTPUT;
        request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
        request.Protocol = CAMD_PROTOCOL_MIDI1;
        request.QueueCapacity = 4;
        request.Signal = CAMD_SIGNAL_NONE;
        result = OpenEndpointSession(&request, &session, NULL);
    }
    check(result == CAMD_RESULT_OK, "a session and a snapshot are left open", NULL);
    {
        struct CAMDPublishRequestV1 publish;
        struct CAMDHandleV1 port;

        memset(&publish, 0, sizeof(publish));
        publish.Size = sizeof(publish);
        publish.Version = 1;
        publish.Directions = CAMD_DIRECTION_OUTPUT;
        publish.Signal = CAMD_SIGNAL_NONE;
        publish.DataFormat = CAMD_DATA_FORMAT_MIDI1;
        publish.Protocol = CAMD_PROTOCOL_MIDI1;
        publish.Flags = CAMD_PUBLISH_TEMPORARY;
        strcpy(publish.Name, "camdcompat left behind");
        check(PublishEndpoint(&publish, &port, NULL) == CAMD_RESULT_OK, "and so is a published endpoint", NULL);
    }
}

static int copy_file(CONST_STRPTR from, CONST_STRPTR to)
{
    static UBYTE buffer[4096];
    BPTR in = Open(from, MODE_OLDFILE), out;
    LONG got;
    int ok = 1;

    if (!in)
        return 0;
    if (!(out = Open(to, MODE_NEWFILE))) {
        Close(in);
        return 0;
    }
    while ((got = Read(in, buffer, sizeof(buffer))) > 0)
        if (Write(out, buffer, got) != got)
            ok = 0;
    if (got < 0)
        ok = 0;
    Close(out);
    Close(in);
    return ok;
}

/* A client links to a driver's cluster before the driver is installed;
   RethinkCAMD() must then load the driver into that cluster. */
static void test_rethink_load(void)
{
    static char in_cluster[] = "debugdriver.in.0";
    static char out_cluster[] = "debugdriver.out.0";
    struct MidiNode *node;
    struct MidiLink *receiver, *sender;

    if (FindCluster("debugdriver.in.1")) {
        skip("RethinkCAMD loads a new driver", "debugdriver was loaded at start");
        return;
    }
    node = new_node("camdcompat rethink", 32, 256);
    receiver = node ? link_to(node, MLTYPE_Receiver, in_cluster) : NULL;
    sender = node ? link_to(node, MLTYPE_Sender, out_cluster) : NULL;
    if (!receiver || !sender) {
        check(0, "rethink: setup", NULL);
        goto out;
    }
    check(!MidiLinkConnected(receiver), "rethink: the client waits in its own cluster", NULL);
    if (!copy_file((CONST_STRPTR)"SYS:camdcompat-debugdriver",
                   (CONST_STRPTR)"DEVS:Midi/debugdriver")) {
        check(0, "rethink: install DEVS:Midi/debugdriver", NULL);
        goto out;
    }
    check(RethinkCAMD() == 0, "RethinkCAMD returns 0", NULL);
    check(count_clusters("debugdriver.in.1") == 1, "RethinkCAMD loads the new driver", NULL);
    check(count_clusters(in_cluster) == 1 && count_clusters(out_cluster) == 1,
          "the driver joins the client's clusters instead of duplicating them", NULL);
    check(MidiLinkConnected(receiver), "the waiting receiver is connected to the driver", NULL);
    PutMidi(sender, 0x903c6400UL);
    check(1, "the waiting sender reaches the driver", NULL);
    RethinkCAMD();
    check(count_clusters("debugdriver.in.1") == 1, "a second RethinkCAMD loads nothing again", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (node) DeleteMidi(node);
    check(FindCluster(in_cluster) != NULL || !receiver,
          "the driver's cluster stays after the client leaves", NULL);
}

/* Milliseconds: CamdTime() from 42 on, the DOS clock (20 ms steps) before. */
static ULONG now_ms(void)
{
    struct DateStamp ds;

    if (CamdBase->lib_Version >= 42)
        return CamdTime();
    DateStamp(&ds);
    return (ULONG)(ds.ds_Minute * 60000 + ds.ds_Tick * 20);
}

/* MIDIHubRouter forwarding: what a user hears as latency. */
static void test_router(char *source, char *destination)
{
    struct MidiNode *out = new_node("camdcompat router out", 32, 256);
    struct MidiNode *in = new_node("camdcompat router in", 32, 256);
    struct MidiLink *sender = NULL, *receiver = NULL;
    MidiMsg msg;
    ULONG worst = 0, start, waited;
    int i, received = 0;

    if (!out || !in) {
        check(0, "router: CreateMidiA", NULL);
        goto out;
    }
    sender = link_to(out, MLTYPE_Sender, source);
    receiver = link_to(in, MLTYPE_Receiver, destination);
    if (!sender || !receiver) {
        check(0, "router: AddMidiLinkA", NULL);
        goto out;
    }
    /* Let the router see both links. */
    Delay(100);
    for (i = 0; i < 10; i++) {
        start = now_ms();
        PutMidi(sender, 0x90000000UL | ((ULONG)(60 + i) << 16) | (100UL << 8));
        for (waited = 0; waited < 1000; waited = now_ms() - start) {
            if (GetMidi(in, &msg))
                break;
            Delay(1);
        }
        if (waited < 1000) {
            received++;
            if (waited > worst)
                worst = waited;
        }
        Delay(10);
    }
    printf("     %d of 10 forwarded, slowest after %lu ms\n", received, (unsigned long)worst);
    check(received == 10, "MIDIHubRouter forwards every message", NULL);
    check(received && worst <= 100, "MIDIHubRouter forwards within 100 ms", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (receiver) RemoveMidiLink(receiver);
    if (out) DeleteMidi(out);
    if (in) DeleteMidi(in);
}

int main(int argc, char **argv)
{
    int rethink_mode = argc == 2 && !strcmp(argv[1], "--rethink");
    int v42_mode = argc == 2 && !strcmp(argv[1], "--v42");
    int v43_mode = argc == 2 && !strcmp(argv[1], "--v43");
    int leak_mode = argc == 2 && (!strcmp(argv[1], "--v43-leak") || !strcmp(argv[1], "--v43-abandon"));
    int abandon_mode = argc == 2 && !strcmp(argv[1], "--v43-abandon");
    int router_mode = argc == 4 && !strcmp(argv[1], "--router");

    /* Unbuffered, so a crash still leaves the checks before it in the log. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 1 && !rethink_mode && !v42_mode && !v43_mode && !leak_mode && !router_mode) {
        puts("Usage: MIDIHubCAMDCompat [--v42 | --v43 | --v43-leak | --v43-abandon | --rethink | --router SOURCE DESTINATION]");
        return 20;
    }
    /* The shell that runs User-Startup at boot has task switching disabled
       while a command runs. Tests that need other tasks to run mean nothing
       there. */
    if (SysBase->TDNestCnt >= 0 && !router_mode) {
        puts("FAIL task switching is disabled in this shell: start the suite with Run");
        return 20;
    }
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) {
        puts("FAIL camd.library unavailable");
        return 20;
    }
    printf("camd.library %d.%d\n", CamdBase->lib_Version, CamdBase->lib_Revision);
    if (router_mode) {
        test_router(argv[2], argv[3]);
    } else if (rethink_mode) {
        if (CamdBase->lib_Version >= 43) start_rethink_watch();
        RUN(test_rethink_load);
        if (CamdBase->lib_Version >= 43) RUN(test_rethink_watch);
    } else if (leak_mode) {
        if (CamdBase->lib_Version < 43) {
            skip("camd.library 43", "older library");
        } else {
            RUN(test_endpoint_leak);
        }
    } else if (v43_mode) {
        if (CamdBase->lib_Version < 43) {
            skip("camd.library 43", "older library");
        } else {
            RUN(test_endpoints);
            RUN(test_endpoint_input);
            RUN(test_endpoint_time);
            RUN(test_endpoint_sysex_limit);
            RUN(test_endpoint_watch);
            RUN(test_published_endpoint);
            RUN(test_ump_endpoint);
            RUN(test_published_hold);
            RUN(test_ump_protocol);
            RUN(test_flags_and_names);
            RUN(test_endpoint_records);
            RUN(test_endpoint_stress);
            RUN(test_endpoint_limits);
        }
    } else if (v42_mode) {
        if (CamdBase->lib_Version < 42) {
            skip("camd.library 42", "older library");
        } else {
            RUN(test_participants);
            RUN(test_cluster_attrs);
            RUN(test_link_comments);
            RUN(test_clock);
            RUN(test_watch);
            /* Last: a deadlock leaves its process holding LockCAMD(). */
            RUN(test_watch_locked);
        }
    } else {
        RUN(test_nodes);
        RUN(test_links);
        RUN(test_messages);
        RUN(test_sysex);
        RUN(test_errors);
        RUN(test_parse);
        RUN(test_msgtypes);
        RUN(test_notify);
        RUN(test_driver);
        RUN(test_loopback);
        RUN(test_rethink_stub);
        /* Last: a deadlock leaves its process holding LockCAMD(). */
        RUN(test_notify_locked);
    }
    printf("camdcompat: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
    if (!abandon_mode) CloseLibrary(CamdBase);
    return failed ? 5 : 0;
}
