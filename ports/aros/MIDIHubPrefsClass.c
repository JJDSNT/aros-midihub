#include <midihub/ble_midi.h>
#include <midihub/config.h>
#include <midihub/routes.h>
#include "MIDIHubPrefsClass.h"
#include "router_control.h"
#include "btmidi/btmidi_cfg.h"

#include <dos/dostags.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <midi/camd.h>
#include <libraries/bluetooth.h>
#include <libraries/btclass.h>

#include <proto/bluetooth.h>
#include <proto/btclass.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wint-conversion"
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define CONFIG_MAX 65536

/* Optional: without bluetooth.library the overview simply lacks BLE detail */
struct Library *BluetoothBase;

static CONST_STRPTR nav_entries[] = {
    "Overview", "Routing", "Network MIDI", "Synthesizer", "Profiles",
    "Diagnostics", NULL
};
static CONST_STRPTR backend_entries[] = { "TinySoundFont", "FluidSynth", NULL };

static void set_status(struct MHPrefsData *data, CONST_STRPTR text)
{
    set(data->status_text, MUIA_Text_Contents, (IPTR)text);
}

static int contains_ci(CONST_STRPTR text, CONST_STRPTR needle)
{
    size_t i, j, length = strlen(needle);
    for (i = 0; text[i]; ++i) {
        for (j = 0; j < length; ++j) {
            char a = text[i + j], b = needle[j];
            if (!a) return 0;
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (j == length) return 1;
    }
    return 0;
}

static CONST_STRPTR endpoint_transport(CONST_STRPTR name)
{
    if (contains_ci(name, "synth")) return "Software";
    /* btmidi.class's own ports are known from bluetooth.library; these
       catch its defaults without it, and MIDIHubBLE's diagnostic ports */
    if (!strncmp(name, BTMIDI_DEFAULT_NODE " ", sizeof(BTMIDI_DEFAULT_NODE)) ||
        !strncmp(name, "MIDIHub BLE ", 12) ||
        contains_ci(name, "bluetooth")) return "BLE MIDI";
    if (contains_ci(name, "usb")) return "USB";
    if (contains_ci(name, "rtp") || contains_ci(name, "apple") ||
        contains_ci(name, "midihub")) return "Network";
    return "CAMD";
}

AROS_UFH3(IPTR, EndpointDisplay,
          AROS_UFHA(struct Hook *, hook, A0),
          AROS_UFHA(STRPTR *, columns, A2),
          AROS_UFHA(struct MHPrefsEndpoint *, entry, A1))
{
    AROS_USERFUNC_INIT
    (void)hook;
    if (entry) {
        *columns++ = entry->name;
        *columns++ = entry->transport;
        *columns++ = entry->direction;
        *columns = entry->state;
    } else {
        *columns++ = (STRPTR)"Endpoint";
        *columns++ = (STRPTR)"Transport";
        *columns++ = (STRPTR)"Direction";
        *columns = (STRPTR)"Status";
    }
    return 0;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(IPTR, RouteDisplay,
          AROS_UFHA(struct Hook *, hook, A0),
          AROS_UFHA(STRPTR *, columns, A2),
          AROS_UFHA(struct mh_route_config *, route, A1))
{
    AROS_USERFUNC_INIT
    struct MHPrefsData *data = (struct MHPrefsData *)hook->h_Data;
    if (route) {
        ULONG index = (ULONG)(route - data->routes.routes);
        *columns++ = route->source;
        *columns++ = route->destination;
        /* Say what to do when the route is not forwarding yet. */
        *columns++ = !route->enabled ? (STRPTR)"Disabled" :
                     data->routes_changed || index >= data->routes.count ?
                         (STRPTR)"Not saved: press Use or Save" :
                     !data->router_running ? (STRPTR)"Router stopped: press Start" :
                     data->route_states[index] == MH_ROUTE_STATE_ACTIVE ? (STRPTR)"Active" :
                     data->route_states[index] == MH_ROUTE_STATE_WAITING ?
                         (STRPTR)"Waiting for an endpoint" :
                     (STRPTR)"Not saved: press Use or Save";
        *columns = route->reconnect ? (STRPTR)"Automatic" : (STRPTR)"Once";
    } else {
        *columns++ = (STRPTR)"From";
        *columns++ = (STRPTR)"To";
        *columns++ = (STRPTR)"Status";
        *columns = (STRPTR)"Reconnect";
    }
    return 0;
    AROS_USERFUNC_EXIT
}

static int read_file(CONST_STRPTR path, char **text, size_t *length)
{
    FILE *file = fopen(path, "rb");
    char *buffer;
    long size;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        size > CONFIG_MAX || fseek(file, 0, SEEK_SET)) {
        fclose(file); return -1;
    }
    buffer = malloc((size_t)size + 1);
    if (!buffer) { fclose(file); return -1; }
    if (size && fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer); fclose(file); return -1;
    }
    fclose(file);
    buffer[size] = 0;
    *text = buffer; *length = (size_t)size;
    return 0;
}

static int load_routes_path(struct MHPrefsData *data, CONST_STRPTR path)
{
    struct mh_route_table next;
    char *text;
    size_t length, error_line;
    if (read_file(path, &text, &length)) return -1;
    if (mh_routes_parse(&next, text, length, &error_line)) {
        free(text); return -1;
    }
    free(text);
    data->routes = next;
    return 0;
}

static int load_routes(struct MHPrefsData *data)
{
    if (!load_routes_path(data, "ENV:MidiHub/Routes")) return 0;
    if (!load_routes_path(data, "ENVARC:MidiHub/Routes")) return 0;
    memset(&data->routes, 0, sizeof(data->routes));
    return 0;
}

static int ensure_directory(CONST_STRPTR path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) lock = CreateDir((STRPTR)path);
    if (!lock) return -1;
    UnLock(lock); return 0;
}

static int write_routes(struct MHPrefsData *data, CONST_STRPTR path)
{
    FILE *file = fopen(path, "wb");
    ULONG i;
    if (!file) return -1;
    fputs("# MIDIHub persistent routes\n", file);
    for (i = 0; i < data->routes.count; ++i)
        if (fprintf(file, "route\t%d\t%d\t%s\t%s\n",
                    data->routes.routes[i].enabled,
                    data->routes.routes[i].reconnect,
                    data->routes.routes[i].source,
                    data->routes.routes[i].destination) < 0) {
            fclose(file); return -1;
        }
    return fclose(file) ? -1 : 0;
}

static int write_network(struct MHPrefsData *data, CONST_STRPTR path)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    fprintf(file, "local_port=%u\nsession_name=%s\n",
            (unsigned)data->network.local_port, data->network.session_name);
    if (data->network.peer_ip[0])
        fprintf(file, "peer_ip=%s\npeer_port=%u\n", data->network.peer_ip,
                (unsigned)data->network.peer_port);
    return fclose(file) ? -1 : 0;
}

static int write_value(CONST_STRPTR path, CONST_STRPTR value)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    if (fprintf(file, "%s\n", value) < 0) { fclose(file); return -1; }
    return fclose(file) ? -1 : 0;
}

static int read_value(CONST_STRPTR path, char *value, size_t capacity)
{
    FILE *file = fopen(path, "r");
    size_t length;
    if (!file || !fgets(value, (int)capacity, file)) {
        if (file) fclose(file);
        return -1;
    }
    fclose(file);
    length = strlen(value);
    while (length && (value[length - 1] == '\n' || value[length - 1] == '\r'))
        value[--length] = 0;
    return length ? 0 : -1;
}

static int router_command(ULONG command, struct mh_router_message *result)
{
    struct MsgPort *reply = CreateMsgPort();
    struct MsgPort *control;
    struct mh_router_message message;
    if (!reply) return -1;
    memset(&message, 0, sizeof(message));
    message.message.mn_ReplyPort = reply;
    message.message.mn_Length = sizeof(message);
    message.command = command;
    Forbid();
    control = FindPort((CONST_STRPTR)MIDIHUB_ROUTER_PORT);
    if (control) PutMsg(control, &message.message);
    Permit();
    if (!control) { DeleteMsgPort(reply); return -1; }
    WaitPort(reply); (void)GetMsg(reply); DeleteMsgPort(reply);
    if (result) *result = message;
    return message.result ? -1 : 0;
}

/* Services that keep working once this window is closed: the router owns
   the routes, the synth plays MIDIHub Synth. Each has a public port that
   says it runs. Package-Startup starts them at boot. */
#define MIDIHUB_SYNTH_PORT "MIDIHub.Synth"

static int service_running(CONST_STRPTR port_name)
{
    struct MsgPort *port;
    Forbid();
    port = FindPort(port_name);
    Permit();
    return port != NULL;
}

static void wait_service(CONST_STRPTR port_name, int running, int seconds)
{
    int tries;
    for (tries = 0; tries < seconds * 10 && service_running(port_name) != running; ++tries)
        Delay(5);
}

static void start_service(CONST_STRPTR command, CONST_STRPTR port_name)
{
    BPTR nil = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);

    if (!nil)
        return;
    SystemTags((STRPTR)command, SYS_Input, (IPTR)nil, SYS_Output, (IPTR)NULL, TAG_DONE);
    Close(nil);
    /* So that the state shown next is the service's. */
    wait_service(port_name, 1, 5);
}

static void start_router(void)
{
    start_service((CONST_STRPTR)"Run >NIL: <NIL: MIDIHUB:C/MIDIHubRouter",
                  (CONST_STRPTR)MIDIHUB_ROUTER_PORT);
}

static void stop_router(void)
{
    if (router_command(MH_ROUTER_STOP, NULL) == 0)
        wait_service((CONST_STRPTR)MIDIHUB_ROUTER_PORT, 0, 5);
}

static int synth_installed(void)
{
    BPTR lock = Lock((CONST_STRPTR)"MIDIHUB:C/MIDIHubSynth", ACCESS_READ);
    if (!lock)
        return 0;
    UnLock(lock);
    return 1;
}

/* The synth writes why it did not start (no SoundFont, AHI, ...) to its
   output, kept in T: so it can be shown. Loading a large bank takes a
   while, hence the longer wait. */
#define SYNTH_LOG "T:MIDIHubSynth.log"

static void start_synth(struct MHPrefsData *data)
{
    BPTR nil = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    char reason[160];

    if (!nil)
        return;
    (void)DeleteFile((CONST_STRPTR)SYNTH_LOG);
    SystemTags((STRPTR)"Run >" SYNTH_LOG " <NIL: MIDIHUB:C/MIDIHubSynth",
               SYS_Input, (IPTR)nil, SYS_Output, (IPTR)NULL, TAG_DONE);
    Close(nil);
    wait_service((CONST_STRPTR)MIDIHUB_SYNTH_PORT, 1, 20);
    if (service_running((CONST_STRPTR)MIDIHUB_SYNTH_PORT))
        return;
    if (read_value(SYNTH_LOG, reason, sizeof(reason)) == 0) {
        char status[200];
        snprintf(status, sizeof(status), "The synth did not start: %s", reason);
        set_status(data, status);
    } else {
        set_status(data, "The synth did not start; run MIDIHUB:C/MIDIHubSynth in a Shell to see why.");
    }
}

static void stop_synth(void)
{
    struct MsgPort *port;
    Forbid();
    port = FindPort((CONST_STRPTR)MIDIHUB_SYNTH_PORT);
    if (port && port->mp_SigTask)
        Signal((struct Task *)port->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
    if (port)
        wait_service((CONST_STRPTR)MIDIHUB_SYNTH_PORT, 0, 5);
}

static void refresh_synth_state(struct MHPrefsData *data)
{
    int installed = synth_installed();
    int running = service_running((CONST_STRPTR)MIDIHUB_SYNTH_PORT);

    set(data->synth_state, MUIA_Text_Contents,
        running ? "Running: MIDIHub Synth plays what is routed to it" :
        installed ? "Stopped" : "Not installed in this package");
    set(data->synth_start, MUIA_Disabled, running || !installed);
    set(data->synth_stop, MUIA_Disabled, !running);
    set(data->synth_boot, MUIA_Disabled, !installed);
}

static void fill_routes(struct MHPrefsData *data)
{
    ULONG i;
    set(data->route_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->route_list, MUIM_List_Clear);
    for (i = 0; i < data->routes.count; ++i)
        DoMethod(data->route_list, MUIM_List_InsertSingle,
                 &data->routes.routes[i], MUIV_List_Insert_Bottom);
    set(data->route_list, MUIA_List_Quiet, FALSE);
}

/* ---- BLE MIDI: what btmidi.class serves, as bluetooth.library tells ---- */

static void add_ble(struct MHPrefsData *data, CONST_STRPTR name, CONST_STRPTR state)
{
    struct MHPrefsBle *ble;

    if (data->ble_count == MH_PREFS_BLE_MAX || !name[0])
        return;
    ble = &data->ble[data->ble_count++];
    snprintf(ble->name, sizeof(ble->name), "%s", name);
    snprintf(ble->state, sizeof(ble->state), "%s", state);
}

/* The library base must be read-locked. */
static APTR find_btmidi_class(void)
{
    struct List *classes = NULL;
    struct Node *node;
    STRPTR name;

    btGetAttrs(BGA_STACK, NULL, BSA_ClassList, &classes, TAG_END);
    if (!classes)
        return NULL;
    for (node = classes->lh_Head; node->ln_Succ; node = node->ln_Succ) {
        name = NULL;
        btGetAttrs(BGA_BTCLASS, node, BCA_ClassName, &name, TAG_END);
        if (name && !strcmp((char *)name, "btmidi.class"))
            return node;
    }
    return NULL;
}

static void collect_ble(struct MHPrefsData *data)
{
    struct BTMidiCfg cfg, *chunk;
    struct List *records = NULL;
    struct Node *node;
    struct BtDevice *device;
    char node_name[BTMIDI_NAME_SIZE], in[BTMIDI_NAME_SIZE], out[BTMIDI_NAME_SIZE];
    CONST_STRPTR service = "Stopped";
    APTR cls, pic;

    data->ble_count = 0;
    if (!BluetoothBase)
        return;
    btLockReadBase();
    if (!(cls = find_btmidi_class())) {
        btUnlockBase();
        return;
    }
    /* AROS as the peripheral: the service btmidi.class registered */
    btGetAttrs(BGA_STACK, NULL, BSA_ServiceRecordList, &records, TAG_END);
    for (node = records ? records->lh_Head : NULL; node && node->ln_Succ;
         node = node->ln_Succ) {
        STRPTR owner = NULL;
        IPTR enabled = FALSE;
        btGetAttrs(BGA_SERVICERECORD, node, BSRA_Owner, &owner,
                   BSRA_Enabled, &enabled, TAG_END);
        if (owner && !strcmp((char *)owner, "btmidi.class"))
            service = enabled ? "Offered" : "Not offered";
    }
    /* AROS as the central: the devices btmidi.class is bound to */
    for (device = btFindDevice(NULL, TAG_END); device;
         device = btFindDevice(device, TAG_END)) {
        struct List *services = NULL;
        STRPTR device_name = NULL;
        IPTR connected = FALSE;
        BOOL bound = FALSE;

        btLockReadDevice(device);
        btGetAttrs(BGA_DEVICE, device, BDA_ServiceList, &services,
                   BDA_Name, &device_name, BDA_IsConnected, &connected, TAG_END);
        for (node = services ? services->lh_Head : NULL; node && node->ln_Succ;
             node = node->ln_Succ) {
            APTR binding_class = NULL;
            btGetAttrs(BGA_SERVICE, node, BSVA_BindingClass, &binding_class, TAG_END);
            if (binding_class == cls)
                bound = TRUE;
        }
        if (bound)
            mh_ble_midi_port_names((const char *)device_name, node_name, in, out,
                                   sizeof(node_name));
        btUnlockDevice(device);
        if (bound) {
            add_ble(data, in, connected ? "Connected" : "Not connected");
            add_ble(data, out, connected ? "Connected" : "Not connected");
        }
    }
    btUnlockBase();

    /* the peripheral role's names, from the class configuration */
    Forbid();
    strcpy(cfg.mc_InName, BTMIDI_DEFAULT_IN);
    strcpy(cfg.mc_OutName, BTMIDI_DEFAULT_OUT);
    if ((pic = btGetClsCfg((STRPTR)"btmidi.class")) &&
        (chunk = btGetCfgChunk(pic, MAKE_ID('B','M','I','D')))) {
        ULONG len = AROS_LONG2BE(chunk->mc_Length);
        if (len > sizeof(cfg) - 8)
            len = sizeof(cfg) - 8;
        CopyMem(((UBYTE *)chunk) + 8, ((UBYTE *)&cfg) + 8, len);
        btFreeVec(chunk);
        cfg.mc_InName[BTMIDI_NAME_SIZE - 1] = 0;
        cfg.mc_OutName[BTMIDI_NAME_SIZE - 1] = 0;
    }
    Permit();
    add_ble(data, cfg.mc_InName, service);
    add_ble(data, cfg.mc_OutName, service);
}

static const struct MHPrefsBle *find_ble(struct MHPrefsData *data, CONST_STRPTR name)
{
    ULONG i;
    for (i = 0; i < data->ble_count; ++i)
        if (!strcmp(data->ble[i].name, name))
            return &data->ble[i];
    return NULL;
}

static void open_ble_midi(struct MHPrefsData *data)
{
    struct Library *BtClsBase = NULL;
    APTR cls;
    BOOL opened = FALSE;

    if (!BluetoothBase) {
        set_status(data, "bluetooth.library is not available.");
        return;
    }
    btLockReadBase();
    if ((cls = find_btmidi_class())) {
        btGetAttrs(BGA_BTCLASS, cls, BCA_ClassBase, &BtClsBase, TAG_END);
        if (BtClsBase)
            opened = btcDoMethod(BCM_OpenCfgWindow) ? TRUE : FALSE;
    }
    btUnlockBase();
    set_status(data, opened ? "Opened the BLE MIDI settings." :
               cls ? "btmidi.class could not open its settings window." :
               "btmidi.class is not loaded; add it in Bluetooth Preferences.");
}

/* ---- Diagnostics: endpoints, a MIDI monitor, a test sender, the BLE state
   and the Bluetooth log on one page, so one screenshot shows it all ---- */

#define DIAG_MAX_LINES 200

static void diag_line(struct MHPrefsData *data, CONST_STRPTR text)
{
    if (data->diag_lines >= DIAG_MAX_LINES) {
        DoMethod(data->diag_monitor, MUIM_List_Remove, MUIV_List_Remove_First);
        data->diag_lines--;
    }
    DoMethod(data->diag_monitor, MUIM_List_InsertSingle, text, MUIV_List_Insert_Bottom);
    data->diag_lines++;
    set(data->diag_monitor, MUIA_List_Active, MUIV_List_Active_Bottom);
}

static void diag_info(struct MHPrefsData *data)
{
    /* Room for an endpoint name of MH_ROUTE_NAME_MAX bytes */
    char text[MH_ROUTE_NAME_MAX + 64];
    if (data->diag_in)
        snprintf(text, sizeof(text), "Monitoring \"%s\": %lu messages",
                 data->diag_in_name, (unsigned long)data->diag_count);
    else
        snprintf(text, sizeof(text), "Not monitoring. Select an endpoint, then Monitor.");
    set(data->diag_monitor_info, MUIA_Text_Contents, text);
}

static void describe_message(char *buf, size_t size, UBYTE status, UBYTE d1, UBYTE d2)
{
    static const char *names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    unsigned ch = (status & 0x0f) + 1;

    switch (status & 0xf0) {
    case 0x80:
    case 0x90:
        snprintf(buf, size, "%02x %02x %02x  Note %s  ch %u  %s%d  vel %u",
                 status, d1, d2,
                 ((status & 0xf0) == 0x90 && d2) ? "On " : "Off",
                 ch, names[d1 % 12], (int)(d1 / 12) - 1, d2);
        return;
    case 0xa0:
        snprintf(buf, size, "%02x %02x %02x  Poly pressure  ch %u", status, d1, d2, ch);
        return;
    case 0xb0:
        snprintf(buf, size, "%02x %02x %02x  Control %u = %u  ch %u", status, d1, d2, d1, d2, ch);
        return;
    case 0xc0:
        snprintf(buf, size, "%02x %02x     Program %u  ch %u", status, d1, d1 + 1, ch);
        return;
    case 0xd0:
        snprintf(buf, size, "%02x %02x     Channel pressure %u  ch %u", status, d1, d1, ch);
        return;
    case 0xe0:
        snprintf(buf, size, "%02x %02x %02x  Pitch bend %d  ch %u", status, d1, d2,
                 (int)((d2 << 7) | d1) - 8192, ch);
        return;
    }
    snprintf(buf, size, "%02x %02x %02x  System %s", status, d1, d2,
             status == 0xf8 ? "clock" : status == 0xfa ? "start" :
             status == 0xfb ? "continue" : status == 0xfc ? "stop" :
             status == 0xfe ? "active sensing" : status == 0xf2 ? "song position" :
             status == 0xf3 ? "song select" : status == 0xf1 ? "MTC quarter frame" :
             "message");
}

static void diag_poll(struct MHPrefsData *data)
{
    MidiMsg message;
    char text[200];

    if (!data->diag_node)
        return;
    while (GetMidi(data->diag_node, &message)) {
        data->diag_count++;
        if (message.mm_Status == 0xf0) {
            ULONG length = QuerySysEx(data->diag_node), i;
            size_t n;
            if (length > sizeof(data->diag_sysex) ||
                GetSysEx(data->diag_node, data->diag_sysex, length) != length) {
                SkipSysEx(data->diag_node);
                snprintf(text, sizeof(text), "SysEx of %lu bytes, too long to show",
                         (unsigned long)length);
            } else {
                n = (size_t)snprintf(text, sizeof(text), "SysEx %lu bytes:", (unsigned long)length);
                for (i = 0; i < length && i < 16 && n < sizeof(text) - 4; i++)
                    n += (size_t)snprintf(text + n, sizeof(text) - n, " %02x",
                                          (unsigned)data->diag_sysex[i]);
                if (length > 16 && n < sizeof(text) - 5)
                    snprintf(text + n, sizeof(text) - n, " ...");
            }
        } else {
            describe_message(text, sizeof(text), message.mm_Status,
                             message.mm_Data1, message.mm_Data2);
        }
        diag_line(data, text);
    }
    diag_info(data);
}

static struct MHPrefsEndpoint *diag_selected(struct MHPrefsData *data)
{
    struct MHPrefsEndpoint *entry = NULL;
    DoMethod(data->diag_endpoint_list, MUIM_List_GetEntry,
             MUIV_List_GetEntry_Active, &entry);
    if (!entry)
        set_status(data, "Select an endpoint in the Diagnostics list first.");
    return entry;
}

static void diag_stop(struct MHPrefsData *data)
{
    if (data->diag_in) {
        RemoveMidiLink(data->diag_in);
        data->diag_in = NULL;
    }
    diag_info(data);
}

static void diag_monitor(struct MHPrefsData *data, Object *obj)
{
    struct MHPrefsEndpoint *entry = diag_selected(data);
    char line[MH_ROUTE_NAME_MAX + 96];

    if (!entry)
        return;
    if (!data->diag_node) {
        if (data->diag_signal < 0 && (data->diag_signal = AllocSignal(-1)) < 0) {
            set_status(data, "No signal left for the monitor.");
            return;
        }
        {
            struct TagItem tags[] = {
                { MIDI_Name, (IPTR)"MIDIHub Prefs Monitor" },
                { MIDI_MsgQueue, 512 },
                { MIDI_SysExSize, sizeof(data->diag_sysex) },
                { MIDI_RecvSignal, (IPTR)data->diag_signal },
                { TAG_DONE, 0 }
            };
            data->diag_node = CreateMidiA(tags);
        }
        if (!data->diag_node) {
            set_status(data, "CAMD refused the monitor node.");
            return;
        }
        data->diag_input.ihn_Object = obj;
        data->diag_input.ihn_Method = MUIM_MHP_DiagPoll;
        data->diag_input.ihn_Signals = 1UL << data->diag_signal;
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, &data->diag_input);
        data->diag_input_added = TRUE;
    }
    diag_stop(data);
    snprintf(data->diag_in_name, sizeof(data->diag_in_name), "%s", entry->name);
    {
        struct TagItem tags[] = {
            { MLINK_Name, (IPTR)"MIDIHub Prefs Monitor" },
            { MLINK_Location, (IPTR)data->diag_in_name },
            { TAG_DONE, 0 }
        };
        data->diag_in = AddMidiLinkA(data->diag_node, MLTYPE_Receiver, tags);
    }
    data->diag_count = 0;
    snprintf(line, sizeof(line), "--- monitoring %s ---", data->diag_in_name);
    diag_line(data, line);
    diag_info(data);
}

/* A C major scale and a SysEx Identity Request, as MIDIHubCAMDProbe --send */
static void diag_send(struct MHPrefsData *data)
{
    static UBYTE identity_request[] = { 0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7 };
    static const UBYTE scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    static char name[] = "MIDIHub Prefs Test";
    struct MHPrefsEndpoint *entry = diag_selected(data);
    struct MidiNode *node;
    struct MidiLink *link = NULL;
    char line[MH_ROUTE_NAME_MAX + 96];
    ULONG i;

    if (!entry)
        return;
    {
        struct TagItem tags[] = { { MIDI_Name, (IPTR)name }, { TAG_DONE, 0 } };
        node = CreateMidiA(tags);
    }
    if (node) {
        struct TagItem tags[] = {
            { MLINK_Name, (IPTR)name },
            { MLINK_Location, (IPTR)entry->name },
            { TAG_DONE, 0 }
        };
        link = AddMidiLinkA(node, MLTYPE_Sender, tags);
    }
    if (!link) {
        if (node) DeleteMidi(node);
        set_status(data, "Could not link to that endpoint.");
        return;
    }
    snprintf(line, sizeof(line), "--- sending a C major scale and a SysEx to %s%s ---",
             entry->name, MidiLinkConnected(link) ? "" : " (nothing receives there)");
    diag_line(data, line);
    Delay(5);
    for (i = 0; i < sizeof(scale); i++) {
        PutMidi(link, 0x90000000UL | ((ULONG)scale[i] << 16) | (100UL << 8));
        Delay(15);
        PutMidi(link, 0x80000000UL | ((ULONG)scale[i] << 16));
        Delay(3);
    }
    PutSysEx(link, identity_request);
    Delay(10);
    RemoveMidiLink(link);
    DeleteMidi(node);
    diag_line(data, "--- sent ---");
}

/* The BLE MIDI state and the latest Bluetooth log lines */
static void diag_refresh_bt(struct MHPrefsData *data)
{
    char text[1024];
    size_t n = 0;
    ULONG i;

    if (!BluetoothBase) {
        set(data->diag_ble_text, MUIA_Text_Contents,
            (IPTR)"bluetooth.library is not available.");
        return;
    }
    {
        IPTR advertising = FALSE, interval = 0;
        struct BtDevice *device;
        APTR cls;
        ULONG connected = 0;

        btGetAttrs(BGA_STACK, NULL, BSA_LEAdvertising, &advertising,
                   BSA_LEConnInterval, &interval, TAG_END);
        btLockReadBase();
        cls = find_btmidi_class();
        btUnlockBase();
        n += (size_t)snprintf(text + n, sizeof(text) - n,
                              "btmidi.class: %s    Advertising: %s\n"
                              "Asks centrals for: %s\n",
                              cls ? "loaded" : "NOT loaded",
                              advertising ? "on" : "OFF",
                              interval ? "15 ms interval" : "nothing (central decides)");
        for (i = 0; i < data->ble_count && n < sizeof(text) - 80; i++)
            n += (size_t)snprintf(text + n, sizeof(text) - n, "  %s: %s\n",
                                  data->ble[i].name, data->ble[i].state);
        btLockReadBase();
        for (device = btFindDevice(NULL, TAG_END); device;
             device = btFindDevice(device, TAG_END)) {
            IPTR is_connected = FALSE;
            STRPTR address = NULL, name = NULL;
            btGetAttrs(BGA_DEVICE, device, BDA_IsConnected, &is_connected,
                       BDA_AddressString, &address, BDA_Name, &name, TAG_END);
            if (is_connected && n < sizeof(text) - 80) {
                n += (size_t)snprintf(text + n, sizeof(text) - n, "Connected: %s %s\n",
                                      address ? (char *)address : "?",
                                      name ? (char *)name : "(no name)");
                connected++;
            }
        }
        btUnlockBase();
        if (!connected && n < sizeof(text) - 40)
            snprintf(text + n, sizeof(text) - n, "No device connected.");
        set(data->diag_ble_text, MUIA_Text_Contents, (IPTR)text);
        /* nnset: showing the stack's value must not set it again, or every
           refresh would make the stack ask connected centrals once more */
        nnset(data->diag_interval, MUIA_Selected, interval ? TRUE : FALSE);
    }
    /* the last lines of the Bluetooth log */
    {
        struct List *log = NULL;
        struct Node *message;
        char line[300];
        ULONG total = 0, skip;

        set(data->diag_btlog, MUIA_List_Quiet, TRUE);
        DoMethod(data->diag_btlog, MUIM_List_Clear);
        btLockReadBase();
        btGetAttrs(BGA_STACK, NULL, BSA_ErrorMsgList, &log, TAG_END);
        if (log) {
            for (message = log->lh_Head; message->ln_Succ; message = message->ln_Succ)
                total++;
            skip = total > 14 ? total - 14 : 0;
            for (message = log->lh_Head; message->ln_Succ; message = message->ln_Succ) {
                IPTR level = 0;
                STRPTR text_msg = NULL;
                if (skip) { skip--; continue; }
                btGetAttrs(BGA_ERRORMSG, message, BEMA_Level, &level,
                           BEMA_Msg, &text_msg, TAG_END);
                snprintf(line, sizeof(line), "%2ld %s", (long)level,
                         text_msg ? (char *)text_msg : "");
                DoMethod(data->diag_btlog, MUIM_List_InsertSingle, line,
                         MUIV_List_Insert_Bottom);
            }
        }
        btUnlockBase();
        set(data->diag_btlog, MUIA_List_Quiet, FALSE);
        set(data->diag_btlog, MUIA_List_Active, MUIV_List_Active_Bottom);
    }
}

static void diag_interval(struct MHPrefsData *data)
{
    IPTR selected = FALSE;
    if (!BluetoothBase)
        return;
    get(data->diag_interval, MUIA_Selected, &selected);
    btSetAttrs(BGA_STACK, NULL, BSA_LEConnInterval, selected ? 12 : 0, TAG_END);
    diag_line(data, selected ? "--- will ask the next central for a 15 ms interval ---" :
                               "--- will not ask centrals for a connection interval ---");
    diag_refresh_bt(data);
}

/* The endpoint lists are filled again on every CAMD change; with a
   camd.library 42 cluster watch that is every link joining or leaving,
   including Send test's own. Keep each list's selection across it. */
static void remember_active(Object *list, char *name, size_t size)
{
    struct MHPrefsEndpoint *entry = NULL;
    DoMethod(list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &entry);
    snprintf(name, size, "%s", entry ? entry->name : "");
}

static void restore_active(Object *list, const char *name)
{
    struct MHPrefsEndpoint *entry;
    LONG i;
    if (!name[0])
        return;
    for (i = 0; ; ++i) {
        entry = NULL;
        DoMethod(list, MUIM_List_GetEntry, i, &entry);
        if (!entry)
            return;
        if (!strcmp(entry->name, name)) {
            /* nnset: picking it again must not refill the route fields */
            nnset(list, MUIA_List_Active, i);
            return;
        }
    }
}

static void refresh(struct MHPrefsData *data)
{
    char selected[4][MH_ROUTE_NAME_MAX + 1];
    struct MidiCluster *cluster;
    struct mh_router_message status;
    APTR lock;
    ULONG i, active = 0, waiting = 0;
    char summary[160];

    collect_ble(data);
    remember_active(data->endpoint_list, selected[0], sizeof(selected[0]));
    remember_active(data->diag_endpoint_list, selected[1], sizeof(selected[1]));
    remember_active(data->route_from_list, selected[2], sizeof(selected[2]));
    remember_active(data->route_to_list, selected[3], sizeof(selected[3]));
    data->endpoint_count = 0;
    set(data->endpoint_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->endpoint_list, MUIM_List_Clear);
    set(data->diag_endpoint_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->diag_endpoint_list, MUIM_List_Clear);
    set(data->route_from_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->route_from_list, MUIM_List_Clear);
    set(data->route_to_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->route_to_list, MUIM_List_Clear);
    lock = LockCAMD(CD_Linkages);
    for (cluster = NextCluster(NULL); cluster &&
         data->endpoint_count < MH_PREFS_ENDPOINT_MAX;
         cluster = NextCluster(cluster)) {
        struct MHPrefsEndpoint *entry = &data->endpoints[data->endpoint_count++];
        const struct MHPrefsBle *ble;
        int senders = !IsListEmpty(&cluster->mcl_Senders);
        int receivers = !IsListEmpty(&cluster->mcl_Receivers);
        snprintf(entry->name, sizeof(entry->name), "%s",
                 cluster->mcl_Node.ln_Name ? cluster->mcl_Node.ln_Name : "");
        ble = find_ble(data, entry->name);
        snprintf(entry->transport, sizeof(entry->transport), "%s",
                 ble ? "BLE MIDI" : (const char *)endpoint_transport(entry->name));
        snprintf(entry->direction, sizeof(entry->direction), "%s",
                 senders && receivers ? "In / Out" : senders ? "Source" :
                 receivers ? "Destination" : "Idle");
        snprintf(entry->state, sizeof(entry->state), "%s",
                 ble ? ble->state : senders || receivers ? "Online" : "Idle");
        if (CamdBase->lib_Version >= 42) {
            /* What CAMD dropped for this endpoint since it was created */
            IPTR overflows = 0, sysex = 0, errors = 0;
            struct TagItem counters[] = {
                {MCLA_Overflows, (IPTR)&overflows},
                {MCLA_SysExDropped, (IPTR)&sysex},
                {MCLA_RecvErrors, (IPTR)&errors},
                {TAG_DONE, 0}
            };
            ULONG lost;
            GetClusterAttrsA(cluster, counters);
            lost = (ULONG)(overflows + sysex + errors);
            if (lost) {
                size_t used = strlen(entry->state);
                snprintf(entry->state + used, sizeof(entry->state) - used,
                         ", %lu lost", (unsigned long)lost);
            }
        }
        DoMethod(data->endpoint_list, MUIM_List_InsertSingle, entry,
                 MUIV_List_Insert_Bottom);
        DoMethod(data->diag_endpoint_list, MUIM_List_InsertSingle, entry,
                 MUIV_List_Insert_Bottom);
        /* MIDI comes in from a cluster something sends to, and goes to one
           something receives from; an idle one can be either. */
        if (senders || !receivers)
            DoMethod(data->route_from_list, MUIM_List_InsertSingle, entry,
                     MUIV_List_Insert_Bottom);
        if (receivers || !senders)
            DoMethod(data->route_to_list, MUIM_List_InsertSingle, entry,
                     MUIV_List_Insert_Bottom);
    }
    UnlockCAMD(lock);
    set(data->endpoint_list, MUIA_List_Quiet, FALSE);
    set(data->diag_endpoint_list, MUIA_List_Quiet, FALSE);
    set(data->route_from_list, MUIA_List_Quiet, FALSE);
    set(data->route_to_list, MUIA_List_Quiet, FALSE);
    restore_active(data->endpoint_list, selected[0]);
    restore_active(data->diag_endpoint_list, selected[1]);
    restore_active(data->route_from_list, selected[2]);
    restore_active(data->route_to_list, selected[3]);
    diag_refresh_bt(data);

    memset(data->route_states, 0, sizeof(data->route_states));
    data->router_running = FALSE;
    if (!router_command(MH_ROUTER_STATUS, &status)) {
        char state[80];
        data->router_running = TRUE;
        active = status.active; waiting = status.waiting;
        snprintf(state, sizeof(state), "Running: %lu active, %lu waiting",
                 (unsigned long)active, (unsigned long)waiting);
        set(data->router_state, MUIA_Text_Contents, state);
        set(data->router_start, MUIA_Disabled, TRUE);
        set(data->router_stop, MUIA_Disabled, FALSE);
        for (i = 0; i < status.route_count && i < MH_ROUTE_MAX; ++i)
            data->route_states[i] = status.route_state[i];
        snprintf(summary, sizeof(summary),
                 "CAMD: %lu endpoints | Router: %lu active, %lu waiting",
                 (unsigned long)data->endpoint_count,
                 (unsigned long)active, (unsigned long)waiting);
    } else {
        set(data->router_state, MUIA_Text_Contents,
            "Stopped: routes are not forwarded");
        set(data->router_start, MUIA_Disabled, FALSE);
        set(data->router_stop, MUIA_Disabled, TRUE);
        snprintf(summary, sizeof(summary),
                 "CAMD: %lu endpoints | Router is not running",
                 (unsigned long)data->endpoint_count);
    }
    set_status(data, summary);
    refresh_synth_state(data);
    DoMethod(data->route_list, MUIM_List_Redraw, MUIV_List_Redraw_All);
}

static int validate_routes(struct MHPrefsData *data)
{
    struct mh_route_table checked;
    char *text;
    size_t size = 1, offset = 0, error_line;
    ULONG i;
    int result;
    for (i = 0; i < data->routes.count; ++i)
        size += strlen(data->routes.routes[i].source) +
                strlen(data->routes.routes[i].destination) + 16;
    text = malloc(size);
    if (!text) return -1;
    for (i = 0; i < data->routes.count; ++i)
        offset += snprintf(text + offset, size - offset,
                           "route\t%d\t%d\t%s\t%s\n",
                           data->routes.routes[i].enabled,
                           data->routes.routes[i].reconnect,
                           data->routes.routes[i].source,
                           data->routes.routes[i].destination);
    result = mh_routes_parse(&checked, text, offset, &error_line);
    free(text); return result;
}

static int collect_settings(struct MHPrefsData *data)
{
    STRPTR text = NULL;
    IPTR number = 0;
    get(data->network_session, MUIA_String_Contents, &text);
    if (!text || !text[0] || strlen(text) >= sizeof(data->network.session_name)) return -1;
    strcpy(data->network.session_name, text);
    get(data->network_local_port, MUIA_String_Integer, &number);
    if (number < 1 || number >= 65535) return -1;
    data->network.local_port = (uint16_t)number;
    get(data->network_peer_ip, MUIA_String_Contents, &text);
    if (!text || strlen(text) >= sizeof(data->network.peer_ip)) return -1;
    strcpy(data->network.peer_ip, text);
    get(data->network_peer_port, MUIA_String_Integer, &number);
    if (data->network.peer_ip[0] && (number < 1 || number >= 65535)) return -1;
    data->network.peer_port = data->network.peer_ip[0] ? (uint16_t)number : 0;
    return 0;
}

static int apply_settings(struct MHPrefsData *data, BOOL persistent)
{
    STRPTR soundfont = NULL;
    IPTR backend = 0;
    IPTR router_boot = TRUE, synth_boot = FALSE;
    CONST_STRPTR backend_name;
    get(data->router_boot, MUIA_Selected, &router_boot);
    get(data->synth_boot, MUIA_Selected, &synth_boot);
    if (collect_settings(data) || validate_routes(data) ||
        ensure_directory("ENV:MidiHub")) return -1;
    get(data->synth_soundfont, MUIA_String_Contents, &soundfont);
    get(data->synth_backend, MUIA_Cycle_Active, &backend);
    backend_name = backend == 1 ? "fluid" : "tiny";
    if (write_routes(data, "ENV:MidiHub/Routes") ||
        write_network(data, "ENV:MidiHub/Network") ||
        (soundfont && soundfont[0] &&
         write_value("ENV:MidiHub/SoundFont", soundfont)) ||
        write_value("ENV:MidiHub/Backend", backend_name) ||
        write_value("ENV:MidiHub/RouterAtBoot", router_boot ? "1" : "0") ||
        write_value("ENV:MidiHub/SynthAtBoot", synth_boot ? "1" : "0")) return -1;
    if (!soundfont || !soundfont[0])
        (void)DeleteFile("ENV:MidiHub/SoundFont");
    if (persistent && (ensure_directory("ENVARC:MidiHub") ||
        write_routes(data, "ENVARC:MidiHub/Routes") ||
        write_network(data, "ENVARC:MidiHub/Network") ||
        (soundfont && soundfont[0] &&
         write_value("ENVARC:MidiHub/SoundFont", soundfont)) ||
        write_value("ENVARC:MidiHub/Backend", backend_name) ||
        write_value("ENVARC:MidiHub/RouterAtBoot", router_boot ? "1" : "0") ||
        write_value("ENVARC:MidiHub/SynthAtBoot", synth_boot ? "1" : "0"))) return -1;
    if (persistent && (!soundfont || !soundfont[0]))
        (void)DeleteFile("ENVARC:MidiHub/SoundFont");
    /* With "Start at boot" off the router is left to the Start button. */
    if (router_command(MH_ROUTER_RELOAD, NULL) != 0 && data->routes.count > 0 &&
        router_boot) {
        Forbid();
        if (!FindPort((CONST_STRPTR)MIDIHUB_ROUTER_PORT)) {
            Permit();
            start_router();
        } else {
            Permit();
        }
    }
    data->dirty = FALSE;
    data->routes_changed = FALSE;
    refresh(data);
    return 0;
}

static int profile_name_valid(CONST_STRPTR name)
{
    ULONG i;
    if (!name || !name[0] || strlen((const char *)name) > MH_PREFS_PROFILE_NAME_MAX)
        return 0;
    for (i = 0; name[i]; ++i)
        if (!((name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == ' ' || name[i] == '_' || name[i] == '-'))
            return 0;
    return 1;
}

static void profile_path(char *path, size_t capacity, CONST_STRPTR name,
                         CONST_STRPTR file)
{
    snprintf(path, capacity, "ENVARC:MidiHub/Profiles/%s%s%s", name,
             file ? "/" : "", file ? (const char *)file : "");
}

static void refresh_profiles(struct MHPrefsData *data)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    data->profile_count = 0;
    set(data->profile_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->profile_list, MUIM_List_Clear);
    lock = Lock((STRPTR)"ENVARC:MidiHub/Profiles", ACCESS_READ);
    fib = lock ? AllocDosObject(DOS_FIB, NULL) : NULL;
    if (fib && Examine(lock, fib)) {
        while (data->profile_count < MH_PREFS_PROFILE_MAX && ExNext(lock, fib)) {
            if (fib->fib_DirEntryType > 0 && profile_name_valid(fib->fib_FileName)) {
                size_t length = strlen((const char *)fib->fib_FileName);
                memcpy(data->profiles[data->profile_count], fib->fib_FileName,
                       length);
                data->profiles[data->profile_count][length] = 0;
                DoMethod(data->profile_list, MUIM_List_InsertSingle,
                         data->profiles[data->profile_count],
                         MUIV_List_Insert_Bottom);
                ++data->profile_count;
            }
        }
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (lock) UnLock(lock);
    set(data->profile_list, MUIA_List_Quiet, FALSE);
}

static STRPTR selected_profile(struct MHPrefsData *data)
{
    STRPTR name = NULL;
    DoMethod(data->profile_list, MUIM_List_GetEntry,
             MUIV_List_GetEntry_Active, &name);
    return name;
}

static void save_profile(struct MHPrefsData *data)
{
    STRPTR name = NULL, soundfont = NULL;
    IPTR backend = 0;
    char directory[256], path[320];
    get(data->profile_name, MUIA_String_Contents, &name);
    if (!profile_name_valid(name)) {
        set_status(data, "Profile names may use letters, numbers, spaces, '_' and '-'.");
        return;
    }
    if (collect_settings(data) || validate_routes(data) ||
        ensure_directory("ENVARC:MidiHub") ||
        ensure_directory("ENVARC:MidiHub/Profiles")) {
        set_status(data, "The current settings cannot be saved as a profile.");
        return;
    }
    profile_path(directory, sizeof(directory), name, NULL);
    if (ensure_directory(directory)) {
        set_status(data, "Could not create the profile directory."); return;
    }
    profile_path(path, sizeof(path), name, "Routes");
    if (write_routes(data, path)) goto failed;
    profile_path(path, sizeof(path), name, "Network");
    if (write_network(data, path)) goto failed;
    get(data->synth_backend, MUIA_Cycle_Active, &backend);
    profile_path(path, sizeof(path), name, "Backend");
    if (write_value(path, backend == 1 ? "fluid" : "tiny")) goto failed;
    get(data->synth_soundfont, MUIA_String_Contents, &soundfont);
    if (soundfont && soundfont[0]) {
        profile_path(path, sizeof(path), name, "SoundFont");
        if (write_value(path, soundfont)) goto failed;
    } else {
        profile_path(path, sizeof(path), name, "SoundFont");
        (void)DeleteFile(path);
    }
    refresh_profiles(data);
    set_status(data, "Profile saved.");
    return;
failed:
    set_status(data, "Could not write the profile.");
}

static void load_profile(struct MHPrefsData *data)
{
    STRPTR name = selected_profile(data);
    char path[320], value[1024];
    if (!name) { set_status(data, "Select a profile first."); return; }
    profile_path(path, sizeof(path), name, "Routes");
    if (load_routes_path(data, path)) {
        set_status(data, "The profile route configuration is invalid."); return;
    }
    mh_config_defaults(&data->network);
    profile_path(path, sizeof(path), name, "Network");
    if (mh_config_load(path, &data->network)) {
        set_status(data, "The profile network configuration is invalid."); return;
    }
    set(data->network_session, MUIA_String_Contents, data->network.session_name);
    set(data->network_local_port, MUIA_String_Integer, data->network.local_port);
    set(data->network_peer_ip, MUIA_String_Contents, data->network.peer_ip);
    set(data->network_peer_port, MUIA_String_Integer, data->network.peer_port);
    profile_path(path, sizeof(path), name, "Backend");
    strcpy(value, "tiny");
    (void)read_value(path, value, sizeof(value));
    set(data->synth_backend, MUIA_Cycle_Active,
        strcmp(value, "fluid") == 0 ? 1 : 0);
    profile_path(path, sizeof(path), name, "SoundFont");
    if (read_value(path, value, sizeof(value))) value[0] = 0;
    set(data->synth_soundfont, MUIA_String_Contents, value);
    fill_routes(data);
    if (apply_settings(data, TRUE)) {
        set_status(data, "The profile loaded, but its settings could not be applied.");
        return;
    }
    if (!write_value("ENVARC:MidiHub/Profile", name))
        set_status(data, "Profile activated and saved as the startup profile.");
    else
        set_status(data, "Profile activated.");
}

static void delete_profile(struct MHPrefsData *data)
{
    STRPTR name = selected_profile(data);
    static CONST_STRPTR files[] = { "Routes", "Network", "Backend", "SoundFont", NULL };
    char path[320];
    ULONG i;
    if (!name) { set_status(data, "Select a profile first."); return; }
    for (i = 0; files[i]; ++i) {
        profile_path(path, sizeof(path), name, files[i]);
        (void)DeleteFile(path);
    }
    profile_path(path, sizeof(path), name, NULL);
    if (!DeleteFile(path)) {
        set_status(data, "Could not delete the profile."); return;
    }
    refresh_profiles(data);
    set_status(data, "Profile deleted.");
}

static void selected_endpoint(struct MHPrefsData *data, Object *list, Object *target)
{
    struct MHPrefsEndpoint *entry = NULL;
    DoMethod(list, MUIM_List_GetEntry,
             MUIV_List_GetEntry_Active, &entry);
    if (entry) {
        set(target, MUIA_String_Contents, (IPTR)entry->name);
        data->dirty = TRUE;
    } else set_status(data, "Select an endpoint first.");
}

/* Clicking an endpoint in the Routing page's From or To list. The lists
   are refilled on every CAMD change, which clears the selection: only an
   actual pick fills the field. */
static void picked_endpoint(struct MHPrefsData *data, Object *list, Object *target)
{
    struct MHPrefsEndpoint *entry = NULL;
    DoMethod(list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &entry);
    if (entry) {
        set(target, MUIA_String_Contents, (IPTR)entry->name);
        data->dirty = TRUE;
    }
}

static void add_route(struct MHPrefsData *data)
{
    STRPTR source = NULL, destination = NULL;
    IPTR reconnect = 0;
    struct mh_route_config *route;
    get(data->source_string, MUIA_String_Contents, &source);
    get(data->destination_string, MUIA_String_Contents, &destination);
    get(data->reconnect_check, MUIA_Selected, &reconnect);
    if (!source || !source[0] || !destination || !destination[0] ||
        data->routes.count >= MH_ROUTE_MAX) {
        set_status(data, "Enter a source and destination."); return;
    }
    route = &data->routes.routes[data->routes.count++];
    memset(route, 0, sizeof(*route));
    snprintf(route->source, sizeof(route->source), "%s", source);
    snprintf(route->destination, sizeof(route->destination), "%s", destination);
    route->enabled = 1; route->reconnect = reconnect != 0;
    if (validate_routes(data)) {
        --data->routes.count;
        set_status(data, "Invalid route or feedback cycle."); return;
    }
    data->dirty = TRUE; data->routes_changed = TRUE; fill_routes(data);
    set_status(data, "Route added. Choose Use or Save to apply it.");
}

static void remove_route(struct MHPrefsData *data)
{
    IPTR active = MUIV_List_Active_Off;
    get(data->route_list, MUIA_List_Active, &active);
    if ((LONG)active < 0 || (ULONG)active >= data->routes.count) return;
    memmove(&data->routes.routes[active], &data->routes.routes[active + 1],
            (data->routes.count - (ULONG)active - 1) * sizeof(data->routes.routes[0]));
    --data->routes.count; data->dirty = TRUE; data->routes_changed = TRUE;
    fill_routes(data);
    set_status(data, "Route removed. Choose Use or Save to apply it.");
}

static void preview_synth(struct MHPrefsData *data)
{
    struct MidiNode *node;
    struct MidiLink *link;
    MidiMsg message;
    struct TagItem node_tags[] = {
        { MIDI_Name, (IPTR)"MIDIHub Preferences preview" }, { TAG_DONE, 0 }
    };
    struct TagItem link_tags[] = {
        { MLINK_Name, (IPTR)"MIDIHub preview" },
        { MLINK_Location, (IPTR)"MIDIHub Synth" }, { TAG_DONE, 0 }
    };
    node = CreateMidiA(node_tags);
    link = node ? AddMidiLinkA(node, MLTYPE_Sender, link_tags) : NULL;
    if (!link || !MidiLinkConnected(link)) {
        if (link) RemoveMidiLink(link); if (node) DeleteMidi(node);
        set_status(data, "MIDIHub Synth is not running."); return;
    }
    message.mm_Status = 0x90; message.mm_Data1 = 60; message.mm_Data2 = 100;
    PutMidi(link, message.mm_Msg); Delay(12);
    message.mm_Status = 0x80; message.mm_Data2 = 0;
    PutMidi(link, message.mm_Msg);
    RemoveMidiLink(link); DeleteMidi(node);
    set_status(data, "Preview note sent through CAMD.");
}

static void open_prefs(CONST_STRPTR command)
{
    SystemTags((STRPTR)command, SYS_Asynch, TRUE, TAG_DONE);
}

static IPTR mNew(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct MHPrefsData *data;
    Object *main_group, *refresh_button, *source_button, *destination_button;
    Object *add_button, *preview_button, *usb_button, *bluetooth_button;
    Object *ble_button;
    Object *diag_monitor_button, *diag_stop_button, *diag_send_button;
    Object *diag_clear_button, *diag_refresh_button;
    Object *overview_ahi_button, *synth_ahi_button, *use_button, *save_button;
    Object *profile_save_button, *profile_load_button, *profile_delete_button;
    ULONG i;
    struct TagItem tags[] = { { TAG_MORE, (IPTR)msg->ops_AttrList } };
    struct opSet supermsg = { OM_NEW, tags, msg->ops_GInfo };

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)&supermsg);
    if (!obj) return 0;
    data = INST_DATA(cl, obj);
    memset(data, 0, sizeof(*data));
    data->cluster_signal = -1;
    data->diag_signal = -1;
    data->endpoint_hook.h_Entry = (HOOKFUNC)EndpointDisplay;
    data->route_hook.h_Entry = (HOOKFUNC)RouteDisplay;
    data->route_hook.h_Data = data;

    main_group = HGroup,
        Child, ListviewObject,
            MUIA_HorizWeight, 22,
            MUIA_Listview_List, data->nav_list = ListObject,
                InputListFrame,
                MUIA_List_SourceArray, nav_entries,
                End,
            End,
        Child, BalanceObject, End,
        Child, VGroup,
            Child, data->page_group = PageGroup,
                Child, VGroup,
                    Child, TextObject,
                        MUIA_Text_Contents, "CAMD endpoints available to every AROS target:", End,
                    Child, ListviewObject,
                        MUIA_Listview_List, data->endpoint_list = ListObject,
                            InputListFrame,
                            MUIA_List_Title, TRUE,
                            MUIA_List_Format, "BAR,BAR,BAR,",
                            MUIA_List_DisplayHook, &data->endpoint_hook,
                            End,
                        End,
                    Child, HGroup,
                        Child, refresh_button = SimpleButton("Refresh"),
                        Child, source_button = SimpleButton("Use as source"),
                        Child, destination_button = SimpleButton("Use as destination"),
                        End,
                    Child, HGroup, GroupFrameT("Transport preferences"),
                        Child, usb_button = SimpleButton("USB / Trident"),
                        Child, bluetooth_button = SimpleButton("Bluetooth"),
                        Child, ble_button = SimpleButton("BLE MIDI"),
                        Child, overview_ahi_button = SimpleButton("AHI"),
                        End,
                    End,
                Child, VGroup,
                    Child, ListviewObject,
                        MUIA_Listview_List, data->route_list = ListObject,
                            InputListFrame,
                            MUIA_List_Title, TRUE,
                            MUIA_List_Format, "BAR,BAR,BAR,",
                            MUIA_List_DisplayHook, &data->route_hook,
                            End,
                        End,
                    Child, VGroup, GroupFrameT("New route"),
                        Child, HGroup,
                            Child, VGroup,
                                Child, TextObject,
                                    MUIA_Text_Contents, "From: where MIDI comes in", End,
                                Child, ListviewObject,
                                    MUIA_Listview_List, data->route_from_list = ListObject,
                                        InputListFrame,
                                        MUIA_List_Format, "BAR,",
                                        MUIA_List_DisplayHook, &data->endpoint_hook,
                                        End,
                                    End,
                                End,
                            Child, VGroup,
                                Child, TextObject,
                                    MUIA_Text_Contents, "To: where it goes", End,
                                Child, ListviewObject,
                                    MUIA_Listview_List, data->route_to_list = ListObject,
                                        InputListFrame,
                                        MUIA_List_Format, "BAR,",
                                        MUIA_List_DisplayHook, &data->endpoint_hook,
                                        End,
                                    End,
                                End,
                            End,
                        Child, ColGroup(2),
                            Child, Label("From"),
                            Child, data->source_string = StringObject,
                                StringFrame, MUIA_String_MaxLen, MH_ROUTE_NAME_MAX + 1, End,
                            Child, Label("To"),
                            Child, data->destination_string = StringObject,
                                StringFrame, MUIA_String_MaxLen, MH_ROUTE_NAME_MAX + 1, End,
                            End,
                        /* Not in the ColGroup: a fixed-width checkmark there
                           caps the column, and the From and To fields with it. */
                        Child, HGroup,
                            Child, data->reconnect_check = MUI_MakeObject(MUIO_Checkmark, NULL),
                            Child, Label("Reconnect automatically when an endpoint comes back"),
                            Child, HSpace(0),
                            End,
                        Child, HGroup,
                            Child, add_button = SimpleButton("Add Route"),
                            Child, data->remove_button = SimpleButton("Remove Route"),
                            Child, HSpace(0),
                            End,
                        End,
                    Child, VGroup, GroupFrameT("Router service"),
                        Child, HGroup,
                            Child, data->router_state = TextObject,
                                TextFrame, MUIA_Background, MUII_TextBack, End,
                            Child, data->router_start = SimpleButton("Start"),
                            Child, data->router_stop = SimpleButton("Stop"),
                            End,
                        Child, HGroup,
                            Child, data->router_boot = MUI_MakeObject(MUIO_Checkmark, NULL),
                            Child, Label("Start at boot (routes keep working with this window closed)"),
                            Child, HSpace(0),
                            End,
                        End,
                    End,
                Child, VGroup,
                    Child, VGroup, GroupFrameT("AppleMIDI / RTP-MIDI session"),
                        Child, ColGroup(2),
                            Child, Label("Session name"),
                            Child, data->network_session = StringObject,
                                StringFrame, MUIA_String_MaxLen, 64, End,
                            Child, Label("Local control port"),
                            Child, data->network_local_port = StringObject,
                                StringFrame, MUIA_String_Accept, "0123456789", End,
                            Child, Label("Peer IPv4 (optional)"),
                            Child, data->network_peer_ip = StringObject,
                                StringFrame, MUIA_String_MaxLen, 64, End,
                            Child, Label("Peer control port"),
                            Child, data->network_peer_port = StringObject,
                                StringFrame, MUIA_String_Accept, "0123456789", End,
                            End,
                        End,
                    Child, TextObject,
                        MUIA_Text_Contents, "mDNS discovery remains enabled when no peer is configured. Restart MIDIHub after changing session settings.",
                        End,
                    Child, VSpace(0),
                    End,
                Child, VGroup,
                    Child, VGroup, GroupFrameT("SoundFont synthesizer"),
                        Child, ColGroup(2),
                            Child, Label("Backend"),
                            Child, data->synth_backend = CycleObject,
                                MUIA_Cycle_Entries, backend_entries, End,
                            Child, Label("SoundFont (.sf2)"),
                            Child, data->synth_soundfont = StringObject,
                                StringFrame, MUIA_String_MaxLen, 1024, End,
                            End,
                        Child, HGroup,
                            Child, preview_button = SimpleButton("Test Note"),
                            Child, synth_ahi_button = SimpleButton("AHI Preferences"),
                            Child, HSpace(0),
                            End,
                        End,
                    Child, VGroup, GroupFrameT("Synth service"),
                        Child, HGroup,
                            Child, data->synth_state = TextObject,
                                TextFrame, MUIA_Background, MUII_TextBack, End,
                            Child, data->synth_start = SimpleButton("Start"),
                            Child, data->synth_stop = SimpleButton("Stop"),
                            End,
                        Child, HGroup,
                            Child, data->synth_boot = MUI_MakeObject(MUIO_Checkmark, NULL),
                            Child, Label("Start at boot (route an input to MIDIHub Synth to play it)"),
                            Child, HSpace(0),
                            End,
                        End,
                    Child, TextObject,
                        MUIA_Text_Contents, "TinySoundFont is the package backend. FluidSynth is offered for external builds that provide it.", End,
                    Child, VSpace(0),
                    End,
                Child, VGroup,
                    Child, TextObject,
                        MUIA_Text_Contents, "Profiles store MIDIHub routes, Network MIDI, and synthesizer settings:", End,
                    Child, ListviewObject,
                        MUIA_Listview_List, data->profile_list = ListObject,
                            InputListFrame,
                            End,
                        End,
                    Child, ColGroup(2),
                        Child, Label("Profile name"),
                        Child, data->profile_name = StringObject,
                            StringFrame,
                            MUIA_String_MaxLen, MH_PREFS_PROFILE_NAME_MAX + 1,
                            End,
                        End,
                    Child, HGroup,
                        Child, profile_save_button = SimpleButton("Save Current"),
                        Child, profile_load_button = SimpleButton("Activate"),
                        Child, profile_delete_button = SimpleButton("Delete"),
                        End,
                    End,
                Child, VGroup,
                    Child, HGroup,
                        Child, VGroup, GroupFrameT("CAMD endpoints"),
                            Child, ListviewObject,
                                MUIA_Listview_List, data->diag_endpoint_list = ListObject,
                                    InputListFrame,
                                    MUIA_List_Title, TRUE,
                                    MUIA_List_Format, "BAR,BAR,BAR,",
                                    MUIA_List_DisplayHook, &data->endpoint_hook,
                                    End,
                                End,
                            Child, HGroup,
                                Child, diag_monitor_button = SimpleButton("Monitor"),
                                Child, diag_stop_button = SimpleButton("Stop"),
                                Child, diag_send_button = SimpleButton("Send test"),
                                End,
                            End,
                        Child, VGroup, GroupFrameT("Bluetooth LE MIDI"),
                            Child, data->diag_ble_text = TextObject,
                                TextFrame,
                                MUIA_Background, MUII_TextBack,
                                MUIA_Text_SetVMax, FALSE,
                                End,
                            Child, HGroup,
                                Child, data->diag_interval = MUI_MakeObject(MUIO_Checkmark, NULL),
                                Child, Label1("Ask centrals for a 15 ms connection interval"),
                                Child, HSpace(0),
                                End,
                            End,
                        End,
                    Child, VGroup, GroupFrameT("MIDI monitor"),
                        Child, ListviewObject,
                            MUIA_Listview_List, data->diag_monitor = ListObject,
                                ReadListFrame,
                                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                                End,
                            End,
                        Child, HGroup,
                            Child, data->diag_monitor_info = TextObject, End,
                            Child, diag_clear_button = SimpleButton("Clear"),
                            End,
                        End,
                    Child, VGroup, GroupFrameT("Bluetooth log"),
                        Child, ListviewObject,
                            MUIA_Listview_List, data->diag_btlog = ListObject,
                                ReadListFrame,
                                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                                End,
                            End,
                        Child, diag_refresh_button = SimpleButton("Refresh"),
                        End,
                    End,
                End,
            Child, data->status_text = TextObject,
                TextFrame, MUIA_Background, MUII_TextBack,
                MUIA_Text_Contents, "Starting...", End,
            Child, HGroup,
                Child, HSpace(0),
                Child, use_button = SimpleButton("Use"),
                Child, save_button = SimpleButton("Save"),
                End,
            End,
        End;
    if (!main_group) { CoerceMethod(cl, obj, OM_DISPOSE); return 0; }
    DoMethod(obj, OM_ADDMEMBER, main_group);
    set(data->nav_list, MUIA_List_Active, 0);
    set(data->reconnect_check, MUIA_Selected, TRUE);

    DoMethod(data->nav_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
             data->page_group, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
    DoMethod(refresh_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Refresh);
    DoMethod(source_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Source);
    DoMethod(destination_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Destination);
    DoMethod(data->route_from_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
             obj, 1, MUIM_MHP_RouteFrom);
    DoMethod(data->route_to_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
             obj, 1, MUIM_MHP_RouteTo);
    DoMethod(add_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_AddRoute);
    DoMethod(data->remove_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_RemoveRoute);
    DoMethod(preview_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Preview);
    DoMethod(usb_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenUSB);
    DoMethod(bluetooth_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenBluetooth);
    DoMethod(ble_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenBLEMidi);
    set(ble_button, MUIA_ShortHelp,
        (IPTR)"Port names and activity of btmidi.class,\n"
              "the Bluetooth LE MIDI class.");
    data->ble_button = ble_button;
    DoMethod(diag_monitor_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_DiagMonitor);
    DoMethod(diag_stop_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_DiagStop);
    DoMethod(diag_send_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_DiagSend);
    DoMethod(diag_clear_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_DiagClear);
    DoMethod(data->router_start, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_RouterStart);
    DoMethod(data->router_stop, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_RouterStop);
    DoMethod(data->router_boot, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
             obj, 1, MUIM_MHP_Changed);
    DoMethod(data->synth_start, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_SynthStart);
    DoMethod(data->synth_stop, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_SynthStop);
    DoMethod(data->synth_boot, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
             obj, 1, MUIM_MHP_Changed);
    DoMethod(diag_refresh_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Refresh);
    DoMethod(data->diag_interval, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
             obj, 1, MUIM_MHP_DiagInterval);
    DoMethod(overview_ahi_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenAHI);
    DoMethod(synth_ahi_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenAHI);
    DoMethod(use_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Use);
    DoMethod(save_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Save);
    DoMethod(profile_save_button, MUIM_Notify, MUIA_Pressed, FALSE,
             obj, 1, MUIM_MHP_SaveProfile);
    DoMethod(profile_load_button, MUIM_Notify, MUIA_Pressed, FALSE,
             obj, 1, MUIM_MHP_LoadProfile);
    DoMethod(profile_delete_button, MUIM_Notify, MUIA_Pressed, FALSE,
             obj, 1, MUIM_MHP_DeleteProfile);

    {
        Object *changed[] = { data->source_string, data->destination_string,
            data->network_session, data->network_local_port,
            data->network_peer_ip, data->network_peer_port,
            data->synth_soundfont, NULL };
        for (i = 0; changed[i]; ++i)
            DoMethod(changed[i], MUIM_Notify, MUIA_String_Acknowledge,
                     MUIV_EveryTime, obj, 1, MUIM_MHP_Changed);
    }
    DoMethod(data->synth_backend, MUIM_Notify, MUIA_Cycle_Active,
             MUIV_EveryTime, obj, 1, MUIM_MHP_Changed);
    return (IPTR)obj;
}

AROS_UFH3(IPTR, MHPrefsDispatcher,
          AROS_UFHA(struct IClass *, cl, A0),
          AROS_UFHA(Object *, obj, A2),
          AROS_UFHA(Msg, msg, A1))
{
    AROS_USERFUNC_INIT
    struct MHPrefsData *data;
    if (msg->MethodID == OM_NEW) return mNew(cl, obj, (struct opSet *)msg);
    data = INST_DATA(cl, obj);
    switch (msg->MethodID) {
        case MUIM_MHP_Refresh:
            if (data->cluster_watch) {
                struct ClusterWatchEvent event;
                /* Everything is listed again, so the events only need
                   taking. */
                while (GetClusterWatchEvent(data->cluster_watch, &event))
                    ;
            }
            refresh(data);
            return 0;
        case MUIM_MHP_Source:
            selected_endpoint(data, data->endpoint_list, data->source_string); return 0;
        case MUIM_MHP_Destination:
            selected_endpoint(data, data->endpoint_list, data->destination_string); return 0;
        case MUIM_MHP_ShowPage: {
            /* The page whose name starts like name, any case: PAGE=routing */
            STRPTR name = ((struct MUIP_MHP_ShowPage *)msg)->name;
            ULONG page, i;
            for (page = 0; name && name[0] && page < MHPAGE_COUNT; ++page) {
                for (i = 0; name[i] && nav_entries[page][i] &&
                     (name[i] | 0x20) == (nav_entries[page][i] | 0x20); ++i)
                    ;
                if (!name[i]) {
                    set(data->nav_list, MUIA_List_Active, page);
                    return TRUE;
                }
            }
            return FALSE;
        }
        case MUIM_MHP_RouteFrom:
            picked_endpoint(data, data->route_from_list, data->source_string); return 0;
        case MUIM_MHP_RouteTo:
            picked_endpoint(data, data->route_to_list, data->destination_string); return 0;
        case MUIM_MHP_AddRoute: add_route(data); return 0;
        case MUIM_MHP_RemoveRoute: remove_route(data); return 0;
        case MUIM_MHP_Preview: preview_synth(data); return 0;
        case MUIM_MHP_OpenUSB: open_prefs("SYS:Prefs/Trident"); return 0;
        case MUIM_MHP_OpenBluetooth: open_prefs("SYS:Prefs/Bluetooth"); return 0;
        case MUIM_MHP_OpenAHI: open_prefs("SYS:Prefs/AHI"); return 0;
        case MUIM_MHP_OpenBLEMidi: open_ble_midi(data); return 0;
        case MUIM_MHP_DiagMonitor: diag_monitor(data, obj); return 0;
        case MUIM_MHP_DiagStop: diag_stop(data); return 0;
        case MUIM_MHP_DiagSend: diag_send(data); return 0;
        case MUIM_MHP_DiagPoll: diag_poll(data); return 0;
        case MUIM_MHP_DiagInterval: diag_interval(data); return 0;
        case MUIM_MHP_RouterStart:
            start_router();
            refresh(data);
            return 0;
        case MUIM_MHP_RouterStop:
            stop_router();
            refresh(data);
            return 0;
        case MUIM_MHP_SynthStart:
            /* The synth reads the SoundFont and backend from ENV:. */
            refresh(data);
            if (data->dirty)
                set_status(data, "Use or Save first: the synth reads the saved SoundFont.");
            start_synth(data);
            refresh_synth_state(data);
            return 0;
        case MUIM_MHP_SynthStop:
            stop_synth();
            refresh(data);
            return 0;
        case MUIM_MHP_DiagClear:
            DoMethod(data->diag_monitor, MUIM_List_Clear);
            data->diag_lines = 0;
            data->diag_count = 0;
            diag_info(data);
            return 0;
        case MUIM_MHP_BluetoothEvent:
            {
                /* a device came or went, or a binding or class changed */
                struct Message *event;
                while ((event = GetMsg(data->bt_port)))
                    ReplyMsg(event);
            }
            refresh(data);
            return 0;
        case MUIM_MHP_Changed: data->dirty = TRUE; return 0;
        case MUIM_MHP_SaveProfile: save_profile(data); return 0;
        case MUIM_MHP_LoadProfile: load_profile(data); return 0;
        case MUIM_MHP_DeleteProfile: delete_profile(data); return 0;
        case MUIM_MHP_Use:
            set_status(data, apply_settings(data, FALSE) ?
                       "Could not apply the MIDIHub settings." :
                       "Settings applied to ENV:. Restart network or synth services if changed.");
            return 0;
        case MUIM_MHP_Save:
            set_status(data, apply_settings(data, TRUE) ?
                       "Could not save the MIDIHub settings." :
                       "Settings saved to ENV: and ENVARC:.");
            return 0;
        case MUIM_Setup:
            if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
            BluetoothBase = OpenLibrary((CONST_STRPTR)"bluetooth.library", 45);
            if (BluetoothBase && (data->bt_port = CreateMsgPort()) &&
                (data->bt_handler = btAddEventHandler(data->bt_port,
                     BEHMF_DEVICECONNECTED | BEHMF_DEVICEDISCONNECTED |
                     BEHMF_ADDBINDING | BEHMF_REMBINDING | BEHMF_SERVICESCHG |
                     BEHMF_ADDCLASS | BEHMF_REMCLASS | BEHMF_CONFIGCHG |
                     BEHMF_ADDERRORMSG))) {
                data->bt_input.ihn_Object = obj;
                data->bt_input.ihn_Method = MUIM_MHP_BluetoothEvent;
                data->bt_input.ihn_Signals = 1UL << data->bt_port->mp_SigBit;
                DoMethod(_app(obj), MUIM_Application_AddInputHandler, &data->bt_input);
                data->bt_input_added = TRUE;
            }
            set(data->ble_button, MUIA_Disabled, BluetoothBase == NULL);
            if (load_routes(data)) set_status(data, "The route configuration is invalid.");
            mh_config_defaults(&data->network);
            if (mh_config_load("ENV:MidiHub/Network", &data->network) != 0)
                (void)mh_config_load("ENVARC:MidiHub/Network", &data->network);
            set(data->network_session, MUIA_String_Contents, data->network.session_name);
            set(data->network_local_port, MUIA_String_Integer, data->network.local_port);
            set(data->network_peer_ip, MUIA_String_Contents, data->network.peer_ip);
            set(data->network_peer_port, MUIA_String_Integer, data->network.peer_port);
            {
                char value[1024];
                if (read_value("ENV:MidiHub/SoundFont", value, sizeof(value)) &&
                    read_value("ENVARC:MidiHub/SoundFont", value, sizeof(value))) value[0] = 0;
                set(data->synth_soundfont, MUIA_String_Contents, value);
                if (read_value("ENV:MidiHub/Backend", value, sizeof(value)) &&
                    read_value("ENVARC:MidiHub/Backend", value, sizeof(value))) strcpy(value, "tiny");
                set(data->synth_backend, MUIA_Cycle_Active,
                    strcmp(value, "fluid") == 0 ? 1 : 0);
                if (read_value("ENV:MidiHub/RouterAtBoot", value, sizeof(value)) &&
                    read_value("ENVARC:MidiHub/RouterAtBoot", value, sizeof(value)))
                    strcpy(value, "1");
                nnset(data->router_boot, MUIA_Selected, strcmp(value, "0") != 0);
                if (read_value("ENV:MidiHub/SynthAtBoot", value, sizeof(value)) &&
                    read_value("ENVARC:MidiHub/SynthAtBoot", value, sizeof(value)))
                    strcpy(value, "0");
                nnset(data->synth_boot, MUIA_Selected, strcmp(value, "1") == 0);
            }
            fill_routes(data); refresh_profiles(data); refresh(data); diag_info(data);
            data->dirty = FALSE;
            data->cluster_signal = AllocSignal(-1);
            if (data->cluster_signal >= 0) {
                if (CamdBase->lib_Version >= 42) {
                    /* Also tells when a link joins or leaves a cluster,
                       which the Direction column shows. */
                    struct TagItem watch_tags[] = {
                        {CWA_SigBit, (IPTR)data->cluster_signal},
                        {TAG_DONE, 0}
                    };
                    data->cluster_watch = StartClusterWatchA(watch_tags);
                }
                if (!data->cluster_watch) {
                    data->cluster_notify.cnn_Task = FindTask(NULL);
                    data->cluster_notify.cnn_SigBit = data->cluster_signal;
                    StartClusterNotify(&data->cluster_notify);
                    data->notifying = TRUE;
                }
                data->input_handler.ihn_Object = obj;
                data->input_handler.ihn_Method = MUIM_MHP_Refresh;
                data->input_handler.ihn_Signals = 1UL << data->cluster_signal;
                DoMethod(_app(obj), MUIM_Application_AddInputHandler,
                         &data->input_handler);
                data->input_added = TRUE;
            }
            return TRUE;
        case MUIM_Cleanup:
            if (data->diag_input_added) {
                DoMethod(_app(obj), MUIM_Application_RemInputHandler, &data->diag_input);
                data->diag_input_added = FALSE;
            }
            diag_stop(data);
            if (data->diag_node) {
                DeleteMidi(data->diag_node);
                data->diag_node = NULL;
            }
            if (data->diag_signal >= 0) {
                FreeSignal(data->diag_signal);
                data->diag_signal = -1;
            }
            if (data->input_added) {
                DoMethod(_app(obj), MUIM_Application_RemInputHandler,
                         &data->input_handler);
                data->input_added = FALSE;
            }
            if (data->cluster_watch) {
                EndClusterWatch(data->cluster_watch);
                data->cluster_watch = NULL;
            }
            if (data->notifying) {
                EndClusterNotify(&data->cluster_notify);
                data->notifying = FALSE;
            }
            if (data->cluster_signal >= 0) {
                FreeSignal(data->cluster_signal); data->cluster_signal = -1;
            }
            if (data->bt_input_added) {
                DoMethod(_app(obj), MUIM_Application_RemInputHandler, &data->bt_input);
                data->bt_input_added = FALSE;
            }
            if (data->bt_handler) {
                btRemEventHandler(data->bt_handler);
                data->bt_handler = NULL;
            }
            if (data->bt_port) {
                struct Message *event;
                while ((event = GetMsg(data->bt_port)))
                    ReplyMsg(event);
                DeleteMsgPort(data->bt_port);
                data->bt_port = NULL;
            }
            if (BluetoothBase) {
                CloseLibrary(BluetoothBase);
                BluetoothBase = NULL;
            }
            break;
    }
    return DoSuperMethodA(cl, obj, msg);
    AROS_USERFUNC_EXIT
}
