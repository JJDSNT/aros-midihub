/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_engine.c - packet handling, IPC dispatch and the periodic tick
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <string.h>

#include <proto/exec.h>
#include <netinet/in.h>

#include <bonami/mdns.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"

void
MDNSEngineHandlePacket(struct MDNSEngineToken *tok,
    const UBYTE *pkt, LONG pktlen, ULONG srcaddr)
{
    struct MDNSHeader *hdr;
    struct MDNSQuestion q;
    struct MDNSRecord r;
    LONG offset;
    LONG consumed;
    UWORD i;
    UWORD flags;

    (void)srcaddr;

    if (tok == NULL || pkt == NULL || pktlen < (LONG)sizeof(struct MDNSHeader)) {
        return;
    }

    hdr = (struct MDNSHeader *)pkt;
    flags = ntohs(hdr->flags);
    offset = (LONG)sizeof(struct MDNSHeader);

    if ((flags & DNS_FLAG_QR) == 0) {
        UWORD qd;

        qd = ntohs(hdr->qdcount);
        for (i = 0; i < qd; i++) {
            if (mdnsParseQuestion(pkt, pktlen, offset, &q, &consumed) != 0) {
                return;
            }
            mdnsRegHandleQuery(tok, &q);
            offset += consumed;
        }
        return;
    }

    for (i = 0; i < ntohs(hdr->qdcount); i++) {
        if (mdnsParseQuestion(pkt, pktlen, offset, &q, &consumed) != 0) {
            return;
        }
        offset += consumed;
    }

    for (i = 0; i < ntohs(hdr->ancount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        tok->et_RxAnswers++;
        mdnsBrowseHandleRecord(tok, &r, pkt, pktlen);
        offset += consumed;
    }

    /*
     * Authority section is not useful to us, but we must still walk it to reach
     * the additional section, where responders normally place the SRV/TXT/A
     * records that accompany a PTR answer (especially in legacy-unicast
     * replies).  Feeding the additionals through the same record handler lets a
     * single response yield a fully-populated cache entry (host + port + txt).
     */
    for (i = 0; i < ntohs(hdr->nscount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        offset += consumed;
    }

    for (i = 0; i < ntohs(hdr->arcount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        mdnsBrowseHandleRecord(tok, &r, pkt, pktlen);
        offset += consumed;
    }
}

void
MDNSEngineDispatch(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    if (tok == NULL || msg == NULL) {
        return;
    }

    if (msg->bim_Version != BONAMI_IPC_VERSION) {
        msg->bim_Result = MDNSERR_BADPARAM;
        return;
    }

    switch (msg->bim_Command) {
    case BONAMI_MSG_RELEASE:
        /* A client is gone: nothing of its may be announced or called. */
        MDNSEngineReleaseServices(tok, msg->bim_Task);
        MDNSEngineReleaseBrowses(tok, msg->bim_Task);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_REGISTER:
        msg->bim_Result = MDNSEngineRegister(tok, msg);
        break;
    case BONAMI_MSG_UNREGISTER:
        MDNSEngineDeregister(tok, msg->bim_Handle);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_BROWSE:
        msg->bim_Result = MDNSEngineBrowse(tok, msg);
        break;
    case BONAMI_MSG_STOPBROWSE:
        MDNSEngineStopBrowse(tok, msg->bim_Handle);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_RESOLVE:
        msg->bim_Result = MDNSEngineResolve(tok, msg);
        break;
    case BONAMI_MSG_STATUS:
        msg->bim_HostAddr = tok->et_RxPackets;
        msg->bim_Flags = tok->et_TxPackets;
        msg->bim_Port = (UWORD)tok->et_RxAnswers;
        msg->bim_Dbg0 = tok->et_DbgRecs;
        msg->bim_Dbg1 = tok->et_DbgPtr;
        msg->bim_Dbg2 = tok->et_DbgNotify;
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_INQUIRY:
        msg->bim_Result = MDNSEngineInquiryNIPC(tok, msg);
        break;
    default:
        msg->bim_Result = MDNSERR_BADPARAM;
        break;
    }
}

void
MDNSEngineTick(struct MDNSEngineToken *tok)
{
    static ULONG lastAnnounce;

    if (tok == NULL) {
        return;
    }

    MDNSEngineInquiryTick(tok);
    mdnsBrowseRetick(tok);
    mdnsCacheExpire(tok);

    if (mdnsEngineNow() - lastAnnounce > 60) {
        mdnsRegAnnounceAll(tok);
        lastAnnounce = mdnsEngineNow();
    }
}
