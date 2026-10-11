/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_ipc.c - library-side IPC to mdnsd
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <utility/tagitem.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/utility.h>

#include <bonami/mdns.h>
#include "private/bonami_internal.h"
#include "private/bonami_ipc.h"

/* Is mdnsd there? It may still go away before the next call. */
LONG
MDNSEnsureEngine(struct BonamiPrivate *pb)
{
    struct MsgPort *port;

    (void)pb;
    Forbid();
    port = FindPort((STRPTR)BONAMI_ENGINE_PORT);
    Permit();
    return port != NULL ? MDNS_OK : MDNSERR_NOENGINE;
}

/* Tells mdnsd to drop every registration and browse of the calling task. */
void
MDNSReleaseTask(struct BonamiPrivate *pb)
{
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_RELEASE;
    MDNSSendIPC(pb, &msg);
}

LONG
MDNSTagsToRegisterMsg(struct TagItem *tags, struct MDNSIPCMsg *msg)
{
    struct TagItem *tag;
    struct TagItem *cur;
    STRPTR s;

    if (tags == NULL || msg == NULL) {
        return MDNSERR_BADPARAM;
    }

    msg->bim_Name[0] = '\0';
    msg->bim_ServiceType[0] = '\0';
    msg->bim_TXT[0] = '\0';
    msg->bim_Port = 0;
    strcpy(msg->bim_Domain, MDNS_DOMAIN_LOCAL);

    cur = tags;
    while ((tag = (struct TagItem *)NextTagItem(&cur)) != NULL) {
        switch (tag->ti_Tag) {
        case MDNS_ServiceName:
            s = (STRPTR)tag->ti_Data;
            if (s != NULL) {
                strncpy(msg->bim_Name, s, MDNS_MAX_NAME);
                msg->bim_Name[MDNS_MAX_NAME] = '\0';
            }
            break;
        case MDNS_ServiceType:
            s = (STRPTR)tag->ti_Data;
            if (s != NULL) {
                strncpy(msg->bim_ServiceType, s, MDNS_MAX_TYPE);
                msg->bim_ServiceType[MDNS_MAX_TYPE] = '\0';
            }
            break;
        case MDNS_Port:
            msg->bim_Port = (UWORD)tag->ti_Data;
            break;
        case MDNS_TXTRecord:
            s = (STRPTR)tag->ti_Data;
            if (s != NULL) {
                strncpy(msg->bim_TXT, s, MDNS_MAX_TXT);
                msg->bim_TXT[MDNS_MAX_TXT] = '\0';
            }
            break;
        case MDNS_Domain:
            s = (STRPTR)tag->ti_Data;
            if (s != NULL) {
                strncpy(msg->bim_Domain, s, MDNS_MAX_DOMAIN);
                msg->bim_Domain[MDNS_MAX_DOMAIN] = '\0';
            }
            break;
        default:
            break;
        }
    }

    if (msg->bim_Name[0] == '\0' || msg->bim_ServiceType[0] == '\0' || msg->bim_Port == 0) {
        return MDNSERR_BADPARAM;
    }

    return MDNS_OK;
}

LONG
MDNSSendIPC(struct BonamiPrivate *pb, struct MDNSIPCMsg *msg)
{
    struct MsgPort *enginePort;
    struct MsgPort *replyPort;
    struct MDNSIPCMsg *copy;

    if (pb == NULL || msg == NULL) {
        return MDNSERR_BADPARAM;
    }

    /*
     * A reply port of the calling task's own, so that clients calling at the
     * same time never take each other's reply.
     */
    replyPort = CreateMsgPort();
    if (replyPort == NULL) {
        return MDNSERR_NOMEM;
    }

    copy = (struct MDNSIPCMsg *)AllocMem(sizeof(*copy), MEMF_CLEAR);
    if (copy == NULL) {
        DeleteMsgPort(replyPort);
        return MDNSERR_NOMEM;
    }

    memcpy(copy, msg, sizeof(*copy));
    copy->bim_Msg.mn_Node.ln_Type = NT_MESSAGE;
    copy->bim_Msg.mn_Length = sizeof(*copy);
    copy->bim_Msg.mn_ReplyPort = replyPort;
    copy->bim_Version = BONAMI_IPC_VERSION;
    copy->bim_Result = MDNSERR_NOTREADY;
    copy->bim_Task = FindTask(NULL);

    /* mdnsd removes its port before it stops reading it, so a message sent
     * to a port found here is always answered. */
    Forbid();
    enginePort = FindPort((STRPTR)BONAMI_ENGINE_PORT);
    if (enginePort != NULL) {
        PutMsg(enginePort, &copy->bim_Msg);
    }
    Permit();
    if (enginePort == NULL) {
        FreeMem(copy, sizeof(*copy));
        DeleteMsgPort(replyPort);
        return MDNSERR_NOENGINE;
    }

    WaitPort(replyPort);
    GetMsg(replyPort);

    msg->bim_Result = copy->bim_Result;
    msg->bim_Handle = copy->bim_Handle;
    msg->bim_HostAddr = copy->bim_HostAddr;
    msg->bim_Flags = copy->bim_Flags;
    msg->bim_Port = copy->bim_Port;
    msg->bim_Dbg0 = copy->bim_Dbg0;
    msg->bim_Dbg1 = copy->bim_Dbg1;
    msg->bim_Dbg2 = copy->bim_Dbg2;
    FreeMem(copy, sizeof(*copy));
    DeleteMsgPort(replyPort);
    return msg->bim_Result;
}
