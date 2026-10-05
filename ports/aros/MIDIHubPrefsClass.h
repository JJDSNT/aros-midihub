#ifndef MIDIHUB_PREFS_CLASS_H
#define MIDIHUB_PREFS_CLASS_H

#include <midihub/config.h>
#include <midihub/routes.h>
#include <exec/types.h>
#include <midi/camd.h>
#include <libraries/mui.h>
#include <utility/hooks.h>

#define MH_PREFS_ENDPOINT_MAX 128
#define MH_PREFS_PROFILE_MAX 64
#define MH_PREFS_PROFILE_NAME_MAX 63
#define MH_PREFS_BLE_MAX 32

/* A CAMD cluster btmidi.class owns, as bluetooth.library describes it */
struct MHPrefsBle {
    char name[MH_ROUTE_NAME_MAX + 1];
    char state[16];
};

struct MHPrefsEndpoint {
    char name[MH_ROUTE_NAME_MAX + 1];
    char transport[20];
    char direction[16];
    char state[16];
};

struct MHPrefsData {
    Object *nav_list, *page_group;
    Object *endpoint_list, *route_list;
    Object *source_string, *destination_string, *reconnect_check;
    Object *remove_button;
    Object *network_session, *network_local_port;
    Object *network_peer_ip, *network_peer_port;
    Object *synth_backend, *synth_soundfont;
    Object *profile_list, *profile_name;
    Object *status_text;
    struct Hook endpoint_hook, route_hook;
    struct MHPrefsEndpoint endpoints[MH_PREFS_ENDPOINT_MAX];
    ULONG endpoint_count;
    struct mh_route_table routes;
    struct mh_network_config network;
    UBYTE route_states[MH_ROUTE_MAX];
    char profiles[MH_PREFS_PROFILE_MAX][MH_PREFS_PROFILE_NAME_MAX + 1];
    ULONG profile_count;
    struct ClusterNotifyNode cluster_notify;
    struct MUI_InputHandlerNode input_handler;
    BYTE cluster_signal;
    BOOL notifying, input_added, dirty;
    /* BLE MIDI, when bluetooth.library is there */
    Object *ble_button;
    struct MHPrefsBle ble[MH_PREFS_BLE_MAX];
    ULONG ble_count;
    struct MsgPort *bt_port;
    APTR bt_handler;
    struct MUI_InputHandlerNode bt_input;
    BOOL bt_input_added;
};

enum { MHPAGE_OVERVIEW, MHPAGE_ROUTING, MHPAGE_NETWORK,
       MHPAGE_SYNTH, MHPAGE_PROFILES, MHPAGE_COUNT };

#define TAGBASE_MHP (TAG_USER | 0x2d00)
#define MUIM_MHP_Refresh       (TAGBASE_MHP | 0x01)
#define MUIM_MHP_AddRoute      (TAGBASE_MHP | 0x02)
#define MUIM_MHP_RemoveRoute   (TAGBASE_MHP | 0x03)
#define MUIM_MHP_Source        (TAGBASE_MHP | 0x04)
#define MUIM_MHP_Destination   (TAGBASE_MHP | 0x05)
#define MUIM_MHP_Use           (TAGBASE_MHP | 0x06)
#define MUIM_MHP_Save          (TAGBASE_MHP | 0x07)
#define MUIM_MHP_Preview       (TAGBASE_MHP | 0x08)
#define MUIM_MHP_OpenUSB       (TAGBASE_MHP | 0x09)
#define MUIM_MHP_OpenBluetooth (TAGBASE_MHP | 0x0a)
#define MUIM_MHP_OpenAHI       (TAGBASE_MHP | 0x0b)
#define MUIM_MHP_Changed       (TAGBASE_MHP | 0x0c)
#define MUIM_MHP_SaveProfile   (TAGBASE_MHP | 0x0d)
#define MUIM_MHP_LoadProfile   (TAGBASE_MHP | 0x0e)
#define MUIM_MHP_DeleteProfile (TAGBASE_MHP | 0x0f)
#define MUIM_MHP_OpenBLEMidi   (TAGBASE_MHP | 0x10)
#define MUIM_MHP_BluetoothEvent (TAGBASE_MHP | 0x11)

AROS_UFP3(IPTR, MHPrefsDispatcher,
          AROS_UFPA(struct IClass *, cl, A0),
          AROS_UFPA(Object *, obj, A2),
          AROS_UFPA(Msg, msg, A1));

#endif
