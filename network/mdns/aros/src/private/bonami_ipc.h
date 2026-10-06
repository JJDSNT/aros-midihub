/*
 * bonami_ipc.h - internal IPC to shared mDNS engine
 */

#ifndef BONAMI_PRIVATE_BONAMI_IPC_H
#define BONAMI_PRIVATE_BONAMI_IPC_H

#include <exec/types.h>
#include <exec/ports.h>
#include <utility/hooks.h>
#include "private/bonami_limits.h"

#define BONAMI_ENGINE_PORT  "BonAmi"
#define BONAMI_IPC_VERSION  1

#define BONAMI_MSG_REGISTER     1
#define BONAMI_MSG_UNREGISTER   2
#define BONAMI_MSG_UPDATE       3
#define BONAMI_MSG_BROWSE       4
#define BONAMI_MSG_STOPBROWSE   5
#define BONAMI_MSG_RESOLVE      6
#define BONAMI_MSG_STOPRESOLVE  7
#define BONAMI_MSG_STATUS       8
#define BONAMI_MSG_EVENT        9
#define BONAMI_MSG_SHUTDOWN     10
#define BONAMI_MSG_INQUIRY      11

struct MDNSIPCMsg
{
    struct Message  bim_Msg;
    UWORD           bim_Version;
    UWORD           bim_Command;
    LONG            bim_Result;
    APTR            bim_Handle;
    ULONG           bim_Flags;
    char            bim_Name[MDNS_MAX_NAME + 1];
    char            bim_ServiceType[MDNS_MAX_TYPE + 1];
    char            bim_Domain[MDNS_MAX_DOMAIN + 1];
    char            bim_HostName[MDNS_MAX_HOST + 1];
    char            bim_TXT[MDNS_MAX_TXT + 1];
    UWORD           bim_Port;
    UWORD           bim_Pad;
    ULONG           bim_HostAddr;
    struct Hook    *bim_Hook;
    struct Task    *bim_Task;
    ULONG           bim_Dbg0;       /* STATUS: records entering browse handler */
    ULONG           bim_Dbg1;       /* STATUS: PTR records seen */
    ULONG           bim_Dbg2;       /* STATUS: browse hook fires attempted */
};

#endif /* BONAMI_PRIVATE_BONAMI_IPC_H */
