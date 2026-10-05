/* BLE MIDI GATT service and CAMD bridge for bluetooth.library. */

#include "btmidi.h"
#include <midihub/ble_midi.h>
#include <midihub/rtpmidi.h>
#include "midihub/camd_bridge.h"
#include <proto/bluetooth.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <string.h>
#include <sys/time.h>

#define BTMIDI_VALUE_SIZE 512
#define BTMIDI_TX_PACKET_SIZE 20

struct Library *BluetoothBase;

struct btmidi_runtime {
    struct BTMidiBase *base;
    char node_name[BTMIDI_NAME_SIZE];
    char in_name[BTMIDI_NAME_SIZE];
    char out_name[BTMIDI_NAME_SIZE];
    struct MsgPort *event_port;
    APTR event_handler;
    APTR record;
    struct mh_camd_bridge camd;
    struct mh_ble_midi_decoder decoder;
    UBYTE message[3];
    UBYTE message_length;
    UBYTE message_needed;
    UBYTE sysex[MH_SYSEX_MAX];
    ULONG sysex_length;
};

static UWORD now_milliseconds(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (UWORD)(((unsigned long long)now.tv_sec * 1000ULL +
                    (unsigned long long)now.tv_usec / 1000ULL) & 0x1fff);
}

static void deliver(struct btmidi_runtime *runtime, const UBYTE *message,
                    ULONG length)
{
    mh_camd_bridge_deliver(&runtime->camd, message, length);
    runtime->base->stats.ms_RxMessages++;
}

static void deliver_sysex(struct btmidi_runtime *runtime, const UBYTE *message,
                          ULONG length)
{
    mh_camd_bridge_deliver_sysex(&runtime->camd, message, length);
    runtime->base->stats.ms_RxMessages++;
}

static int received_byte(void *context, uint16_t timestamp, uint8_t byte)
{
    struct btmidi_runtime *runtime = context;
    (void)timestamp;
    if (byte >= 0xf8) {
        deliver(runtime, &byte, 1);
        return 0;
    }
    if (runtime->sysex_length) {
        if (runtime->sysex_length == MH_SYSEX_MAX) {
            runtime->sysex_length = 0;
            return -1;
        }
        runtime->sysex[runtime->sysex_length++] = byte;
        if (byte == 0xf7) {
            deliver_sysex(runtime, runtime->sysex, runtime->sysex_length);
            runtime->sysex_length = 0;
        }
        return 0;
    }
    if (byte == 0xf0) {
        runtime->sysex[0] = byte;
        runtime->sysex_length = 1;
        runtime->message_length = runtime->message_needed = 0;
        return 0;
    }
    if (byte & 0x80) {
        if (byte == 0xf6) {
            deliver(runtime, &byte, 1);
            runtime->message_length = runtime->message_needed = 0;
            return 0;
        }
        if (byte > 0xef && byte != 0xf1 && byte != 0xf2 && byte != 0xf3) {
            runtime->message_length = runtime->message_needed = 0;
            return 0;
        }
        runtime->message[0] = byte;
        runtime->message_length = 1;
        runtime->message_needed = ((byte & 0xf0) == 0xc0 ||
                                   (byte & 0xf0) == 0xd0 ||
                                   byte == 0xf1 || byte == 0xf3) ? 1 : 2;
        return 0;
    }
    if (!runtime->message_needed)
        return -1;
    runtime->message[runtime->message_length++] = byte;
    if (!--runtime->message_needed) {
        deliver(runtime, runtime->message, runtime->message_length);
        runtime->message_length = 0;
    }
    return 0;
}

static int set_packet(struct btmidi_runtime *runtime,
                      const UBYTE *packet, ULONG length)
{
    if (btSetServiceValue(runtime->record, 0, (APTR)packet, length) != (LONG)length) {
        runtime->base->stats.ms_Errors++;
        return -1;
    }
    runtime->base->stats.ms_TxPackets++;
    return 0;
}

static int send_to_ble(void *context, const uint8_t *message, size_t length)
{
    struct btmidi_runtime *runtime = context;
    UBYTE packet[BTMIDI_TX_PACKET_SIZE];
    size_t written;
    UWORD timestamp = now_milliseconds();

    runtime->base->stats.ms_TxMessages++;

    if (length >= 2 && message[0] == 0xf0 && message[length - 1] == 0xf7) {
        size_t offset = 0;
        while (offset < length) {
            if (mh_ble_midi_encode_sysex_chunk(message, length, &offset,
                                                timestamp, packet,
                                                sizeof(packet), &written) ||
                set_packet(runtime, packet, written))
                return -1;
        }
        return 0;
    }
    if (mh_ble_midi_encode_message(message, length, timestamp, packet,
                                   sizeof(packet), &written))
        return -1;
    return set_packet(runtime, packet, written);
}

static APTR add_service(void)
{
    static const UBYTE service_uuid[16] = MH_BLE_MIDI_SERVICE_UUID;
    static const UBYTE io_uuid[16] = MH_BLE_MIDI_IO_UUID;
    struct BtGattCharDef characteristic;

    memset(&characteristic, 0, sizeof(characteristic));
    characteristic.bgd_UUID128 = io_uuid;
    characteristic.bgd_Properties = BGDP_READ | BGDP_WRITENR |
                                    BGDP_WRITE | BGDP_NOTIFY;
    characteristic.bgd_MaxLen = BTMIDI_VALUE_SIZE;
    return btAddServiceRecord(BSRA_Protocol, BSVP_ATT,
                              BSRA_UUID128, (IPTR)service_uuid,
                              BSRA_Name, (IPTR)"BLE MIDI",
                              BSRA_Owner, (IPTR)"btmidi.class",
                              BSRA_Characteristics, (IPTR)&characteristic,
                              BSRA_NumCharacteristics, 1, TAG_END);
}

static void handle_service_write(struct btmidi_runtime *runtime,
                                 struct Message *message)
{
    IPTR event = 0, index = -1, length = 0;
    APTR record = NULL;
    UBYTE *value = NULL;

    btGetAttrs(BGA_EVENTNOTE, message, BENA_EventID, &event,
               BENA_Param1, &record, BENA_Param2, &index,
               BENA_Data, &value, BENA_DataLength, &length, TAG_END);
    if (event == BEHMB_SERVICEWRITE && record == runtime->record && index == 0 &&
        value && length > 0 && length <= BTMIDI_VALUE_SIZE) {
        runtime->base->stats.ms_RxPackets++;
        if (mh_ble_midi_decode(&runtime->decoder, value, (size_t)length,
                               received_byte, runtime)) {
            runtime->base->stats.ms_Errors++;
            mh_ble_midi_decoder_init(&runtime->decoder);
            runtime->sysex_length = 0;
            runtime->message_length = runtime->message_needed = 0;
        }
    }
    ReplyMsg(message);
}

static void reset_parser(struct btmidi_runtime *runtime)
{
    mh_ble_midi_decoder_init(&runtime->decoder);
    runtime->sysex_length = 0;
    runtime->message_length = runtime->message_needed = 0;
}

/* Opens the CAMD node under the configured names. Should CAMD refuse them,
   the defaults keep the service usable. */
static void open_camd(struct btmidi_runtime *runtime)
{
    struct BTMidiBase *base = runtime->base;
    enum btmidi_camd_state state = BTMIDI_CAMD_OPEN;

    Forbid();
    CopyMem(base->cfg.mc_NodeName, runtime->node_name, BTMIDI_NAME_SIZE);
    CopyMem(base->cfg.mc_InName, runtime->in_name, BTMIDI_NAME_SIZE);
    CopyMem(base->cfg.mc_OutName, runtime->out_name, BTMIDI_NAME_SIZE);
    Permit();
    if (mh_camd_bridge_open_named(&runtime->camd, runtime->node_name,
                                  runtime->in_name, runtime->out_name)) {
        strcpy(runtime->node_name, BTMIDI_DEFAULT_NODE);
        strcpy(runtime->in_name, BTMIDI_DEFAULT_IN);
        strcpy(runtime->out_name, BTMIDI_DEFAULT_OUT);
        state = mh_camd_bridge_open_named(&runtime->camd, runtime->node_name,
                                          runtime->in_name, runtime->out_name)
                    ? BTMIDI_CAMD_CLOSED : BTMIDI_CAMD_DEFAULTS;
    }
    base->camd_state = state;
}

/* At most one wake-up per refresh of the settings window. */
static void notify_gui(struct BTMidiBase *base)
{
    Forbid();
    if (base->gui_task && !base->activity_pending) {
        base->activity_pending = TRUE;
        Signal(base->gui_task, SIGBREAKF_CTRL_F);
    }
    Permit();
}

AROS_UFH0(void, btmidi_task)
{
    AROS_USERFUNC_INIT
    struct Task *task = FindTask(NULL);
    struct btmidi_runtime runtime;
    struct Message *message;
    ULONG signals;

    memset(&runtime, 0, sizeof(runtime));
    runtime.base = task->tc_UserData;
    runtime.camd.signal_bit = -1;
    mh_ble_midi_decoder_init(&runtime.decoder);
    BluetoothBase = OpenLibrary((CONST_STRPTR)"bluetooth.library", 45);
    if (BluetoothBase && (BluetoothBase->lib_Version == 45) &&
        (BluetoothBase->lib_Revision < 18)) {
        CloseLibrary(BluetoothBase);
        BluetoothBase = NULL;
    }
    if (BluetoothBase && (runtime.event_port = CreateMsgPort()) &&
        (runtime.record = add_service())) {
        /* without CAMD the service still runs; the window tells */
        open_camd(&runtime);
        runtime.event_handler = btAddEventHandler(runtime.event_port,
                                                   BEHMF_SERVICEWRITE);
        if (runtime.event_handler) {
            runtime.base->record = runtime.record;
            runtime.base->task = task;
        }
    }
    Forbid();
    if (runtime.base->ready_task)
        Signal(runtime.base->ready_task, 1UL << runtime.base->ready_signal);
    Permit();
    if (runtime.base->task) {
        do {
            ULONG mask = (1UL << runtime.event_port->mp_SigBit) |
                         SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_E;

            if (runtime.camd.signal_bit >= 0)
                mask |= 1UL << runtime.camd.signal_bit;
            signals = Wait(mask);
            while ((message = GetMsg(runtime.event_port)))
                handle_service_write(&runtime, message);
            if ((runtime.camd.signal_bit >= 0) &&
                (signals & (1UL << runtime.camd.signal_bit)))
                mh_camd_bridge_poll(&runtime.camd, send_to_ble, &runtime);
            if (signals & SIGBREAKF_CTRL_E) {
                /* new port names: CAMD clients reconnect by name */
                mh_camd_bridge_close(&runtime.camd);
                reset_parser(&runtime);
                open_camd(&runtime);
            }
            notify_gui(runtime.base);
        } while (!(signals & SIGBREAKF_CTRL_C));
    }
    runtime.base->record = NULL;
    runtime.base->camd_state = BTMIDI_CAMD_CLOSED;
    if (runtime.event_handler)
        btRemEventHandler(runtime.event_handler);
    while (runtime.event_port && (message = GetMsg(runtime.event_port)))
        ReplyMsg(message);
    if (runtime.record)
        btRemServiceRecord(runtime.record);
    mh_camd_bridge_close(&runtime.camd);
    if (runtime.event_port)
        DeleteMsgPort(runtime.event_port);
    if (BluetoothBase)
        CloseLibrary(BluetoothBase);
    runtime.base->task = NULL;
    Forbid();
    if (runtime.base->ready_task)
        Signal(runtime.base->ready_task, 1UL << runtime.base->ready_signal);
    Permit();
    AROS_USERFUNC_EXIT
}

/* The configuration lives with the other Bluetooth class settings. The
   functions below take the caller's bluetooth.library base: the global one
   belongs to the service task. */
#define BluetoothBase bluetooth

static void terminate_name(char *name, const char *fallback)
{
    name[BTMIDI_NAME_SIZE - 1] = 0;
    if (!name[0])
        strcpy(name, fallback);
}

void btmidi_default_cfg(struct BTMidiCfg *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mc_ChunkID = AROS_LONG2BE(MAKE_ID('B','M','I','D'));
    cfg->mc_Length = AROS_LONG2BE(sizeof(*cfg) - 8);
    strcpy(cfg->mc_NodeName, BTMIDI_DEFAULT_NODE);
    strcpy(cfg->mc_InName, BTMIDI_DEFAULT_IN);
    strcpy(cfg->mc_OutName, BTMIDI_DEFAULT_OUT);
}

void btmidi_load_cfg(struct BTMidiBase *base, struct Library *bluetooth)
{
    struct BTMidiCfg *cfg = &base->cfg;
    struct BTMidiCfg *chunk;
    APTR pic;

    Forbid();
    btmidi_default_cfg(cfg);
    base->using_default_cfg = TRUE;
    if ((pic = btGetClsCfg((STRPTR)MOD_NAME_STRING)) &&
        (chunk = btGetCfgChunk(pic, AROS_LONG2BE(cfg->mc_ChunkID)))) {
        ULONG len = AROS_LONG2BE(chunk->mc_Length);

        if (len > sizeof(*cfg) - 8)
            len = sizeof(*cfg) - 8;
        CopyMem(((UBYTE *)chunk) + 8, ((UBYTE *)cfg) + 8, len);
        btFreeVec(chunk);
        terminate_name(cfg->mc_NodeName, BTMIDI_DEFAULT_NODE);
        terminate_name(cfg->mc_InName, BTMIDI_DEFAULT_IN);
        terminate_name(cfg->mc_OutName, BTMIDI_DEFAULT_OUT);
        base->using_default_cfg = FALSE;
    }
    Permit();
}

void btmidi_store_cfg(struct BTMidiBase *base, struct Library *bluetooth,
                      BOOL to_disk)
{
    APTR pic;

    if (!(pic = btGetClsCfg((STRPTR)MOD_NAME_STRING))) {
        btSetClsCfg((STRPTR)MOD_NAME_STRING, NULL);
        pic = btGetClsCfg((STRPTR)MOD_NAME_STRING);
    }
    if (pic && btAddCfgEntry(pic, &base->cfg)) {
        base->using_default_cfg = FALSE;
        if (to_disk)
            btSaveCfgToDisk(NULL, FALSE);
    }
}

BOOL btmidi_open_cfg_window(struct BTMidiBase *base, struct Library *bluetooth)
{
    BOOL ok = FALSE;

    Forbid();
    if (base->gui_task)
        ok = TRUE;
    else if (base->task &&
             (base->gui_task = btSpawnSubTask((STRPTR)"btmidi.class GUI",
                                              (APTR)btmidi_gui_task, base)))
        ok = TRUE;
    Permit();
    return ok;
}

void btmidi_reconfigure(struct BTMidiBase *base)
{
    Forbid();
    if (base->task)
        Signal(base->task, SIGBREAKF_CTRL_E);
    Permit();
}

static int GM_UNIQUENAME(libInit)(LIBBASETYPEPTR base)
{
    struct Library *bluetooth;
    struct Task *task;
    base->utility_base = OpenLibrary((CONST_STRPTR)"utility.library", 39);
    if (!base->utility_base)
        return FALSE;
    bluetooth = OpenLibrary((CONST_STRPTR)"bluetooth.library", 45);
    if (!bluetooth || ((bluetooth->lib_Version == 45) &&
                       (bluetooth->lib_Revision < 18))) {
        if (bluetooth) CloseLibrary(bluetooth);
        CloseLibrary(base->utility_base);
        return FALSE;
    }
    btmidi_load_cfg(base, bluetooth);
    base->ready_signal = SIGB_SINGLE;
    base->ready_task = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    task = btSpawnSubTask((STRPTR)"btmidi.class", (APTR)btmidi_task, base);
    if (task)
        btBorrowLocksWait(task, SIGF_SINGLE);
    base->ready_task = NULL;
    CloseLibrary(bluetooth);
    if (!base->task) {
        CloseLibrary(base->utility_base);
        return FALSE;
    }
    return TRUE;
}

static int GM_UNIQUENAME(libExpunge)(LIBBASETYPEPTR base)
{
    if (base->gui_task)
        return FALSE;
    if (base->task) {
        base->ready_signal = SIGB_SINGLE;
        base->ready_task = FindTask(NULL);
        SetSignal(0, SIGF_SINGLE);
        Signal(base->task, SIGBREAKF_CTRL_C);
        while (base->task) Wait(SIGF_SINGLE);
        base->ready_task = NULL;
    }
    CloseLibrary(base->utility_base);
    return TRUE;
}

ADD2INITLIB(GM_UNIQUENAME(libInit), 0)
ADD2EXPUNGELIB(GM_UNIQUENAME(libExpunge), 0)
#undef BluetoothBase

#define UtilityBase base->utility_base
AROS_LH3(LONG, btcGetAttrsA,
         AROS_LHA(ULONG, type, D0), AROS_LHA(APTR, object, A0),
         AROS_LHA(struct TagItem *, tags, A1),
         LIBBASETYPEPTR, base, 5, btmidi)
{
    AROS_LIBFUNC_INIT
    struct TagItem *tag;
    LONG count = 0;
    (void)object;
    if (type == BCGA_CLASS) {
        if ((tag = FindTagItem(BCCA_Priority, tags))) {
            *((SIPTR *)tag->ti_Data) = 0; count++;
        }
        if ((tag = FindTagItem(BCCA_Description, tags))) {
            *((STRPTR *)tag->ti_Data) = (STRPTR)"BLE MIDI GATT service and CAMD bridge"; count++;
        }
        if ((tag = FindTagItem(BCCA_HasClassCfgGUI, tags))) {
            *((IPTR *)tag->ti_Data) = TRUE; count++;
        }
        if ((tag = FindTagItem(BCCA_HasBindingCfgGUI, tags))) {
            *((IPTR *)tag->ti_Data) = FALSE; count++;
        }
        if ((tag = FindTagItem(BCCA_AfterDOSRestart, tags))) {
            *((IPTR *)tag->ti_Data) = TRUE; count++;
        }
        if ((tag = FindTagItem(BCCA_UsingDefaultCfg, tags))) {
            *((IPTR *)tag->ti_Data) = base->using_default_cfg; count++;
        }
    }
    return count;
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, btcSetAttrsA,
         AROS_LHA(ULONG, type, D0), AROS_LHA(APTR, object, A0),
         AROS_LHA(struct TagItem *, tags, A1),
         LIBBASETYPEPTR, base, 6, btmidi)
{
    AROS_LIBFUNC_INIT
    (void)type; (void)object; (void)tags;
    return 0;
    AROS_LIBFUNC_EXIT
}
#undef UtilityBase

AROS_LH2(SIPTR, btcDoMethodA,
         AROS_LHA(ULONG, method, D0), AROS_LHA(IPTR *, data, A1),
         LIBBASETYPEPTR, base, 7, btmidi)
{
    AROS_LIBFUNC_INIT
    struct Library *bluetooth;
    SIPTR result = 0;

    (void)data;
    switch (method) {
    case BCM_OpenCfgWindow:
    case BCM_ConfigChangedEvent:
        if (!(bluetooth = OpenLibrary((CONST_STRPTR)"bluetooth.library", 45)))
            break;
        if (method == BCM_OpenCfgWindow) {
            result = btmidi_open_cfg_window(base, bluetooth);
        } else if (!base->gui_task) {
            /* not while the window edits it; its Use and Save apply it */
            btmidi_load_cfg(base, bluetooth);
            btmidi_reconfigure(base);
            result = TRUE;
        }
        CloseLibrary(bluetooth);
        break;
    }
    return result;
    AROS_LIBFUNC_EXIT
}
