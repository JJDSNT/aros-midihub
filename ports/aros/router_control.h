#ifndef MIDIHUB_ROUTER_CONTROL_H
#define MIDIHUB_ROUTER_CONTROL_H

#ifdef __AROS__
#include <exec/ports.h>

#define MIDIHUB_ROUTER_PORT "MIDIHub.Router"

enum mh_router_command {
    MH_ROUTER_STATUS = 1,
    MH_ROUTER_RELOAD,
    MH_ROUTER_STOP
};

struct mh_router_message {
    struct Message message;
    ULONG command;
    LONG result;
    ULONG configured;
    ULONG active;
    ULONG waiting;
};
#endif

#endif
