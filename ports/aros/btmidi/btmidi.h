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

struct BTMidiBase {
    struct Library library;
    struct Library *utility_base;
    struct Task *task;
    struct Task *ready_task;
    LONG ready_signal;
};

#endif
