/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 Jaime Dias
 *
 * mdns_query.c - host address lookups and questions for any record
 *
 * A lookup is a client's message that mdnsd keeps until it knows the host's
 * address or the client's time is up. A query is a standing question whose
 * every answer goes to the client's hook.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/utility.h>

#include <bonami/mdns.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"

/* A name without a dot is a host of the link; a trailing dot is dropped. */
static void
local_name(char *buf, LONG buflen, const char *name)
{
    LONG len;

    strncpy(buf, name, (size_t)buflen - 1);
    buf[buflen - 1] = '\0';
    len = (LONG)strlen(buf);
    if (len > 0 && buf[len - 1] == '.') {
        buf[--len] = '\0';
    }
    if (strchr(buf, '.') == NULL) {
        strncat(buf, "." MDNS_DOMAIN_LOCAL, (size_t)(buflen - 1 - len));
    }
}

/*------------------------------------------------------------------------*/
/* Lookups. bim_Dbg0 holds the tick at which the client's time is up and
 * bim_Dbg1 the tick of the next question. */

static void
lookup_reply(struct MDNSIPCMsg *msg, LONG result, ULONG addr)
{
    Remove(&msg->bim_Msg.mn_Node);
    msg->bim_Result = result;
    msg->bim_HostAddr = addr;
    ReplyMsg(&msg->bim_Msg);
}

BOOL
MDNSEngineLookup(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSHostEntry *he;
    char qname[256];
    char own[MDNS_MAX_HOST + 8];
    char *dot;
    ULONG seconds;

    msg->bim_QName[sizeof(msg->bim_QName) - 1] = '\0';
    if (msg->bim_QName[0] == '\0') {
        msg->bim_Result = MDNSERR_BADPARAM;
        return TRUE;
    }
    local_name(qname, (LONG)sizeof(qname), msg->bim_QName);
    strcpy(msg->bim_QName, qname);

    /* This host's own name is not asked of the link. */
    strncpy(own, tok->et_HostName, MDNS_MAX_HOST);
    own[MDNS_MAX_HOST] = '\0';
    if ((dot = strchr(own, '.')) != NULL) {
        *dot = '\0';
    }
    strcat(own, "." MDNS_DOMAIN_LOCAL);
    if (tok->et_HostAddr != 0 && Stricmp(own, msg->bim_QName) == 0) {
        msg->bim_HostAddr = tok->et_HostAddr;
        msg->bim_Result = MDNS_OK;
        return TRUE;
    }

    he = mdnsHostFind(tok, msg->bim_QName);
    if (he != NULL) {
        msg->bim_HostAddr = he->he_Addr;
        msg->bim_Result = MDNS_OK;
        return TRUE;
    }
    if (tok->et_Socket < 0) {
        msg->bim_Result = MDNSERR_NETWORK;
        return TRUE;
    }

    seconds = msg->bim_Flags != 0 ? msg->bim_Flags : 3;
    if (seconds > 60) {
        seconds = 60;
    }
    msg->bim_Dbg0 = mdnsEngineTicks() + seconds * 50;
    msg->bim_Dbg1 = mdnsEngineTicks() + 50;
    AddTail(&tok->et_Lookups, &msg->bim_Msg.mn_Node);
    mdnsSendQuestion(tok, msg->bim_QName, DNS_TYPE_A, TRUE);
    return FALSE;
}

/* A host's address has just been heard. */
void
mdnsLookupHostKnown(struct MDNSEngineToken *tok, const char *name, ULONG addr)
{
    struct MDNSIPCMsg *msg;
    struct Node *n;
    struct Node *next;

    for (n = tok->et_Lookups.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        msg = (struct MDNSIPCMsg *)n;
        if (Stricmp(msg->bim_QName, name) == 0) {
            lookup_reply(msg, MDNS_OK, addr);
        }
    }
}

/* mdnsd is ending: nobody may be left waiting. */
void
mdnsLookupAbort(struct MDNSEngineToken *tok)
{
    struct Node *n;

    while ((n = tok->et_Lookups.lh_Head) != NULL && n->ln_Succ != NULL) {
        lookup_reply((struct MDNSIPCMsg *)n, MDNSERR_NOENGINE, 0);
    }
}

/*------------------------------------------------------------------------*/
/* Queries */

LONG
MDNSEngineQuery(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSQuerySession *qs;

    msg->bim_QName[sizeof(msg->bim_QName) - 1] = '\0';
    if (msg->bim_QName[0] == '\0' || msg->bim_Hook == NULL) {
        return MDNSERR_BADPARAM;
    }
    qs = (struct MDNSQuerySession *)AllocMem(sizeof(*qs), MEMF_CLEAR);
    if (qs == NULL) {
        return MDNSERR_NOMEM;
    }
    local_name(qs->qs_Name, (LONG)sizeof(qs->qs_Name), msg->bim_QName);
    qs->qs_Type = msg->bim_Port;
    qs->qs_Hook = msg->bim_Hook;
    qs->qs_Task = msg->bim_Task;
    qs->qs_Ref = mdnsEngineNextHandle(tok);
    qs->qs_LastQuery = mdnsEngineNow();
    qs->qs_Interval = 1;
    AddTail(&tok->et_Queries, &qs->qs_Node);

    mdnsSendQuestion(tok, qs->qs_Name, qs->qs_Type, TRUE);
    msg->bim_Handle = qs->qs_Ref;
    return MDNS_OK;
}

void
MDNSEngineStopQuery(struct MDNSEngineToken *tok, APTR ref)
{
    struct Node *n;

    for (n = tok->et_Queries.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        if (((struct MDNSQuerySession *)n)->qs_Ref == ref) {
            Remove(n);
            FreeMem(n, sizeof(struct MDNSQuerySession));
            return;
        }
    }
}

void
MDNSEngineReleaseQueries(struct MDNSEngineToken *tok, struct Task *task)
{
    struct Node *n;
    struct Node *next;

    for (n = tok->et_Queries.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        if (((struct MDNSQuerySession *)n)->qs_Task == task) {
            Remove(n);
            FreeMem(n, sizeof(struct MDNSQuerySession));
        }
    }
}

/*
 * Copies a record's data for a client. A name inside it may point back into
 * the packet, which the client does not have, so the names of PTR and SRV
 * records are written out again in full.
 */
static void
copy_rdata(struct MDNSRecordMsg *out, struct MDNSRecord *r, const UBYTE *pkt,
    LONG pktlen)
{
    char name[256];
    LONG skip = -1, used;

    if (r->type == DNS_TYPE_PTR) {
        skip = 0;
    } else if (r->type == DNS_TYPE_SRV && r->rdlength >= 6) {
        skip = 6;
    }
    if (skip >= 0 &&
        mdnsWireToName(pkt, pktlen, r->rdoffset + skip, pkt, pktlen, name,
            (LONG)sizeof(name)) >= 0 &&
        mdnsNameToWire(name, out->qm_Data + skip, MDNS_MAX_RDATA - skip,
            &used) == 0) {
        memcpy(out->qm_Data, r->rdata, (size_t)skip);
        out->qm_Length = (UWORD)(skip + used);
        return;
    }
    out->qm_Length = r->rdlength;
    memcpy(out->qm_Data, r->rdata, r->rdlength);
}

void
mdnsQueryHandleRecord(struct MDNSEngineToken *tok, struct MDNSRecord *r,
    const UBYTE *pkt, LONG pktlen)
{
    static struct MDNSRecordMsg out;    /* mdnsd has one task */
    struct MDNSQuerySession *qs;
    struct Node *n;
    BOOL filled = FALSE;

    for (n = tok->et_Queries.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        qs = (struct MDNSQuerySession *)n;
        if ((qs->qs_Type != DNS_TYPE_ANY && qs->qs_Type != r->type) ||
            Stricmp(qs->qs_Name, r->name) != 0) {
            continue;
        }
        if (!filled) {
            memset(&out, 0, sizeof(out));
            out.qm_Code = MEVENT_RECORD;
            strncpy(out.qm_Name, r->name, sizeof(out.qm_Name) - 1);
            out.qm_Type = r->type;
            out.qm_Class = (UWORD)(r->rclass & 0x7fff);
            out.qm_TTL = r->ttl;
            copy_rdata(&out, r, pkt, pktlen);
            filled = TRUE;
        }
        CallHookPkt(qs->qs_Hook, qs->qs_Task, &out);
    }
}

/* Once a second: questions are asked again, more and more rarely, and
 * lookups whose time is up are answered. */
void
mdnsQueryTick(struct MDNSEngineToken *tok)
{
    struct MDNSQuerySession *qs;
    struct MDNSIPCMsg *msg;
    struct Node *n;
    struct Node *next;
    ULONG now = mdnsEngineNow();
    ULONG ticks = mdnsEngineTicks();

    for (n = tok->et_Queries.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        qs = (struct MDNSQuerySession *)n;
        if (now - qs->qs_LastQuery >= qs->qs_Interval) {
            mdnsSendQuestion(tok, qs->qs_Name, qs->qs_Type, FALSE);
            qs->qs_LastQuery = now;
            qs->qs_Interval *= 2;
            if (qs->qs_Interval > MDNS_BROWSE_INTERVAL_MAX) {
                qs->qs_Interval = MDNS_BROWSE_INTERVAL_MAX;
            }
        }
    }

    for (n = tok->et_Lookups.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        msg = (struct MDNSIPCMsg *)n;
        if ((LONG)(ticks - msg->bim_Dbg0) >= 0) {
            lookup_reply(msg, MDNSERR_TIMEOUT, 0);
        } else if ((LONG)(ticks - msg->bim_Dbg1) >= 0) {
            mdnsSendQuestion(tok, msg->bim_QName, DNS_TYPE_A, FALSE);
            msg->bim_Dbg1 = ticks + 50;
        }
    }
}
