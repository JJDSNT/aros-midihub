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
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

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
    result = ObtainEndpointSnapshot(&snapshot, &count);
    check(result == CAMD_REGISTRY_OK && count >= 4, "ObtainEndpointSnapshot counts the driver's ports", NULL);
    if (result != CAMD_REGISTRY_OK) return;

    memset(&port, 0, sizeof(port));
    for (i = 0; i < count; i++) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        info.Version = 1;
        if (GetEndpointInfo(&snapshot, i, &info) != CAMD_REGISTRY_OK) break;
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
    check(result == CAMD_REGISTRY_OK && info.Size == sizeof(info) && ((UBYTE *)&info)[24] == 0xa5,
          "GetEndpointInfo writes no more than the caller's Size", NULL);
    info.Size = sizeof(info);
    check(GetEndpointInfo(&snapshot, count, &info) == CAMD_REGISTRY_RANGE, "GetEndpointInfo past the end is CAMD_REGISTRY_RANGE", NULL);
    ReleaseEndpointSnapshot(&snapshot);
    info.Size = sizeof(info);
    check(GetEndpointInfo(&snapshot, 0, &info) == CAMD_REGISTRY_STALE, "a released snapshot is CAMD_REGISTRY_STALE", NULL);
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
    check(OpenEndpointSession(&request, &session, NULL) == CAMD_REGISTRY_UNSUPPORTED,
          "a UMP session on a MIDI 1.0 port is CAMD_REGISTRY_UNSUPPORTED", NULL);
    request.DataFormat = CAMD_DATA_FORMAT_MIDI1;
    request.Protocol = CAMD_PROTOCOL_MIDI1;
    request.Direction = CAMD_DIRECTION_INPUT;
    check(OpenEndpointSession(&request, &session, NULL) == CAMD_REGISTRY_UNSUPPORTED,
          "an input session is CAMD_REGISTRY_UNSUPPORTED so far", NULL);
    request.Direction = CAMD_DIRECTION_OUTPUT;
    request.EndpointID.word[3] ^= 0x5a5a5a5aUL;
    check(OpenEndpointSession(&request, &session, NULL) != CAMD_REGISTRY_OK && session.generation == 0,
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
    check(result == CAMD_REGISTRY_OK, "OpenEndpointSession opens an output session", NULL);
    if (result != CAMD_REGISTRY_OK) goto out;
    check(got.Direction == CAMD_DIRECTION_OUTPUT && got.DataFormat == CAMD_DATA_FORMAT_MIDI1 &&
          got.QueueCapacity >= 8 && got.MaxSysExBytes >= sizeof(sysex),
          "the session reports its format, queue and SysEx limit", NULL);

    events[0] = midi1(0x91, 0x40, 0x7f);
    events[1] = midi1(0x81, 0x40, 0x00);
    check(PutEndpointMidi(&session, events, 2) == CAMD_REGISTRY_OK, "PutEndpointMidi queues a batch", NULL);
    check(PutEndpointSysEx(&session, sysex, sizeof(sysex)) == CAMD_REGISTRY_OK, "PutEndpointSysEx queues a message", NULL);
    check(DrainEndpointSession(&session) == CAMD_REGISTRY_OK, "DrainEndpointSession returns once they are sent", NULL);
    if (sender) PutMidi(sender, 0x92311100UL);

    events[0].Length = 0;
    check(PutEndpointMidi(&session, events, 1) == CAMD_REGISTRY_INVALID, "a message without bytes is CAMD_REGISTRY_INVALID", NULL);
    memset(big, 0x01, sizeof(big));
    big[0] = 0xf0;
    big[sizeof(big) - 1] = 0xf7;
    check(got.MaxSysExBytes < sizeof(big) && PutEndpointSysEx(&session, big, sizeof(big)) == CAMD_REGISTRY_TOO_LARGE,
          "SysEx past MaxSysExBytes is CAMD_REGISTRY_TOO_LARGE", NULL);

    check(OpenEndpointSession(&request, &second, NULL) == CAMD_REGISTRY_OK, "a second session opens on the same endpoint", NULL);
    check(CloseEndpointSession(&second) == CAMD_REGISTRY_OK, "CloseEndpointSession closes it", NULL);

    /* The session keeps the port open without the legacy link. */
    if (sender) {
        RemoveMidiLink(sender);
        sender = NULL;
    }
    events[0] = midi1(0xb1, 0x07, 0x64);
    check(PutEndpointMidi(&session, events, 1) == CAMD_REGISTRY_OK && DrainEndpointSession(&session) == CAMD_REGISTRY_OK,
          "the session still sends after the legacy link has gone", NULL);

    check(CloseEndpointSession(&session) == CAMD_REGISTRY_OK, "CloseEndpointSession", NULL);
    check(PutEndpointMidi(&session, events, 1) != CAMD_REGISTRY_OK, "a closed session does not send", NULL);
    check(CloseEndpointSession(&session) != CAMD_REGISTRY_OK, "closing a session twice is refused", NULL);
out:
    if (sender) RemoveMidiLink(sender);
    if (node) DeleteMidi(node);
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
    int router_mode = argc == 4 && !strcmp(argv[1], "--router");

    /* Unbuffered, so a crash still leaves the checks before it in the log. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 1 && !rethink_mode && !v42_mode && !v43_mode && !router_mode) {
        puts("Usage: MIDIHubCAMDCompat [--v42 | --v43 | --rethink | --router SOURCE DESTINATION]");
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
        RUN(test_rethink_load);
    } else if (v43_mode) {
        if (CamdBase->lib_Version < 43) {
            skip("camd.library 43", "older library");
        } else {
            RUN(test_endpoints);
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
        RUN(test_rethink_stub);
        /* Last: a deadlock leaves its process holding LockCAMD(). */
        RUN(test_notify_locked);
    }
    printf("camdcompat: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
    CloseLibrary(CamdBase);
    return failed ? 5 : 0;
}
