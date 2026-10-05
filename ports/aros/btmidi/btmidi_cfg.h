#ifndef MIDIHUB_BTMIDI_CFG_H
#define MIDIHUB_BTMIDI_CFG_H

/* The btmidi.class configuration as bluetooth.library stores it (chunk
   'BMID' of the class configuration). MIDIHub.prefs reads it to recognise
   the peripheral role's CAMD ports; only the class's window changes it. */

#include <exec/types.h>

#define BTMIDI_NAME_SIZE 32

#define BTMIDI_DEFAULT_NODE "MIDIHub BLE"
#define BTMIDI_DEFAULT_IN   "MIDIHub BLE In"
#define BTMIDI_DEFAULT_OUT  "MIDIHub BLE Out"

/* The class configuration, stored as an IFF chunk by bluetooth.library.
   The names are those of the CAMD node and of its two clusters. */
struct BTMidiCfg {
    ULONG mc_ChunkID;
    ULONG mc_Length;
    char mc_NodeName[BTMIDI_NAME_SIZE];
    char mc_InName[BTMIDI_NAME_SIZE];
    char mc_OutName[BTMIDI_NAME_SIZE];
};

#endif
