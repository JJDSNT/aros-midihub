#ifndef MIDIHUB_ROUTER_CONTROL_H
#define MIDIHUB_ROUTER_CONTROL_H

#ifdef __AROS__
#include <exec/ports.h>
#include <midihub/routes.h>

#define MIDIHUB_ROUTER_PORT "MIDIHub.Router"

enum mh_router_command {
    MH_ROUTER_STATUS = 1,
    MH_ROUTER_RELOAD,
    MH_ROUTER_STOP
};

enum mh_router_route_state {
    MH_ROUTE_STATE_DISABLED = 0,
    MH_ROUTE_STATE_WAITING,
    MH_ROUTE_STATE_ACTIVE
};

struct mh_router_message {
    struct Message message;
    ULONG command;
    LONG result;
    ULONG configured;
    ULONG active;
    ULONG waiting;
    ULONG route_count;
    UBYTE route_state[MH_ROUTE_MAX];
};
#endif

#endif
