#include <midihub/routes.h>
#include "router_control.h"

#ifdef __AROS__
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUTE_FILE_MAX 65536
#define ROUTE_SYSEX_MAX 65536

struct Library *CamdBase;

struct route_runtime {
    struct MidiLink *input;
    struct MidiLink *output;
    int was_connected;
};

struct router_runtime {
    struct MidiNode *node;
    struct ClusterNotifyNode notify;
    struct MsgPort *control;
    int control_public;
    struct mh_route_table table;
    struct route_runtime routes[MH_ROUTE_MAX];
    UBYTE *sysex;
    BYTE midi_signal;
    BYTE notify_signal;
    int notifying;
};

static int read_file(const char *path, char **text, size_t *length)
{
    FILE *file;
    long size;
    char *data;

    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        size > ROUTE_FILE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    data = malloc((size_t)size + 1);
    if (!data) {
        fclose(file);
        return -1;
    }
    if (size != 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return -1;
    }
    fclose(file);
    data[size] = '\0';
    *text = data;
    *length = (size_t)size;
    return 0;
}

static int load_routes(struct mh_route_table *table)
{
    char *text;
    size_t length;
    size_t line;

    if (read_file("ENV:MidiHub/Routes", &text, &length) != 0 &&
        read_file("ENVARC:MidiHub/Routes", &text, &length) != 0) {
        memset(table, 0, sizeof(*table));
        return 0;
    }
    if (mh_routes_parse(table, text, length, &line) != 0) {
        if (line != 0)
            fprintf(stderr, "MIDIHubRouter: invalid route configuration at line %lu\n",
                    (unsigned long)line);
        else
            fputs("MIDIHubRouter: enabled routes contain a cycle\n", stderr);
        free(text);
        return -1;
    }
    free(text);
    return 0;
}

static void remove_route(struct route_runtime *route)
{
    if (route->output)
        RemoveMidiLink(route->output);
    if (route->input)
        RemoveMidiLink(route->input);
    memset(route, 0, sizeof(*route));
}

static int add_route(struct router_runtime *rt, size_t index)
{
    struct mh_route_config *config = &rt->table.routes[index];
    struct route_runtime *route = &rt->routes[index];
    struct TagItem input_tags[] = {
        {MLINK_Name, (IPTR)"MIDIHub route input"},
        {MLINK_Location, (IPTR)config->source},
        {MLINK_PortID, (IPTR)index},
        {TAG_DONE, 0}
    };
    struct TagItem output_tags[] = {
        {MLINK_Name, (IPTR)"MIDIHub route output"},
        {MLINK_Location, (IPTR)config->destination},
        {TAG_DONE, 0}
    };

    if (!config->enabled)
        return 0;
    route->input = AddMidiLinkA(rt->node, MLTYPE_Receiver, input_tags);
    if (route->input)
        route->output = AddMidiLinkA(rt->node, MLTYPE_Sender, output_tags);
    if (!route->input || !route->output) {
        remove_route(route);
        return -1;
    }
    route->was_connected = -1;
    return 0;
}

static int route_connected(const struct route_runtime *route)
{
    return route->input && route->output && MidiLinkConnected(route->input) &&
           MidiLinkConnected(route->output);
}

static void update_route_states(struct router_runtime *rt)
{
    size_t i;

    for (i = 0; i < rt->table.count; ++i) {
        struct route_runtime *route = &rt->routes[i];
        int connected = route_connected(route);
        int previous = route->was_connected;
        if (connected != route->was_connected) {
            printf("MIDIHubRouter: %s -> %s: %s\n",
                   rt->table.routes[i].source,
                   rt->table.routes[i].destination,
                   connected ? "active" : "waiting");
            route->was_connected = connected;
        }
        if (!connected && previous == 1 &&
            !rt->table.routes[i].reconnect)
            remove_route(route);
    }
}

static int reload_routes(struct router_runtime *rt)
{
    struct mh_route_table replacement;
    MidiMsg pending;
    size_t i;

    if (load_routes(&replacement) != 0)
        return -1;
    /* Do not deliver messages received under the old PortID mapping through
       a different route after the table is replaced. */
    while (GetMidi(rt->node, &pending))
        if (pending.mm_Status == 0xf0)
            SkipSysEx(rt->node);
    for (i = 0; i < rt->table.count; ++i)
        remove_route(&rt->routes[i]);
    rt->table = replacement;
    for (i = 0; i < rt->table.count; ++i)
        if (add_route(rt, i) != 0)
            return -1;
    update_route_states(rt);
    return 0;
}

static void route_counts(struct router_runtime *rt, ULONG *active, ULONG *waiting)
{
    size_t i;

    *active = *waiting = 0;
    for (i = 0; i < rt->table.count; ++i) {
        if (!rt->table.routes[i].enabled)
            continue;
        if (route_connected(&rt->routes[i]))
            ++*active;
        else if (rt->routes[i].input)
            ++*waiting;
    }
}

static void route_status(struct router_runtime *rt,
                         struct mh_router_message *message)
{
    size_t i;
    message->route_count = (ULONG)rt->table.count;
    memset(message->route_state, MH_ROUTE_STATE_DISABLED,
           sizeof(message->route_state));
    for (i = 0; i < rt->table.count; ++i) {
        if (!rt->table.routes[i].enabled)
            continue;
        message->route_state[i] = route_connected(&rt->routes[i]) ?
                                  MH_ROUTE_STATE_ACTIVE :
                                  MH_ROUTE_STATE_WAITING;
    }
}

static int handle_control(struct router_runtime *rt)
{
    struct mh_router_message *message;
    int stop = 0;

    while ((message = (struct mh_router_message *)GetMsg(rt->control)) != NULL) {
        message->result = 0;
        if (message->command == MH_ROUTER_RELOAD)
            message->result = reload_routes(rt);
        else if (message->command == MH_ROUTER_STOP)
            stop = 1;
        else if (message->command != MH_ROUTER_STATUS)
            message->result = -1;
        message->configured = (ULONG)rt->table.count;
        route_counts(rt, &message->active, &message->waiting);
        route_status(rt, message);
        ReplyMsg(&message->message);
    }
    return stop;
}

static int send_control(ULONG command)
{
    struct MsgPort *reply;
    struct MsgPort *control;
    struct mh_router_message message;
    int result = 20;

    reply = CreateMsgPort();
    if (!reply)
        return result;
    memset(&message, 0, sizeof(message));
    message.message.mn_ReplyPort = reply;
    message.message.mn_Length = sizeof(message);
    message.command = command;
    Forbid();
    control = FindPort((CONST_STRPTR)MIDIHUB_ROUTER_PORT);
    if (control)
        PutMsg(control, &message.message);
    Permit();
    if (control) {
        WaitPort(reply);
        (void)GetMsg(reply);
        printf("MIDIHubRouter: configured=%lu active=%lu waiting=%lu\n",
               (unsigned long)message.configured, (unsigned long)message.active,
               (unsigned long)message.waiting);
        result = message.result == 0 ? 0 : 20;
    } else {
        fputs("MIDIHubRouter: service is not running\n", stderr);
    }
    DeleteMsgPort(reply);
    return result;
}

static void forward_messages(struct router_runtime *rt)
{
    MidiMsg message;

    while (GetMidi(rt->node, &message)) {
        size_t index = message.mm_Port;
        struct MidiLink *output = index < rt->table.count ?
                                  rt->routes[index].output : NULL;
        if (message.mm_Status == 0xf0) {
            ULONG length = QuerySysEx(rt->node);
            if (!output || length < 2 || length > ROUTE_SYSEX_MAX ||
                GetSysEx(rt->node, rt->sysex, length) != length) {
                SkipSysEx(rt->node);
            } else {
                PutSysEx(output, rt->sysex);
            }
        } else if (output) {
            PutMidi(output, message.mm_Msg);
        }
    }
}

static void close_router(struct router_runtime *rt)
{
    size_t i;

    if (rt->notifying)
        EndClusterNotify(&rt->notify);
    if (rt->control) {
        struct mh_router_message *message;
        Forbid();
        if (rt->control_public)
            RemPort(rt->control);
        while ((message = (struct mh_router_message *)GetMsg(rt->control)) != NULL) {
            message->result = -1;
            ReplyMsg(&message->message);
        }
        Permit();
        DeleteMsgPort(rt->control);
    }
    for (i = 0; i < rt->table.count; ++i)
        remove_route(&rt->routes[i]);
    if (rt->node)
        DeleteMidi(rt->node);
    if (rt->notify_signal >= 0)
        FreeSignal(rt->notify_signal);
    if (rt->midi_signal >= 0)
        FreeSignal(rt->midi_signal);
    free(rt->sysex);
    if (CamdBase)
        CloseLibrary(CamdBase);
}

int main(int argc, char **argv)
{
    struct router_runtime rt;
    struct TagItem node_tags[] = {
        {MIDI_Name, (IPTR)"MIDIHub Router"},
        {MIDI_MsgQueue, 1024},
        {MIDI_SysExSize, ROUTE_SYSEX_MAX},
        {MIDI_RecvSignal, 0},
        {TAG_DONE, 0}
    };
    ULONG signals;
    ULONG signal_mask;
    size_t i;
    int result = 20;

    memset(&rt, 0, sizeof(rt));
    rt.midi_signal = rt.notify_signal = -1;
    if (argc == 2) {
        if (strcmp(argv[1], "STATUS") == 0)
            return send_control(MH_ROUTER_STATUS);
        if (strcmp(argv[1], "RELOAD") == 0)
            return send_control(MH_ROUTER_RELOAD);
        if (strcmp(argv[1], "STOP") == 0)
            return send_control(MH_ROUTER_STOP);
        fputs("Usage: MIDIHubRouter [STATUS|RELOAD|STOP]\n", stderr);
        return 20;
    }
    if (argc != 1) {
        fputs("Usage: MIDIHubRouter [STATUS|RELOAD|STOP]\n", stderr);
        return 20;
    }
    if (load_routes(&rt.table) != 0)
        return 20;
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 40);
    if (!CamdBase)
        goto done;
    rt.sysex = malloc(ROUTE_SYSEX_MAX);
    rt.midi_signal = AllocSignal(-1);
    rt.notify_signal = AllocSignal(-1);
    if (!rt.sysex || rt.midi_signal < 0 || rt.notify_signal < 0)
        goto done;
    node_tags[3].ti_Data = (IPTR)rt.midi_signal;
    rt.node = CreateMidiA(node_tags);
    if (!rt.node)
        goto done;
    rt.control = CreateMsgPort();
    if (!rt.control)
        goto done;
    rt.control->mp_Node.ln_Name = (char *)MIDIHUB_ROUTER_PORT;
    Forbid();
    if (FindPort((CONST_STRPTR)MIDIHUB_ROUTER_PORT)) {
        Permit();
        fputs("MIDIHubRouter: service is already running\n", stderr);
        goto done;
    }
    AddPort(rt.control);
    rt.control_public = 1;
    Permit();
    for (i = 0; i < rt.table.count; ++i)
        if (add_route(&rt, i) != 0)
            goto done;
    rt.notify.cnn_Task = FindTask(NULL);
    rt.notify.cnn_SigBit = rt.notify_signal;
    StartClusterNotify(&rt.notify);
    rt.notifying = 1;
    puts("MIDIHubRouter: running");
    update_route_states(&rt);
    signal_mask = SIGBREAKF_CTRL_C | (1UL << rt.midi_signal) |
                  (1UL << rt.notify_signal) | (1UL << rt.control->mp_SigBit);
    for (;;) {
        /* Cluster notification covers creation/removal. Participant changes
           inside an existing cluster are not notified by CAMD, so poll the
           link state as well. Delay() keeps this task dormant between polls. */
        Delay(25);
        signals = SetSignal(0, signal_mask) & signal_mask;
        if (signals & SIGBREAKF_CTRL_C) {
            result = 0;
            break;
        }
        if ((signals & (1UL << rt.control->mp_SigBit)) && handle_control(&rt)) {
            result = 0;
            break;
        }
        if (signals & (1UL << rt.midi_signal))
            forward_messages(&rt);
        update_route_states(&rt);
    }

done:
    close_router(&rt);
    return result;
}

#else
#include <stdio.h>
int main(void)
{
    fputs("MIDIHubRouter is an AROS CAMD service\n", stderr);
    return 1;
}
#endif
