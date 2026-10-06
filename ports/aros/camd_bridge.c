#include "camd_bridge.h"
#include <midihub/rtpmidi.h>

#include <stdlib.h>
#include <string.h>

#ifdef __AROS__
#include <exec/libraries.h>
#include <exec/memory.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/exec.h>

/* Several bridges may live in one program, each in its own task (the
   btmidi.class service and one per bound device): none may close the
   library under another, so the base is per bridge. */
#define CamdBase ((struct Library *)bridge->camd_base)

int aros_camd_bridge_open_named(struct aros_camd_bridge *bridge,
                              char *node_name, char *incoming_name,
                              char *outgoing_name)
{
    struct TagItem node_tags[] = {
        {MIDI_Name, (IPTR)node_name},
        {MIDI_MsgQueue, 256},
        {MIDI_SysExSize, MH_SYSEX_MAX},
        {MIDI_RecvSignal, 0},
        {TAG_DONE, 0}
    };
    struct TagItem incoming_tags[] = {
        {MLINK_Name, (IPTR)incoming_name},
        {MLINK_Location, (IPTR)incoming_name},
        {TAG_DONE, 0}
    };
    struct TagItem outgoing_tags[] = {
        {MLINK_Name, (IPTR)outgoing_name},
        {MLINK_Location, (IPTR)outgoing_name},
        {TAG_DONE, 0}
    };

    memset(bridge, 0, sizeof(*bridge));
    bridge->signal_bit = -1;
    bridge->camd_base = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!bridge->camd_base)
        return -1;
    bridge->sysex_buffer = AllocVec(MH_SYSEX_MAX, MEMF_ANY);
    if (!bridge->sysex_buffer)
        goto fail;
    bridge->signal_bit = AllocSignal(-1);
    if (bridge->signal_bit < 0)
        goto fail;
    node_tags[3].ti_Data = (IPTR)bridge->signal_bit;
    bridge->node = CreateMidiA(node_tags);
    if (!bridge->node)
        goto fail;
    bridge->to_clients = AddMidiLinkA(bridge->node, MLTYPE_Sender,
                                      incoming_tags);
    if (!bridge->to_clients)
        goto fail;
    bridge->from_clients = AddMidiLinkA(bridge->node, MLTYPE_Receiver,
                                        outgoing_tags);
    if (!bridge->from_clients)
        goto fail;
    return 0;

fail:
    aros_camd_bridge_close(bridge);
    return -1;
}

int aros_camd_bridge_open(struct aros_camd_bridge *bridge)
{
    static char node_name[] = "MIDIHub";
    static char incoming_name[] = "MIDIHub In";
    static char outgoing_name[] = "MIDIHub Out";
    return aros_camd_bridge_open_named(bridge, node_name, incoming_name,
                                       outgoing_name);
}

void aros_camd_bridge_close(struct aros_camd_bridge *bridge)
{
    if (bridge->from_clients)
        RemoveMidiLink(bridge->from_clients);
    if (bridge->to_clients)
        RemoveMidiLink(bridge->to_clients);
    if (bridge->node)
        DeleteMidi(bridge->node);
    if (bridge->signal_bit >= 0)
        FreeSignal(bridge->signal_bit);
    FreeVec(bridge->sysex_buffer);
    if (bridge->camd_base)
        CloseLibrary(bridge->camd_base);
    memset(bridge, 0, sizeof(*bridge));
    bridge->signal_bit = -1;
}

void aros_camd_bridge_deliver(struct aros_camd_bridge *bridge,
                              const uint8_t *message, size_t length)
{
    uint32_t packed;
    uint8_t status;
    if (!bridge->to_clients || !message || length < 1 || length > 3)
        return;
    status = message[0];
    packed = ((uint32_t)status << 24) |
             ((uint32_t)(length > 1 ? message[1] : 0) << 16) |
             ((uint32_t)(length == 3 ? message[2] : 0) << 8);
    if (length != (size_t)MidiMsgLen(packed) ||
        (length > 1 && (message[1] & 0x80)) ||
        (length > 2 && (message[2] & 0x80)))
        return;
    PutMidi(bridge->to_clients, packed);
}

void aros_camd_bridge_deliver_sysex(struct aros_camd_bridge *bridge,
                                    const uint8_t *message, size_t length)
{
    if (!bridge->to_clients || !message || length < 2 ||
        length > MH_SYSEX_MAX || message[0] != 0xf0 ||
        message[length - 1] != 0xf7)
        return;
    PutSysEx(bridge->to_clients, (UBYTE *)message);
}

void aros_camd_bridge_poll(struct aros_camd_bridge *bridge,
                           aros_camd_output output, void *context)
{
    MidiMsg message;
    uint8_t bytes[3];
    size_t length;
    ULONG sysex_length;
    if (!bridge->node || !output)
        return;
    while (GetMidi(bridge->node, &message)) {
        if (message.mm_Status == 0xf0) {
            sysex_length = QuerySysEx(bridge->node);
            if (sysex_length > MH_SYSEX_MAX) {
                SkipSysEx(bridge->node);
            } else if (sysex_length >= 2 &&
                       GetSysEx(bridge->node, bridge->sysex_buffer,
                                sysex_length) == sysex_length) {
                output(context, bridge->sysex_buffer, sysex_length);
            } else
                SkipSysEx(bridge->node);
            continue;
        }
        bytes[0] = message.mm_Status;
        bytes[1] = message.mm_Data1;
        bytes[2] = message.mm_Data2;
        length = (size_t)MidiMsgLen(message.mm_Msg);
        if (length < 1 || length > sizeof(bytes))
            continue;
        output(context, bytes, length);
    }
}

#else

int aros_camd_bridge_open_named(struct aros_camd_bridge *bridge,
                              char *node_name, char *incoming_name,
                              char *outgoing_name)
{
    (void)node_name;
    (void)incoming_name;
    (void)outgoing_name;
    memset(bridge, 0, sizeof(*bridge));
    bridge->signal_bit = -1;
    return 0;
}

int aros_camd_bridge_open(struct aros_camd_bridge *bridge)
{
    return aros_camd_bridge_open_named(bridge, NULL, NULL, NULL);
}

void aros_camd_bridge_close(struct aros_camd_bridge *bridge)
{
    (void)bridge;
}

void aros_camd_bridge_deliver(struct aros_camd_bridge *bridge,
                              const uint8_t *message, size_t length)
{
    (void)bridge;
    (void)message;
    (void)length;
}

void aros_camd_bridge_deliver_sysex(struct aros_camd_bridge *bridge,
                                    const uint8_t *message, size_t length)
{
    (void)bridge;
    (void)message;
    (void)length;
}

void aros_camd_bridge_poll(struct aros_camd_bridge *bridge,
                           aros_camd_output output, void *context)
{
    (void)bridge;
    (void)output;
    (void)context;
}

#endif
