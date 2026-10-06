/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_browse.c - browse, cache, and Hook delivery
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

static struct MDNSCacheEntry *
mdnsCacheFind(struct MDNSEngineToken *tok, const char *inst, const char *type)
{
    struct MDNSCacheEntry *ce;
    struct Node *n;

    for (n = tok->et_Cache.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        ce = (struct MDNSCacheEntry *)n;
        if (Stricmp(ce->ce_Instance, inst) == 0 &&
            Stricmp(ce->ce_Type, type) == 0) {
            return ce;
        }
    }
    return NULL;
}

static struct MDNSCacheEntry *
mdnsCacheAdd(struct MDNSEngineToken *tok, const char *inst, const char *type,
    BOOL *outNew)
{
    struct MDNSCacheEntry *ce;

    if (outNew != NULL) {
        *outNew = FALSE;
    }

    ce = mdnsCacheFind(tok, inst, type);
    if (ce != NULL) {
        ce->ce_Expire = mdnsEngineNow() + MDNS_DEFAULT_TTL;
        return ce;
    }

    ce = (struct MDNSCacheEntry *)AllocMem(sizeof(*ce), MEMF_CLEAR);
    if (ce == NULL) {
        return NULL;
    }

    strncpy(ce->ce_Instance, inst, MDNS_MAX_NAME);
    strncpy(ce->ce_Type, type, MDNS_MAX_TYPE);
    ce->ce_Expire = mdnsEngineNow() + MDNS_DEFAULT_TTL;
    AddTail(&tok->et_Cache, &ce->ce_Node);
    if (outNew != NULL) {
        *outNew = TRUE;
    }
    return ce;
}

/*
 * Re-send outstanding browse PTR queries every few seconds so discovery
 * tolerates dropped multicast and late responders (normal mDNS browse
 * behaviour).  Called from MDNSEngineTick() once per engine loop second.
 */
void
mdnsBrowseRetick(struct MDNSEngineToken *tok)
{
    struct MDNSBrowseSession *bs;
    struct Node *n;
    ULONG now;

    if (tok == NULL) {
        return;
    }

    now = mdnsEngineNow();
    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        bs = (struct MDNSBrowseSession *)n;
        if (now - bs->bs_LastQuery >= 3) {
            mdnsBrowseSendQuery(tok, bs->bs_Type);
            bs->bs_LastQuery = now;
        }
    }
    ReleaseSemaphore(&tok->et_SessionLock);
}

void
mdnsBrowseSendQuery(struct MDNSEngineToken *tok, const char *type)
{
    UBYTE pkt[512];
    char qname[256];
    LONG pktlen;
    UWORD qid;

    if (tok == NULL || type == NULL) {
        return;
    }

    strncpy(qname, type, sizeof(qname) - 1);
    qname[sizeof(qname) - 1] = '\0';
    strncat(qname, ".", sizeof(qname) - 1 - strlen(qname));
    strncat(qname, MDNS_DOMAIN_LOCAL, sizeof(qname) - 1 - strlen(qname));

    qid = tok->et_QueryId++;
    /*
     * Set the QU (unicast-response) bit.  We bind an ephemeral source port
     * (see MDNSReadPort / mdnsNetOpen), so responders treat this as an RFC 6762
     * legacy-unicast query and send the answer by unicast straight back to our
     * port.  That is a plain unicast datagram our socket receives directly,
     * bypassing the multicast-group / co-bound-5353 delivery problems that make
     * multicast answers invisible to us under the host bsdsocket emulation.
     */
    if (mdnsBuildQuery(pkt, (LONG)sizeof(pkt), qid, qname, DNS_TYPE_PTR,
            (UWORD)(DNS_CLASS_IN | DNS_QU_BIT), &pktlen) == 0) {
        mdnsNetSendMulticast(tok, pkt, pktlen);
    }
}

void
mdnsBrowseFireHook(struct MDNSEngineToken *tok, struct MDNSBrowseSession *bs,
    struct MDNSCacheEntry *ce, ULONG code)
{
    struct MDNSBrowseMsg msg;

    if (bs == NULL || bs->bs_Hook == NULL || ce == NULL) {
        return;
    }

    if (tok != NULL) {
        tok->et_DbgNotify++;
    }

    /*
     * Zero first: the strncpy() calls below do not guarantee a terminator when
     * a source field is exactly MDNS_MAX_* long, and this msg lives on the
     * stack.  Without the memset a maximum-length name/type/host would reach
     * the client unterminated and its %s formatting would run off the end.
     */
    memset(&msg, 0, sizeof(msg));
    msg.bm_Code = code;
    strncpy(msg.bm_Instance.si_Name, ce->ce_Instance, MDNS_MAX_NAME);
    strncpy(msg.bm_Instance.si_Type, ce->ce_Type, MDNS_MAX_TYPE);
    strcpy(msg.bm_Instance.si_Domain, MDNS_DOMAIN_LOCAL);
    strncpy(msg.bm_Instance.si_HostName, ce->ce_Host, MDNS_MAX_HOST);
    msg.bm_Instance.si_HostAddr = ce->ce_HostAddr;
    msg.bm_Instance.si_Port = ce->ce_Port;
    msg.bm_Instance.si_TXTLength = (UWORD)strlen(ce->ce_TXT);
    strncpy((char *)msg.bm_Instance.si_TXT, ce->ce_TXT, MDNS_MAX_TXT);

    CallHookPkt(bs->bs_Hook, bs->bs_Task, &msg);
}

static void
mdnsBrowseNotifyAll(struct MDNSEngineToken *tok, struct MDNSCacheEntry *ce)
{
    struct MDNSBrowseSession *bs;
    struct MDNSInquirySession *is;
    struct Node *n;
    struct Node *next;

    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        bs = (struct MDNSBrowseSession *)n;
        if (Stricmp(bs->bs_Type, ce->ce_Type) == 0) {
            mdnsBrowseFireHook(tok, bs, ce, MEVENT_ADD);
        }
    }
    /*
     * mdnsInquiryFireNIPC() may Remove()/FreeMem() the session it is given
     * (when a one-shot inquiry has satisfied its response cap), so the next
     * link must be latched BEFORE the call - reading n->ln_Succ afterwards
     * would dereference a freed node and walk into corrupted memory.
     */
    for (n = tok->et_InquirySessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        is = (struct MDNSInquirySession *)n;
        mdnsInquiryFireNIPC(tok, is, ce);
    }
    ReleaseSemaphore(&tok->et_SessionLock);
}

/*
 * Remove the trailing mDNS domain from an FQDN label sequence, turning e.g.
 * "_googlecast._tcp.local" into "_googlecast._tcp".  A single trailing root
 * dot is dropped first so both "..local" and "..local." forms are handled.
 * Only the ".local" domain is stripped; the two-label service type itself
 * (e.g. "_proto._transport") is preserved intact.
 */
static void
mdnsStripDomain(char *name)
{
    LONG len;
    LONG dlen;

    if (name == NULL) {
        return;
    }
    len = (LONG)strlen(name);
    if (len > 0 && name[len - 1] == '.') {
        name[--len] = '\0';
    }
    dlen = (LONG)strlen(MDNS_DOMAIN_LOCAL) + 1;      /* ".local" */
    if (len >= dlen && name[len - dlen] == '.' &&
        Stricmp(&name[len - dlen + 1], MDNS_DOMAIN_LOCAL) == 0) {
        name[len - dlen] = '\0';
    }
}

/*
 * Split a two-label service type ("_proto._transport") into the instance-name
 * and remaining-type halves used by MDNSServiceInstance, so a type discovered
 * via the _services._dns-sd._udp enumeration can be reported through the same
 * MDNSBrowseMsg shape as a normal instance.
 */
static struct MDNSCacheEntry *
mdnsCacheAddType(struct MDNSEngineToken *tok, const char *svctype, BOOL *outNew)
{
    char inst[MDNS_MAX_NAME + 1];
    char type[MDNS_MAX_TYPE + 1];
    char *dot;

    strncpy(inst, svctype, MDNS_MAX_NAME);
    inst[MDNS_MAX_NAME] = '\0';
    dot = strchr(inst, '.');
    if (dot == NULL) {
        return NULL;
    }
    strncpy(type, dot + 1, MDNS_MAX_TYPE);
    type[MDNS_MAX_TYPE] = '\0';
    *dot = '\0';
    return mdnsCacheAdd(tok, inst, type, outNew);
}

/*
 * Deliver a discovered service *type* to every active meta browse (a browse of
 * MDNS_TYPE_SERVICES_DNS_SD).  These sessions cannot be matched by ce_Type, so
 * they are fired explicitly here.
 */
static void
mdnsMetaNotify(struct MDNSEngineToken *tok, struct MDNSCacheEntry *ce)
{
    struct MDNSBrowseSession *bs;
    struct Node *n;

    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        bs = (struct MDNSBrowseSession *)n;
        if (Stricmp(bs->bs_Type, MDNS_TYPE_SERVICES_DNS_SD) == 0) {
            mdnsBrowseFireHook(tok, bs, ce, MEVENT_ADD);
        }
    }
    ReleaseSemaphore(&tok->et_SessionLock);
}

void
mdnsBrowseHandleRecord(struct MDNSEngineToken *tok, struct MDNSRecord *r,
    const UBYTE *pkt, LONG pktlen)
{
    struct MDNSCacheEntry *ce;
    char inst[MDNS_MAX_NAME + 1];
    char type[MDNS_MAX_TYPE + 1];
    char hosttarget[MDNS_MAX_HOST + 1];
    char newtxt[MDNS_MAX_TXT + 1];
    char *dot;
    char ptrname[256];
    LONG instlen;
    UWORD port;
    ULONG addr;
    BOOL isNew;
    BOOL changed;

    if (tok == NULL || r == NULL || pkt == NULL) {
        return;
    }

    tok->et_DbgRecs++;

    if (r->type == DNS_TYPE_PTR) {
        tok->et_DbgPtr++;
        /*
         * The PTR target is decoded against the *whole packet* (pkt/rdoffset),
         * not the isolated rdata copy, because DNS name compression points
         * back into earlier packet bytes that rdata[] does not contain.
         */
        if (mdnsWireToName(pkt, pktlen, r->rdoffset, pkt, pktlen,
                ptrname, (LONG)sizeof(ptrname)) < 0) {
            return;
        }

        /*
         * Meta enumeration: when the PTR owner is _services._dns-sd._udp the
         * target is itself a service type, not an instance.  Report it to meta
         * browses so callers can discover which types exist on the LAN.  Only
         * fire on the first sight of a type -- re-queries re-deliver the same
         * PTR every few seconds, which must not re-notify.
         */
        if (Strnicmp((STRPTR)r->name, (STRPTR)MDNS_TYPE_SERVICES_DNS_SD,
                (LONG)strlen(MDNS_TYPE_SERVICES_DNS_SD)) == 0) {
            strncpy(type, ptrname, MDNS_MAX_TYPE);
            type[MDNS_MAX_TYPE] = '\0';
            mdnsStripDomain(type);
            ce = mdnsCacheAddType(tok, type, &isNew);
            if (ce != NULL && isNew) {
                ce->ce_Flags |= MDNS_CE_ANNOUNCED;
                mdnsMetaNotify(tok, ce);
            }
            return;
        }

        /* Normal browse: target is "instance.<type>.local". */
        strncpy(inst, ptrname, MDNS_MAX_NAME);
        inst[MDNS_MAX_NAME] = '\0';
        dot = strchr(inst, '.');
        if (dot == NULL) {
            return;
        }
        strncpy(type, dot + 1, MDNS_MAX_TYPE);
        type[MDNS_MAX_TYPE] = '\0';
        *dot = '\0';                    /* inst is now just the instance label */
        mdnsStripDomain(type);

        ce = mdnsCacheAdd(tok, inst, type, &isNew);
        if (ce != NULL && isNew) {
            ce->ce_Flags |= MDNS_CE_ANNOUNCED;
            mdnsBrowseNotifyAll(tok, ce);
        }
        return;
    }

    /* SRV / TXT / A: owner name is "instance.<type>.local". */
    dot = strchr(r->name, '.');
    if (dot == NULL) {
        return;
    }
    instlen = (LONG)(dot - r->name);
    if (instlen > MDNS_MAX_NAME) {
        instlen = MDNS_MAX_NAME;
    }
    memcpy(inst, r->name, (size_t)instlen);
    inst[instlen] = '\0';
    strncpy(type, dot + 1, MDNS_MAX_TYPE);
    type[MDNS_MAX_TYPE] = '\0';
    mdnsStripDomain(type);

    ce = mdnsCacheAdd(tok, inst, type, &isNew);
    if (ce == NULL) {
        return;
    }

    /*
     * Only re-notify when this record actually adds or changes information.
     * Re-received identical SRV/TXT/A records (from periodic re-queries or
     * duplicate answers) leave the entry unchanged and must stay silent.  A
     * brand-new entry created directly from an SRV/A/TXT (its PTR not yet seen)
     * announces once via isNew.
     */
    changed = isNew;

    if (r->type == DNS_TYPE_SRV && r->rdlength >= 6) {
        port = (UWORD)((r->rdata[4] << 8) | r->rdata[5]);
        if (ce->ce_Port != port) {
            ce->ce_Port = port;
            changed = TRUE;
        }
        /* SRV target is a compressed name too -> decode against full packet. */
        if (mdnsWireToName(pkt, pktlen, r->rdoffset + 6, pkt, pktlen,
                hosttarget, (LONG)sizeof(hosttarget)) >= 0) {
            if (Stricmp(ce->ce_Host, hosttarget) != 0) {
                strncpy(ce->ce_Host, hosttarget, MDNS_MAX_HOST);
                ce->ce_Host[MDNS_MAX_HOST] = '\0';
                changed = TRUE;
            }
        }
    } else if (r->type == DNS_TYPE_A && r->rdlength == 4) {
        addr = (ULONG)r->rdata[0] << 24;
        addr |= (ULONG)r->rdata[1] << 16;
        addr |= (ULONG)r->rdata[2] << 8;
        addr |= (ULONG)r->rdata[3];
        if (ce->ce_HostAddr != addr) {
            ce->ce_HostAddr = addr;
            changed = TRUE;
        }
    } else if (r->type == DNS_TYPE_TXT && r->rdlength > 0) {
        if (r->rdata[0] < r->rdlength) {
            memcpy(newtxt, r->rdata + 1, (size_t)r->rdata[0]);
            newtxt[r->rdata[0]] = '\0';
            if (strcmp(ce->ce_TXT, newtxt) != 0) {
                memcpy(ce->ce_TXT, newtxt, (size_t)r->rdata[0] + 1);
                changed = TRUE;
            }
        }
    } else {
        return;
    }

    if (changed) {
        ce->ce_Flags |= MDNS_CE_ANNOUNCED;
        mdnsBrowseNotifyAll(tok, ce);
    }
}

/*
 * Copy a locally-registered service into a cache entry so local browse and
 * resolve can serve it without a network round-trip.  On the network path the
 * cache is filled from received SRV/TXT/A records; here we fill it directly from
 * our own registration.  Under bsdsocket emulation the RX path may never see our
 * own multicast, so this direct copy is what makes "Share on this Amiga" visible
 * to "Nearby/Handshake on the same Amiga".
 */
static struct MDNSCacheEntry *
mdnsCacheAddLocal(struct MDNSEngineToken *tok, struct MDNSRegisteredService *rs,
    BOOL *outNew)
{
    struct MDNSCacheEntry *ce;

    ce = mdnsCacheAdd(tok, rs->rs_Instance, rs->rs_Type, outNew);
    if (ce == NULL) {
        return NULL;
    }
    strncpy(ce->ce_Host, rs->rs_HostTarget, MDNS_MAX_HOST);
    ce->ce_Host[MDNS_MAX_HOST] = '\0';
    ce->ce_Port = rs->rs_Port;
    ce->ce_HostAddr = rs->rs_HostAddr;
    strncpy(ce->ce_TXT, rs->rs_TXT, MDNS_MAX_TXT);
    ce->ce_TXT[MDNS_MAX_TXT] = '\0';
    return ce;
}

/*
 * Announce a newly-registered local service to any browse/inquiry sessions that
 * are already running (the "you started Nearby, then Share" case), and seed the
 * cache so a later local resolve finds it.  Runs in the engine task from the
 * REGISTER dispatch, after the service list lock has been released, so taking
 * the session lock here cannot deadlock.
 */
void
mdnsBrowseAnnounceLocal(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs)
{
    struct MDNSCacheEntry *ce;
    BOOL isNew;

    if (tok == NULL || rs == NULL) {
        return;
    }

    ce = mdnsCacheAddLocal(tok, rs, &isNew);
    if (ce != NULL) {
        ce->ce_Flags |= MDNS_CE_ANNOUNCED;
        mdnsBrowseNotifyAll(tok, ce);
    }

    /* Also surface the service *type* to any meta (_services._dns-sd) browse. */
    ce = mdnsCacheAddType(tok, rs->rs_Type, &isNew);
    if (ce != NULL) {
        ce->ce_Flags |= MDNS_CE_ANNOUNCED;
        mdnsMetaNotify(tok, ce);
    }
}

/*
 * Withdraw a local service: fire MEVENT_REMOVE to matching browse sessions and
 * drop the cache entry, so a Share stopped on this machine disappears from a
 * Nearby running on the same machine (and a later Handshake no longer resolves
 * a service that is gone).  Runs in the engine task from UNREGISTER dispatch.
 */
void
mdnsBrowseRemoveLocal(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs)
{
    struct MDNSBrowseSession *bs;
    struct MDNSCacheEntry *ce;
    struct Node *n;

    if (tok == NULL || rs == NULL) {
        return;
    }

    ce = mdnsCacheFind(tok, rs->rs_Instance, rs->rs_Type);
    if (ce == NULL) {
        return;
    }

    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        bs = (struct MDNSBrowseSession *)n;
        if (Stricmp(bs->bs_Type, ce->ce_Type) == 0) {
            mdnsBrowseFireHook(tok, bs, ce, MEVENT_REMOVE);
        }
    }
    ReleaseSemaphore(&tok->et_SessionLock);

    Remove(&ce->ce_Node);
    FreeMem(ce, sizeof(*ce));
}

/*
 * Deliver services this machine already offers to a browse session that has just
 * started (the "you started Share, then Nearby" case).  Standard mDNS browse
 * behaviour is to immediately report everything already known.  Scans the local
 * service list; the network-discovered cache is delivered lazily as records
 * continue to arrive.
 */
static void
mdnsBrowseDeliverExisting(struct MDNSEngineToken *tok,
    struct MDNSBrowseSession *bs)
{
    struct MDNSRegisteredService *rs;
    struct MDNSCacheEntry *ce;
    struct Node *n;
    BOOL isNew;
    BOOL meta;

    meta = (Stricmp(bs->bs_Type, MDNS_TYPE_SERVICES_DNS_SD) == 0) ? TRUE : FALSE;

    ObtainSemaphore(&tok->et_ServiceLock);
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (meta) {
            ce = mdnsCacheAddType(tok, rs->rs_Type, &isNew);
            if (ce != NULL) {
                mdnsBrowseFireHook(tok, bs, ce, MEVENT_ADD);
            }
        } else if (Stricmp(rs->rs_Type, bs->bs_Type) == 0) {
            ce = mdnsCacheAddLocal(tok, rs, &isNew);
            if (ce != NULL) {
                mdnsBrowseFireHook(tok, bs, ce, MEVENT_ADD);
            }
        }
    }
    ReleaseSemaphore(&tok->et_ServiceLock);
}

LONG
MDNSEngineBrowse(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSBrowseSession *bs;
    APTR ref;

    if (tok == NULL || msg == NULL) {
        return MDNSERR_BADPARAM;
    }

    bs = (struct MDNSBrowseSession *)AllocMem(sizeof(*bs), MEMF_CLEAR);
    if (bs == NULL) {
        return MDNSERR_NOMEM;
    }

    ref = mdnsEngineNextHandle(tok);
    bs->bs_Ref = ref;
    strncpy(bs->bs_Type, msg->bim_ServiceType, MDNS_MAX_TYPE);
    bs->bs_Hook = msg->bim_Hook;
    bs->bs_Task = msg->bim_Task;
    bs->bs_Expire = mdnsEngineNow() + 120;
    bs->bs_LastQuery = mdnsEngineNow();
    bs->bs_MaxResponses = 64;
    bs->bs_Responses = 0;

    ObtainSemaphore(&tok->et_SessionLock);
    AddTail(&tok->et_BrowseSessions, &bs->bs_Node);
    ReleaseSemaphore(&tok->et_SessionLock);

    /* Report services this machine already offers, then query the network. */
    mdnsBrowseDeliverExisting(tok, bs);
    mdnsBrowseSendQuery(tok, msg->bim_ServiceType);
    msg->bim_Handle = ref;
    return MDNS_OK;
}

void
MDNSEngineStopBrowse(struct MDNSEngineToken *tok, APTR ref)
{
    struct MDNSBrowseSession *bs;
    struct Node *n;

    if (tok == NULL || ref == NULL) {
        return;
    }

    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        bs = (struct MDNSBrowseSession *)n;
        if (bs->bs_Ref == ref) {
            Remove(n);
            FreeMem(bs, sizeof(*bs));
            break;
        }
    }
    ReleaseSemaphore(&tok->et_SessionLock);
}

LONG
MDNSEngineResolve(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSCacheEntry *ce;
    struct MDNSResolveMsg rmsg;

    if (tok == NULL || msg == NULL || msg->bim_Hook == NULL) {
        return MDNSERR_BADPARAM;
    }

    ce = mdnsCacheFind(tok, msg->bim_Name, msg->bim_ServiceType);
    if (ce == NULL) {
        mdnsBrowseSendQuery(tok, msg->bim_ServiceType);
        return MDNSERR_NOTFOUND;
    }

    memset(&rmsg, 0, sizeof(rmsg));
    rmsg.rm_Code = MEVENT_RESOLVED;
    strncpy(rmsg.rm_Instance.si_Name, ce->ce_Instance, MDNS_MAX_NAME);
    strncpy(rmsg.rm_Instance.si_Type, ce->ce_Type, MDNS_MAX_TYPE);
    strcpy(rmsg.rm_Instance.si_Domain, MDNS_DOMAIN_LOCAL);
    strncpy(rmsg.rm_Instance.si_HostName, ce->ce_Host, MDNS_MAX_HOST);
    rmsg.rm_Instance.si_HostAddr = ce->ce_HostAddr;
    rmsg.rm_Instance.si_Port = ce->ce_Port;
    rmsg.rm_Instance.si_TXTLength = (UWORD)strlen(ce->ce_TXT);
    strncpy((char *)rmsg.rm_Instance.si_TXT, ce->ce_TXT, MDNS_MAX_TXT);

    CallHookPkt(msg->bim_Hook, msg->bim_Task, &rmsg);
    return MDNS_OK;
}
