/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_ipc.c - library-side IPC to mdns.task
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
#include "private/mdns_engine.h"
#include "private/bonami_debug.h"

LONG
MDNSEnsureEngine(struct BonamiPrivate *pb)
{
    int rc;

    baDbgPut("bonami: MDNSEnsureEngine enter\n");
    if (pb == NULL) {
        baDbgPut("bonami: MDNSEnsureEngine pb=NULL\n");
        return MDNSERR_BADPARAM;
    }

    if (pb->EngineOpen && pb->EngineToken != NULL &&
        pb->EngineToken->et_Ready && pb->EngineToken->et_Port != NULL) {
        baDbgPut("bonami: MDNSEnsureEngine already open\n");
        return MDNS_OK;
    }

    baDbgPut("bonami: MDNSEnsureEngine attach\n");
    rc = MDNSTokenAttach(pb);
    if (rc != MDNS_OK) {
        baDbgPutLong("bonami: MDNSEnsureEngine attach rc=%ld\n", (LONG)rc);
        return (LONG)rc;
    }

    if (pb->EngineToken == NULL || pb->EngineToken->et_Port == NULL) {
        baDbgPut("bonami: MDNSEnsureEngine no engine port\n");
        return MDNSERR_NOENGINE;
    }

    baDbgPut("bonami: MDNSEnsureEngine ok\n");
    return MDNS_OK;
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
    struct MDNSEngineToken *tok;
    struct MsgPort *replyPort;
    struct MDNSIPCMsg *copy;
    LONG rc;

    baDbgPutLong("bonami: MDNSSendIPC cmd=%ld\n",
        (LONG)(msg != NULL ? (LONG)msg->bim_Command : -1L));
    if (pb == NULL || msg == NULL) {
        baDbgPut("bonami: MDNSSendIPC bad param\n");
        return MDNSERR_BADPARAM;
    }

    rc = MDNSEnsureEngine(pb);
    if (rc != MDNS_OK) {
        baDbgPutLong("bonami: MDNSSendIPC ensure rc=%ld\n", rc);
        return MDNSERR_NOENGINE;
    }

    tok = pb->EngineToken;
    if (tok == NULL || tok->et_Port == NULL) {
        return MDNSERR_NOENGINE;
    }

    /*
     * Use a reply port created in (and owned by) the calling task, so that
     * multiple clients calling concurrently from different tasks never share a
     * reply port.  A shared port would let one caller's WaitPort/GetMsg consume
     * another caller's reply.
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

    PutMsg(tok->et_Port, &copy->bim_Msg);
    WaitPort(replyPort);

    copy = (struct MDNSIPCMsg *)GetMsg(replyPort);
    if (copy == NULL) {
        DeleteMsgPort(replyPort);
        return MDNSERR_NOENGINE;
    }

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
    baDbgPutLong("bonami: MDNSSendIPC result=%ld\n", msg->bim_Result);
    return msg->bim_Result;
}
