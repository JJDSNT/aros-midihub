#include "camd_bridge.h"
#include <midihub/rtpmidi.h>

#include <stdlib.h>
#include <string.h>

#ifdef __AROS__
#include <exec/libraries.h>
#include <midi/camd.h>
#include <proto/camd.h>
#include <proto/exec.h>

struct Library *CamdBase;

int mh_camd_bridge_open_named(struct mh_camd_bridge *bridge,
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
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase)
        return -1;
    bridge->sysex_buffer = malloc(MH_SYSEX_MAX);
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
    mh_camd_bridge_close(bridge);
    return -1;
}

int mh_camd_bridge_open(struct mh_camd_bridge *bridge)
{
    static char node_name[] = "MIDIHub";
    static char incoming_name[] = "MIDIHub In";
    static char outgoing_name[] = "MIDIHub Out";
    return mh_camd_bridge_open_named(bridge, node_name, incoming_name,
                                     outgoing_name);
}

void mh_camd_bridge_close(struct mh_camd_bridge *bridge)
{
    if (bridge->from_clients)
        RemoveMidiLink(bridge->from_clients);
    if (bridge->to_clients)
        RemoveMidiLink(bridge->to_clients);
    if (bridge->node)
        DeleteMidi(bridge->node);
    if (bridge->signal_bit >= 0)
        FreeSignal(bridge->signal_bit);
    free(bridge->sysex_buffer);
    memset(bridge, 0, sizeof(*bridge));
    bridge->signal_bit = -1;
    if (CamdBase) {
        CloseLibrary(CamdBase);
        CamdBase = NULL;
    }
}

void mh_camd_bridge_deliver(struct mh_camd_bridge *bridge,
                            const uint8_t *message, size_t length)
{
    uint32_t packed;
    uint8_t status;
    size_t expected;
    if (!bridge->to_clients || !message || length < 1 || length > 3)
        return;
    status = message[0];
    if (status >= 0x80 && status <= 0xef)
        expected = ((status & 0xf0) == 0xc0 ||
                    (status & 0xf0) == 0xd0) ? 2u : 3u;
    else if (status == 0xf1 || status == 0xf3)
        expected = 2;
    else if (status == 0xf2)
        expected = 3;
    else if (status == 0xf6 || status >= 0xf8)
        expected = 1;
    else
        return;
    if (length != expected ||
        (length > 1 && (message[1] & 0x80)) ||
        (length > 2 && (message[2] & 0x80)))
        return;
    packed = ((uint32_t)status << 24) |
             ((uint32_t)(length > 1 ? message[1] : 0) << 16) |
             ((uint32_t)(length == 3 ? message[2] : 0) << 8);
    PutMidi(bridge->to_clients, packed);
}

void mh_camd_bridge_deliver_sysex(struct mh_camd_bridge *bridge,
                                  const uint8_t *message, size_t length)
{
    if (!bridge->to_clients || !message || length < 2 ||
        length > MH_SYSEX_MAX || message[0] != 0xf0 ||
        message[length - 1] != 0xf7)
        return;
    PutSysEx(bridge->to_clients, (UBYTE *)message);
}

void mh_camd_bridge_poll(struct mh_camd_bridge *bridge,
                         mh_camd_output output, void *context)
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
        if (bytes[0] >= 0x80 && bytes[0] <= 0xef)
            length = ((bytes[0] & 0xf0) == 0xc0 ||
                      (bytes[0] & 0xf0) == 0xd0) ? 2 : 3;
        else if (bytes[0] == 0xf1 || bytes[0] == 0xf3)
            length = 2;
        else if (bytes[0] == 0xf2)
            length = 3;
        else if (bytes[0] == 0xf6 || bytes[0] >= 0xf8)
            length = 1;
        else
            continue;
        output(context, bytes, length);
    }
}

#else

int mh_camd_bridge_open_named(struct mh_camd_bridge *bridge,
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

int mh_camd_bridge_open(struct mh_camd_bridge *bridge)
{
    return mh_camd_bridge_open_named(bridge, NULL, NULL, NULL);
}

void mh_camd_bridge_close(struct mh_camd_bridge *bridge)
{
    (void)bridge;
}

void mh_camd_bridge_deliver(struct mh_camd_bridge *bridge,
                            const uint8_t *message, size_t length)
{
    (void)bridge;
    (void)message;
    (void)length;
}

void mh_camd_bridge_deliver_sysex(struct mh_camd_bridge *bridge,
                                  const uint8_t *message, size_t length)
{
    (void)bridge;
    (void)message;
    (void)length;
}

void mh_camd_bridge_poll(struct mh_camd_bridge *bridge,
                         mh_camd_output output, void *context)
{
    (void)bridge;
    (void)output;
    (void)context;
}

#endif
