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

#include <netinet/in.h>

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
        if (now - bs->bs_LastQuery >= bs->bs_Interval) {
            mdnsBrowseSendQuery(tok, bs->bs_Type, FALSE);
            bs->bs_LastQuery = now;
            bs->bs_Interval *= 2;
            if (bs->bs_Interval > MDNS_BROWSE_INTERVAL_MAX) {
                bs->bs_Interval = MDNS_BROWSE_INTERVAL_MAX;
            }
        }
    }
    ReleaseSemaphore(&tok->et_SessionLock);
}

/* Asks the link one question. The first time something is asked the answer
 * may come by unicast (RFC 6762 section 5.4). */
void
mdnsSendQuestion(struct MDNSEngineToken *tok, const char *name, UWORD qtype,
    BOOL unicast)
{
    UBYTE pkt[512];
    LONG pktlen;

    if (tok == NULL || name == NULL) {
        return;
    }
    if (mdnsBuildQuery(pkt, (LONG)sizeof(pkt), 0, name, qtype,
            (UWORD)(DNS_CLASS_IN | (unicast ? DNS_QU_BIT : 0)),
            &pktlen) == 0) {
        mdnsNetSendMulticast(tok, pkt, pktlen);
    }
}

void
mdnsBrowseSendQuery(struct MDNSEngineToken *tok, const char *type,
    BOOL unicast)
{
    char qname[256];

    if (tok == NULL || type == NULL) {
        return;
    }

    strncpy(qname, type, sizeof(qname) - 1);
    qname[sizeof(qname) - 1] = '\0';
    strncat(qname, ".", sizeof(qname) - 1 - strlen(qname));
    strncat(qname, MDNS_DOMAIN_LOCAL, sizeof(qname) - 1 - strlen(qname));
    mdnsSendQuestion(tok, qname, DNS_TYPE_PTR, unicast);
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
    msg.bm_Instance.si_HostAddr = htonl(ce->ce_HostAddr);
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

struct MDNSHostEntry *
mdnsHostFind(struct MDNSEngineToken *tok, const char *name)
{
    struct Node *n;

    for (n = tok->et_Hosts.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        if (Stricmp(((struct MDNSHostEntry *)n)->he_Name, name) == 0) {
            return (struct MDNSHostEntry *)n;
        }
    }
    return NULL;
}

/* Remembers a host's address; the host heard of longest ago makes room. */
static void
mdnsHostStore(struct MDNSEngineToken *tok, const char *name, ULONG addr)
{
    struct MDNSHostEntry *he;

    he = mdnsHostFind(tok, name);
    if (he != NULL) {
        Remove(&he->he_Node);
    } else if (tok->et_HostCount >= MDNS_HOSTS_MAX) {
        he = (struct MDNSHostEntry *)RemHead(&tok->et_Hosts);
    } else {
        he = (struct MDNSHostEntry *)AllocMem(sizeof(*he), MEMF_CLEAR);
        if (he != NULL) {
            tok->et_HostCount++;
        }
    }
    if (he == NULL) {
        return;
    }
    strncpy(he->he_Name, name, MDNS_MAX_HOST);
    he->he_Name[MDNS_MAX_HOST] = '\0';
    he->he_Addr = addr;
    AddTail(&tok->et_Hosts, &he->he_Node);
    mdnsLookupHostKnown(tok, name, addr);
}

/* Tells the browses of its type that an instance is gone and forgets it. */
static void
mdnsCacheDrop(struct MDNSEngineToken *tok, struct MDNSCacheEntry *ce)
{
    struct MDNSBrowseSession *bs;
    struct Node *n;

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
 * Forgets what other hosts announced and did not announce again within the
 * time their PTR record gave. Called once a second.
 */
void
mdnsCacheExpire(struct MDNSEngineToken *tok)
{
    struct MDNSCacheEntry *ce;
    struct Node *n;
    struct Node *next;
    ULONG now;

    if (tok == NULL) {
        return;
    }
    now = mdnsEngineNow();
    for (n = tok->et_Cache.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        ce = (struct MDNSCacheEntry *)n;
        if ((ce->ce_Flags & MDNS_CE_LOCAL) == 0 && ce->ce_Expire <= now) {
            mdnsCacheDrop(tok, ce);
        } else if (ce->ce_Flags & MDNS_CE_ASKED) {
            /* Known by its PTR record only, a second later: ask. */
            ce->ce_Flags &= ~MDNS_CE_ASKED;
            if (ce->ce_Host[0] == '\0' && ce->ce_Name[0] != '\0') {
                mdnsSendQuestion(tok, ce->ce_Name, DNS_TYPE_ANY, FALSE);
            }
        }
    }
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
    struct MDNSHostEntry *he;
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
            if (ce != NULL && r->ttl != 0) {
                ce->ce_Expire = mdnsEngineNow() + r->ttl;
            }
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

        /* A time to live of zero is the instance saying goodbye. */
        if (r->ttl == 0) {
            ce = mdnsCacheFind(tok, inst, type);
            if (ce != NULL && (ce->ce_Flags & MDNS_CE_LOCAL) == 0) {
                mdnsCacheDrop(tok, ce);
            }
            return;
        }

        ce = mdnsCacheAdd(tok, inst, type, &isNew);
        if (ce != NULL) {
            ce->ce_Expire = mdnsEngineNow() + r->ttl;
        }
        if (ce != NULL && isNew) {
            ce->ce_Flags |= MDNS_CE_ANNOUNCED;
            mdnsBrowseNotifyAll(tok, ce);
            /* The responder usually adds the rest; if it did not, the
             * instance's own records are asked for. */
            strncpy(ce->ce_Name, ptrname, sizeof(ce->ce_Name) - 1);
            ce->ce_Flags |= MDNS_CE_ASKED;
        }
        return;
    }

    /*
     * An address record is owned by a host, not by an instance: give it to
     * every instance whose SRV record named that host.
     */
    if (r->type == DNS_TYPE_A) {
        struct Node *n;

        if (r->rdlength != 4) {
            return;
        }
        addr = (ULONG)r->rdata[0] << 24;
        addr |= (ULONG)r->rdata[1] << 16;
        addr |= (ULONG)r->rdata[2] << 8;
        addr |= (ULONG)r->rdata[3];
        if (r->ttl != 0) {
            mdnsHostStore(tok, r->name, addr);
        }
        for (n = tok->et_Cache.lh_Head; n != NULL && n->ln_Succ != NULL;
            n = n->ln_Succ) {
            ce = (struct MDNSCacheEntry *)n;
            if (ce->ce_Host[0] != '\0' && ce->ce_HostAddr != addr &&
                Stricmp(ce->ce_Host, r->name) == 0) {
                ce->ce_HostAddr = addr;
                ce->ce_Flags |= MDNS_CE_ANNOUNCED;
                mdnsBrowseNotifyAll(tok, ce);
            }
        }
        return;
    }
    if ((r->type != DNS_TYPE_SRV && r->type != DNS_TYPE_TXT) || r->ttl == 0) {
        return;
    }

    /* SRV / TXT: owner name is "instance.<type>.local". */
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
            /* The host's address may have been heard before this record. */
            he = mdnsHostFind(tok, ce->ce_Host);
            if (he != NULL && ce->ce_HostAddr != he->he_Addr) {
                ce->ce_HostAddr = he->he_Addr;
                changed = TRUE;
            }
            if (he == NULL) {
                mdnsSendQuestion(tok, ce->ce_Host, DNS_TYPE_A, FALSE);
            }
        }
    } else if (r->type == DNS_TYPE_TXT && r->rdlength > 0) {
        /* The record's strings, joined by spaces, as a client gave them. */
        LONG in = 0, out = 0, len;

        while (in < r->rdlength) {
            len = r->rdata[in++];
            if (in + len > r->rdlength || out + len + 1 > MDNS_MAX_TXT) {
                break;
            }
            if (len > 0 && out > 0) {
                newtxt[out++] = ' ';
            }
            memcpy(newtxt + out, r->rdata + in, (size_t)len);
            out += len;
            in += len;
        }
        newtxt[out] = '\0';
        if (strcmp(ce->ce_TXT, newtxt) != 0) {
            strcpy(ce->ce_TXT, newtxt);
            changed = TRUE;
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
    ce->ce_Flags |= MDNS_CE_LOCAL;
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
        ce->ce_Flags |= MDNS_CE_ANNOUNCED | MDNS_CE_LOCAL;
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
    struct MDNSCacheEntry *ce;

    if (tok == NULL || rs == NULL) {
        return;
    }

    ce = mdnsCacheFind(tok, rs->rs_Instance, rs->rs_Type);
    if (ce != NULL) {
        mdnsCacheDrop(tok, ce);
    }
}

/*
 * Deliver services this machine already offers to a browse session that has just
 * started (the "you started Share, then Nearby" case).  Standard mDNS browse
 * behaviour is to immediately report everything already known.  Scans the local
 * service list, then the instances other hosts have announced.
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
        if (rs->rs_State != MDNS_RS_ANNOUNCED) {
            continue;       /* still probing: announced when it has its name */
        }
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

    /* And what other hosts have announced so far: mdnsd outlives its
     * clients, so an answer may have come long before this browse. */
    for (n = tok->et_Cache.lh_Head; !meta && n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        ce = (struct MDNSCacheEntry *)n;
        if ((ce->ce_Flags & MDNS_CE_LOCAL) == 0 &&
            Stricmp(ce->ce_Type, bs->bs_Type) == 0) {
            mdnsBrowseFireHook(tok, bs, ce, MEVENT_ADD);
        }
    }
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
    bs->bs_Interval = 1;
    bs->bs_MaxResponses = 64;
    bs->bs_Responses = 0;

    ObtainSemaphore(&tok->et_SessionLock);
    AddTail(&tok->et_BrowseSessions, &bs->bs_Node);
    ReleaseSemaphore(&tok->et_SessionLock);

    /* Report services this machine already offers, then query the network. */
    mdnsBrowseDeliverExisting(tok, bs);
    mdnsBrowseSendQuery(tok, msg->bim_ServiceType, TRUE);
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

/* Ends every browse a client started; its hooks are not called again. */
void
MDNSEngineReleaseBrowses(struct MDNSEngineToken *tok, struct Task *task)
{
    struct MDNSBrowseSession *bs;
    struct Node *n;
    struct Node *next;

    if (tok == NULL || task == NULL) {
        return;
    }
    ObtainSemaphore(&tok->et_SessionLock);
    for (n = tok->et_BrowseSessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        bs = (struct MDNSBrowseSession *)n;
        if (bs->bs_Task == task) {
            Remove(n);
            FreeMem(bs, sizeof(*bs));
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
        char fqdn[256];

        mdnsBrowseSendQuery(tok, msg->bim_ServiceType, TRUE);
        strncpy(fqdn, msg->bim_Name, sizeof(fqdn) - 1);
        fqdn[sizeof(fqdn) - 1] = '\0';
        strncat(fqdn, ".", sizeof(fqdn) - 1 - strlen(fqdn));
        strncat(fqdn, msg->bim_ServiceType, sizeof(fqdn) - 1 - strlen(fqdn));
        strncat(fqdn, "." MDNS_DOMAIN_LOCAL, sizeof(fqdn) - 1 - strlen(fqdn));
        mdnsSendQuestion(tok, fqdn, DNS_TYPE_ANY, FALSE);
        return MDNSERR_NOTFOUND;
    }

    memset(&rmsg, 0, sizeof(rmsg));
    rmsg.rm_Code = MEVENT_RESOLVED;
    strncpy(rmsg.rm_Instance.si_Name, ce->ce_Instance, MDNS_MAX_NAME);
    strncpy(rmsg.rm_Instance.si_Type, ce->ce_Type, MDNS_MAX_TYPE);
    strcpy(rmsg.rm_Instance.si_Domain, MDNS_DOMAIN_LOCAL);
    strncpy(rmsg.rm_Instance.si_HostName, ce->ce_Host, MDNS_MAX_HOST);
    rmsg.rm_Instance.si_HostAddr = htonl(ce->ce_HostAddr);
    rmsg.rm_Instance.si_Port = ce->ce_Port;
    rmsg.rm_Instance.si_TXTLength = (UWORD)strlen(ce->ce_TXT);
    strncpy((char *)rmsg.rm_Instance.si_TXT, ce->ce_TXT, MDNS_MAX_TXT);

    CallHookPkt(msg->bim_Hook, msg->bim_Task, &rmsg);
    return MDNS_OK;
}
