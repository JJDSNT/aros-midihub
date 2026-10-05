#ifndef MIDIHUB_BTMIDI_H
#define MIDIHUB_BTMIDI_H

#include LC_LIBDEFS_FILE

#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <utility/tagitem.h>

#include <libraries/bluetooth.h>
#include <libraries/btclass.h>

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

/* Written by the service task only; the settings window reads them. */
struct BTMidiStats {
    ULONG ms_RxPackets;   /* BLE MIDI packets written by centrals */
    ULONG ms_RxMessages;  /* MIDI messages delivered to CAMD */
    ULONG ms_TxPackets;   /* notifications queued to centrals */
    ULONG ms_TxMessages;  /* MIDI messages taken from CAMD */
    ULONG ms_Errors;      /* malformed packets and failed notifications */
};

enum btmidi_camd_state {
    BTMIDI_CAMD_CLOSED,
    BTMIDI_CAMD_OPEN,
    BTMIDI_CAMD_DEFAULTS  /* the configured names failed; defaults in use */
};

struct btmidi_gui;

struct BTMidiBase {
    struct Library library;
    struct Library *utility_base;
    struct Task *task;
    struct Task *ready_task;
    LONG ready_signal;

    struct BTMidiCfg cfg;           /* the configuration in effect */
    BOOL using_default_cfg;
    APTR record;                    /* the BLE MIDI service record */
    enum btmidi_camd_state camd_state;
    struct BTMidiStats stats;

    struct Task *gui_task;          /* the settings window, if open */
    struct btmidi_gui *gui;
    volatile BOOL activity_pending; /* the window has not refreshed yet */
};

void btmidi_default_cfg(struct BTMidiCfg *cfg);
void btmidi_load_cfg(struct BTMidiBase *base, struct Library *bluetooth);
void btmidi_store_cfg(struct BTMidiBase *base, struct Library *bluetooth,
                      BOOL to_disk);
void btmidi_reconfigure(struct BTMidiBase *base);
BOOL btmidi_open_cfg_window(struct BTMidiBase *base, struct Library *bluetooth);

AROS_UFP0(void, btmidi_gui_task);

#endif
