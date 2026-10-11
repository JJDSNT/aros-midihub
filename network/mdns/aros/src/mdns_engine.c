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

#define MDNS_QUESTIONS_MAX  8
#define MDNS_KNOWN_MAX      16

/* One packet at a time is taken apart, in mdnsd's only task. */
static struct MDNSQuestion questions[MDNS_QUESTIONS_MAX];
static char known[MDNS_KNOWN_MAX][256];
static struct MDNSRecord record;

void
MDNSEngineHandlePacket(struct MDNSEngineToken *tok,
    const UBYTE *pkt, LONG pktlen, ULONG srcaddr)
{
    struct MDNSHeader *hdr;
    struct MDNSQuestion q;
    struct MDNSRecord *r = &record;
    LONG offset;
    LONG consumed;
    LONG nq = 0, nknown = 0;
    UWORD i;
    UWORD flags;

    if (tok == NULL || pkt == NULL || pktlen < (LONG)sizeof(struct MDNSHeader)) {
        return;
    }

    /* The group's traffic is looped back to the sender. What this host said
     * it knows, and what it asked the link it does not ask itself. */
    if (srcaddr == tok->et_HostAddr) {
        return;
    }

    hdr = (struct MDNSHeader *)pkt;
    flags = ntohs(hdr->flags);
    offset = (LONG)sizeof(struct MDNSHeader);

    if ((flags & DNS_FLAG_QR) == 0) {
        for (i = 0; i < ntohs(hdr->qdcount); i++) {
            if (mdnsParseQuestion(pkt, pktlen, offset, &q, &consumed) != 0) {
                return;
            }
            if (nq < MDNS_QUESTIONS_MAX) {
                questions[nq++] = q;
            }
            offset += consumed;
        }
        /*
         * The answers of a query are what the asker already knows (RFC 6762
         * section 7.1): a PTR record it holds need not be sent again.
         */
        for (i = 0; i < ntohs(hdr->ancount); i++) {
            if (mdnsParseRecord(pkt, pktlen, offset, r, &consumed) != 0) {
                break;
            }
            if (r->type == DNS_TYPE_PTR && r->ttl >= MDNS_TTL_OTHER / 2 &&
                nknown < MDNS_KNOWN_MAX &&
                mdnsWireToName(pkt, pktlen, r->rdoffset, pkt, pktlen,
                    known[nknown], (LONG)sizeof(known[nknown])) >= 0) {
                nknown++;
            }
            offset += consumed;
        }
        tok->et_PacketSerial++;
        for (i = 0; i < nq; i++) {
            mdnsRegHandleQuery(tok, &questions[i], known, nknown);
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
        if (mdnsParseRecord(pkt, pktlen, offset, r, &consumed) != 0) {
            return;
        }
        tok->et_RxAnswers++;
        mdnsRegHandleAnswerName(tok, r->name, srcaddr);
        mdnsBrowseHandleRecord(tok, r, pkt, pktlen);
        mdnsQueryHandleRecord(tok, r, pkt, pktlen);
        offset += consumed;
    }

    /*
     * The authority section is walked only to reach the additional one, where
     * responders put the SRV, TXT and address records that go with a PTR
     * answer.
     */
    for (i = 0; i < ntohs(hdr->nscount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, r, &consumed) != 0) {
            return;
        }
        offset += consumed;
    }

    for (i = 0; i < ntohs(hdr->arcount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, r, &consumed) != 0) {
            return;
        }
        mdnsRegHandleAnswerName(tok, r->name, srcaddr);
        mdnsBrowseHandleRecord(tok, r, pkt, pktlen);
        mdnsQueryHandleRecord(tok, r, pkt, pktlen);
        offset += consumed;
    }
}

/* Returns FALSE when the message is kept and answered later. */
BOOL
MDNSEngineDispatch(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    if (tok == NULL || msg == NULL) {
        return TRUE;
    }

    if (msg->bim_Version != BONAMI_IPC_VERSION) {
        msg->bim_Result = MDNSERR_BADPARAM;
        return TRUE;
    }

    switch (msg->bim_Command) {
    case BONAMI_MSG_RELEASE:
        /* A client is gone: nothing of its may be announced or called. */
        MDNSEngineReleaseServices(tok, msg->bim_Task);
        MDNSEngineReleaseBrowses(tok, msg->bim_Task);
        MDNSEngineReleaseQueries(tok, msg->bim_Task);
        MDNSEngineReleaseInquiries(tok, msg->bim_Task);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_LOOKUP:
        return MDNSEngineLookup(tok, msg);
    case BONAMI_MSG_QUERY:
        msg->bim_Result = MDNSEngineQuery(tok, msg);
        break;
    case BONAMI_MSG_UNPUBLISH:
        MDNSEngineUnpublishNIPC(tok, msg->bim_Task);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_STOPQUERY:
        MDNSEngineStopQuery(tok, msg->bim_Handle);
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
    return TRUE;
}

void
MDNSEngineTick(struct MDNSEngineToken *tok)
{
    if (tok == NULL) {
        return;
    }

    MDNSEngineInquiryTick(tok);
    mdnsBrowseRetick(tok);
    mdnsCacheExpire(tok);
    mdnsQueryTick(tok);
}
