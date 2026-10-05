#include <midihub/config.h>
#include <midihub/routes.h>
#include "MIDIHubPrefsClass.h"
#include "router_control.h"

#include <dos/dostags.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <midi/camd.h>

#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>
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

static CONST_STRPTR nav_entries[] = {
    "Overview", "Routing", "Network MIDI", "Synthesizer", "Profiles", NULL
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
    if (contains_ci(name, "ble") || contains_ci(name, "bluetooth")) return "BLE MIDI";
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
        *columns++ = !route->enabled ? (STRPTR)"Disabled" :
                     index >= data->routes.count ? (STRPTR)"Pending" :
                     data->route_states[index] == MH_ROUTE_STATE_ACTIVE ? (STRPTR)"Active" :
                     data->route_states[index] == MH_ROUTE_STATE_WAITING ? (STRPTR)"Waiting" :
                     (STRPTR)"Pending";
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

static void refresh(struct MHPrefsData *data)
{
    struct MidiCluster *cluster;
    struct mh_router_message status;
    APTR lock;
    ULONG i, active = 0, waiting = 0;
    char summary[160];

    data->endpoint_count = 0;
    set(data->endpoint_list, MUIA_List_Quiet, TRUE);
    DoMethod(data->endpoint_list, MUIM_List_Clear);
    lock = LockCAMD(CD_Linkages);
    for (cluster = NextCluster(NULL); cluster &&
         data->endpoint_count < MH_PREFS_ENDPOINT_MAX;
         cluster = NextCluster(cluster)) {
        struct MHPrefsEndpoint *entry = &data->endpoints[data->endpoint_count++];
        int senders = !IsListEmpty(&cluster->mcl_Senders);
        int receivers = !IsListEmpty(&cluster->mcl_Receivers);
        snprintf(entry->name, sizeof(entry->name), "%s",
                 cluster->mcl_Node.ln_Name ? cluster->mcl_Node.ln_Name : "");
        snprintf(entry->transport, sizeof(entry->transport), "%s",
                 endpoint_transport(entry->name));
        snprintf(entry->direction, sizeof(entry->direction), "%s",
                 senders && receivers ? "In / Out" : senders ? "Source" :
                 receivers ? "Destination" : "Idle");
        snprintf(entry->state, sizeof(entry->state), "%s",
                 senders || receivers ? "Online" : "Idle");
        DoMethod(data->endpoint_list, MUIM_List_InsertSingle, entry,
                 MUIV_List_Insert_Bottom);
    }
    UnlockCAMD(lock);
    set(data->endpoint_list, MUIA_List_Quiet, FALSE);

    memset(data->route_states, 0, sizeof(data->route_states));
    if (!router_command(MH_ROUTER_STATUS, &status)) {
        active = status.active; waiting = status.waiting;
        for (i = 0; i < status.route_count && i < MH_ROUTE_MAX; ++i)
            data->route_states[i] = status.route_state[i];
        snprintf(summary, sizeof(summary),
                 "CAMD: %lu endpoints | Router: %lu active, %lu waiting",
                 (unsigned long)data->endpoint_count,
                 (unsigned long)active, (unsigned long)waiting);
    } else {
        snprintf(summary, sizeof(summary),
                 "CAMD: %lu endpoints | Router is not running",
                 (unsigned long)data->endpoint_count);
    }
    set_status(data, summary);
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
    CONST_STRPTR backend_name;
    if (collect_settings(data) || validate_routes(data) ||
        ensure_directory("ENV:MidiHub")) return -1;
    get(data->synth_soundfont, MUIA_String_Contents, &soundfont);
    get(data->synth_backend, MUIA_Cycle_Active, &backend);
    backend_name = backend == 1 ? "fluid" : "tiny";
    if (write_routes(data, "ENV:MidiHub/Routes") ||
        write_network(data, "ENV:MidiHub/Network") ||
        (soundfont && soundfont[0] &&
         write_value("ENV:MidiHub/SoundFont", soundfont)) ||
        write_value("ENV:MidiHub/Backend", backend_name)) return -1;
    if (!soundfont || !soundfont[0])
        (void)DeleteFile("ENV:MidiHub/SoundFont");
    if (persistent && (ensure_directory("ENVARC:MidiHub") ||
        write_routes(data, "ENVARC:MidiHub/Routes") ||
        write_network(data, "ENVARC:MidiHub/Network") ||
        (soundfont && soundfont[0] &&
         write_value("ENVARC:MidiHub/SoundFont", soundfont)) ||
        write_value("ENVARC:MidiHub/Backend", backend_name))) return -1;
    if (persistent && (!soundfont || !soundfont[0]))
        (void)DeleteFile("ENVARC:MidiHub/SoundFont");
    (void)router_command(MH_ROUTER_RELOAD, NULL);
    data->dirty = FALSE;
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

static void selected_endpoint(struct MHPrefsData *data, Object *target)
{
    struct MHPrefsEndpoint *entry = NULL;
    DoMethod(data->endpoint_list, MUIM_List_GetEntry,
             MUIV_List_GetEntry_Active, &entry);
    if (entry) {
        set(target, MUIA_String_Contents, (IPTR)entry->name);
        data->dirty = TRUE;
    } else set_status(data, "Select an endpoint first.");
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
    data->dirty = TRUE; fill_routes(data);
    set_status(data, "Route added. Choose Use or Save to apply it.");
}

static void remove_route(struct MHPrefsData *data)
{
    IPTR active = MUIV_List_Active_Off;
    get(data->route_list, MUIA_List_Active, &active);
    if ((LONG)active < 0 || (ULONG)active >= data->routes.count) return;
    memmove(&data->routes.routes[active], &data->routes.routes[active + 1],
            (data->routes.count - (ULONG)active - 1) * sizeof(data->routes.routes[0]));
    --data->routes.count; data->dirty = TRUE; fill_routes(data);
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
                    Child, ColGroup(2),
                        Child, Label("From"),
                        Child, data->source_string = StringObject,
                            StringFrame, MUIA_String_MaxLen, MH_ROUTE_NAME_MAX + 1, End,
                        Child, Label("To"),
                        Child, data->destination_string = StringObject,
                            StringFrame, MUIA_String_MaxLen, MH_ROUTE_NAME_MAX + 1, End,
                        Child, Label("Reconnect automatically"),
                        Child, data->reconnect_check = MUI_MakeObject(MUIO_Checkmark, NULL),
                        End,
                    Child, HGroup,
                        Child, add_button = SimpleButton("Add Route"),
                        Child, data->remove_button = SimpleButton("Remove Route"),
                        Child, HSpace(0),
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
    DoMethod(add_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_AddRoute);
    DoMethod(data->remove_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_RemoveRoute);
    DoMethod(preview_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_Preview);
    DoMethod(usb_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenUSB);
    DoMethod(bluetooth_button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MUIM_MHP_OpenBluetooth);
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
        case MUIM_MHP_Refresh: refresh(data); return 0;
        case MUIM_MHP_Source: selected_endpoint(data, data->source_string); return 0;
        case MUIM_MHP_Destination: selected_endpoint(data, data->destination_string); return 0;
        case MUIM_MHP_AddRoute: add_route(data); return 0;
        case MUIM_MHP_RemoveRoute: remove_route(data); return 0;
        case MUIM_MHP_Preview: preview_synth(data); return 0;
        case MUIM_MHP_OpenUSB: open_prefs("SYS:Prefs/Trident"); return 0;
        case MUIM_MHP_OpenBluetooth: open_prefs("SYS:Prefs/Bluetooth"); return 0;
        case MUIM_MHP_OpenAHI: open_prefs("SYS:Prefs/AHI"); return 0;
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
            }
            fill_routes(data); refresh_profiles(data); refresh(data); data->dirty = FALSE;
            data->cluster_signal = AllocSignal(-1);
            if (data->cluster_signal >= 0) {
                data->cluster_notify.cnn_Task = FindTask(NULL);
                data->cluster_notify.cnn_SigBit = data->cluster_signal;
                StartClusterNotify(&data->cluster_notify);
                data->notifying = TRUE;
                data->input_handler.ihn_Object = obj;
                data->input_handler.ihn_Method = MUIM_MHP_Refresh;
                data->input_handler.ihn_Signals = 1UL << data->cluster_signal;
                DoMethod(_app(obj), MUIM_Application_AddInputHandler,
                         &data->input_handler);
                data->input_added = TRUE;
            }
            return TRUE;
        case MUIM_Cleanup:
            if (data->input_added) {
                DoMethod(_app(obj), MUIM_Application_RemInputHandler,
                         &data->input_handler);
                data->input_added = FALSE;
            }
            if (data->notifying) {
                EndClusterNotify(&data->cluster_notify);
                data->notifying = FALSE;
            }
            if (data->cluster_signal >= 0) {
                FreeSignal(data->cluster_signal); data->cluster_signal = -1;
            }
            break;
    }
    return DoSuperMethodA(cl, obj, msg);
    AROS_USERFUNC_EXIT
}
