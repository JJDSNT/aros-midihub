/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_reg.c - service registration and query responses
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/utility.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <bonami/mdns.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"

static void
mdnsRegBuildFqdn(char *buf, LONG buflen, const char *inst, const char *type,
    const char *domain)
{
    if (inst == NULL || type == NULL || domain == NULL || buflen < 8) {
        return;
    }
    strncpy(buf, inst, buflen - 1);
    buf[buflen - 1] = '\0';
    strncat(buf, ".", buflen - 1 - strlen(buf));
    strncat(buf, type, buflen - 1 - strlen(buf));
    strncat(buf, ".", buflen - 1 - strlen(buf));
    strncat(buf, domain, buflen - 1 - strlen(buf));
}

static void
mdnsRegBuildTypeFqdn(char *buf, LONG buflen, const char *type, const char *domain)
{
    strncpy(buf, type, buflen - 1);
    buf[buflen - 1] = '\0';
    strncat(buf, ".", buflen - 1 - strlen(buf));
    strncat(buf, domain, buflen - 1 - strlen(buf));
}

/*
 * Build the host's fully-qualified name for the SRV target and A record.  The
 * engine stores only the bare host label (e.g. "amiga1" from gethostname());
 * wire-encoding that alone yields "amiga1." (one label + root), which is not
 * resolvable.  Append the mDNS domain to make "amiga1.local".  A host name
 * with a unicast DNS domain ("amiga1.example.org") keeps only its first
 * label: a name answered over mDNS must be in the mDNS domain.
 */
static void
mdnsRegBuildHostFqdn(char *buf, LONG buflen, const char *host,
    const char *domain)
{
    if (host == NULL || buflen < 4) {
        return;
    }
    strncpy(buf, host, buflen - 1);
    buf[buflen - 1] = '\0';
    if (strchr(buf, '.') != NULL) {
        *strchr(buf, '.') = '\0';
    }
    strncat(buf, ".", buflen - 1 - strlen(buf));
    strncat(buf, domain, buflen - 1 - strlen(buf));
}

static LONG
mdnsRegAppendSrv(UBYTE *rdata, const UBYTE *targetWire, LONG targetLen,
    UWORD port)
{
    rdata[0] = 0;
    rdata[1] = 0;
    rdata[2] = 0;
    rdata[3] = 0;
    rdata[4] = (UBYTE)((port >> 8) & 0xFF);
    rdata[5] = (UBYTE)(port & 0xFF);
    memcpy(rdata + 6, targetWire, (size_t)targetLen);
    return 6 + targetLen;
}

/* Sends a service's records. As a goodbye their time to live is zero, which
 * withdraws them from every cache on the link (RFC 6762 section 10.1). */
static void
mdnsRegSendService(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs, BOOL goodbye)
{
    ULONG ttl = goodbye ? 0 : MDNS_TTL_OTHER;
    ULONG hostttl = goodbye ? 0 : MDNS_TTL_HOST;
    UBYTE *pkt;
    struct MDNSHeader *hdr;
    LONG pos;
    UWORD ancount;
    char instfqdn[256];
    char typefqdn[256];
    char hostfqdn[256];
    char hostwire[256];
    LONG hostwirelen;
    UBYTE srvdata[512];
    LONG srvlen;
    UBYTE ptrtarget[256];
    LONG ptrlen;
    UBYTE txtdata[MDNS_MAX_TXT + 1];
    LONG txtlen;
    LONG txtsl;
    UBYTE addrdata[4];

    if (tok == NULL || rs == NULL) {
        return;
    }

    pkt = tok->et_TxBuf;
    if (pkt == NULL) {
        return;
    }

    mdnsRegBuildFqdn(instfqdn, (LONG)sizeof(instfqdn), rs->rs_Instance,
        rs->rs_Type, rs->rs_Domain);
    mdnsRegBuildTypeFqdn(typefqdn, (LONG)sizeof(typefqdn), rs->rs_Type,
        rs->rs_Domain);
    mdnsRegBuildHostFqdn(hostfqdn, (LONG)sizeof(hostfqdn), rs->rs_HostTarget,
        rs->rs_Domain);

    if (mdnsNameToWire(hostfqdn, hostwire, (LONG)sizeof(hostwire),
            &hostwirelen) != 0) {
        return;
    }

    hdr = (struct MDNSHeader *)pkt;
    mdnsInitHeader(hdr, DNS_FLAG_QR | DNS_FLAG_AA, 0);
    pos = (LONG)sizeof(struct MDNSHeader);
    ancount = 0;

    if (mdnsNameToWire(instfqdn, ptrtarget, (LONG)sizeof(ptrtarget), &ptrlen) != 0) {
        return;
    }
    /* A PTR record is shared by every instance of the type, so it must not
     * carry the cache-flush bit (RFC 6762 section 10.2). */
    mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, typefqdn,
        DNS_TYPE_PTR, DNS_CLASS_IN, ttl, ptrtarget,
        (UWORD)ptrlen);

    srvlen = mdnsRegAppendSrv(srvdata, hostwire, hostwirelen, rs->rs_Port);
    mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, instfqdn,
        DNS_TYPE_SRV, DNS_CLASS_IN | 0x8000, hostttl, srvdata,
        (UWORD)srvlen);

    /*
     * The client's text is "key=value" pairs separated by spaces; DNS-SD
     * wants each pair as a string of its own (RFC 6763 section 6). An
     * instance always has a TXT record: one empty string when it has nothing
     * to say. A value cannot contain a space.
     */
    txtlen = 0;
    for (txtsl = 0; rs->rs_TXT[txtsl] != '\0'; ) {
        LONG start, len;

        while (rs->rs_TXT[txtsl] == ' ') {
            txtsl++;
        }
        start = txtsl;
        while (rs->rs_TXT[txtsl] != '\0' && rs->rs_TXT[txtsl] != ' ') {
            txtsl++;
        }
        len = txtsl - start;
        if (len > 255) {
            len = 255;
        }
        if (len > 0 && txtlen + 1 + len <= (LONG)sizeof(txtdata)) {
            txtdata[txtlen++] = (UBYTE)len;
            memcpy(txtdata + txtlen, rs->rs_TXT + start, (size_t)len);
            txtlen += len;
        }
    }
    if (txtlen == 0) {
        txtdata[txtlen++] = 0;
    }
    mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, instfqdn,
        DNS_TYPE_TXT, DNS_CLASS_IN | 0x8000, ttl, txtdata, (UWORD)txtlen);

    /*
     * Advertise an A record mapping the host FQDN (the SRV target) to our IP,
     * so a resolver that receives this announcement can finish the lookup
     * without a separate query.  rs_HostAddr is the host's address
     * arranged as 0xAABBCCDD == A.B.C.D; emit it big-endian to match the wire
     * order (and our own A-record decoder in mdns_browse.c).  Skip when the
     * address is unknown (0).
     */
    if (rs->rs_HostAddr != 0) {
        addrdata[0] = (UBYTE)((rs->rs_HostAddr >> 24) & 0xFF);
        addrdata[1] = (UBYTE)((rs->rs_HostAddr >> 16) & 0xFF);
        addrdata[2] = (UBYTE)((rs->rs_HostAddr >> 8) & 0xFF);
        addrdata[3] = (UBYTE)(rs->rs_HostAddr & 0xFF);
        mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, hostfqdn,
            DNS_TYPE_A, DNS_CLASS_IN | 0x8000, hostttl, addrdata,
            (UWORD)4);
    }

    hdr->flags = htons((unsigned short)(DNS_FLAG_QR | DNS_FLAG_AA));
    hdr->ancount = htons(ancount);
    mdnsNetSendMulticast(tok, pkt, pos);
}

void
mdnsRegAnnounceService(struct MDNSEngineToken *tok, struct MDNSRegisteredService *rs)
{
    if (rs->rs_State == MDNS_RS_ANNOUNCED) {
        mdnsRegSendService(tok, rs, FALSE);
    }
}

/* Before the socket goes away: nobody should keep what this host offered. */
void
mdnsRegGoodbyeAll(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    if (tok == NULL || tok->et_Socket < 0) {
        return;
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State == MDNS_RS_ANNOUNCED) {
            mdnsRegSendService(tok, rs, TRUE);
        }
    }
}

void
mdnsRegAnnounceAll(struct MDNSEngineToken *tok)
{
    struct Node *n;

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        mdnsRegAnnounceService(tok, (struct MDNSRegisteredService *)n);
    }
}

/*
 * Asks whether anyone already has the instance's name, with the SRV record
 * this host would publish in the authority section (RFC 6762 section 8.1).
 */
static void
mdnsRegSendProbe(struct MDNSEngineToken *tok, struct MDNSRegisteredService *rs)
{
    UBYTE *pkt = tok->et_TxBuf;
    struct MDNSHeader *hdr = (struct MDNSHeader *)pkt;
    char instfqdn[256];
    char hostwire[256];
    UBYTE srvdata[512];
    LONG hostwirelen, srvlen, pos;
    UWORD nscount = 0;

    mdnsRegBuildFqdn(instfqdn, (LONG)sizeof(instfqdn), rs->rs_Instance,
        rs->rs_Type, rs->rs_Domain);
    if (mdnsNameToWire(rs->rs_HostTarget, hostwire, (LONG)sizeof(hostwire),
            &hostwirelen) != 0 ||
        mdnsBuildQuery(pkt, (LONG)MDNS_MAX_PACKET, 0, instfqdn, DNS_TYPE_ANY,
            (UWORD)(DNS_CLASS_IN | (rs->rs_Step == 0 ? DNS_QU_BIT : 0)),
            &pos) != 0) {
        return;
    }
    srvlen = mdnsRegAppendSrv(srvdata, hostwire, hostwirelen, rs->rs_Port);
    if (mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &nscount, instfqdn,
            DNS_TYPE_SRV, DNS_CLASS_IN, MDNS_TTL_HOST, srvdata,
            (UWORD)srvlen) != 0) {
        return;
    }
    hdr->nscount = htons(nscount);
    mdnsNetSendMulticast(tok, pkt, pos);
}

/* "Name" becomes "Name (2)", "Name (2)" becomes "Name (3)". */
static void
mdnsRegRename(struct MDNSRegisteredService *rs)
{
    char *name = rs->rs_Instance;
    LONG len = (LONG)strlen(name);
    LONG open, i;
    ULONG number = 1;

    open = len;
    if (len >= 4 && name[len - 1] == ')') {
        for (i = len - 2; i > 0 && name[i] >= '0' && name[i] <= '9'; i--)
            ;
        if (i < len - 2 && i > 0 && name[i] == '(' && name[i - 1] == ' ') {
            ULONG value = 0;
            LONG d;

            for (d = i + 1; d < len - 1; d++) {
                value = value * 10 + (ULONG)(name[d] - '0');
            }
            number = value;
            open = i - 1;
        }
    }
    number++;
    /* Room for " (4294967295)". */
    if (open > MDNS_MAX_NAME - 13) {
        open = MDNS_MAX_NAME - 13;
    }
    {
        char digits[12];
        LONG n = 0;

        do {
            digits[n++] = (char)('0' + number % 10);
            number /= 10;
        } while (number != 0);
        name[open++] = ' ';
        name[open++] = '(';
        while (n > 0) {
            name[open++] = digits[--n];
        }
        name[open++] = ')';
        name[open] = '\0';
    }
}

/*
 * A record in a response is owned by "name". If a service still being probed
 * wants that name, another host has it: the service takes the next name and
 * starts again. A name already announced is not defended yet.
 */
void
mdnsRegHandleAnswerName(struct MDNSEngineToken *tok, const char *name,
    ULONG srcaddr)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;
    char instfqdn[256];

    if (tok == NULL || name == NULL || srcaddr == tok->et_HostAddr) {
        return;
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State != MDNS_RS_PROBING) {
            continue;
        }
        mdnsRegBuildFqdn(instfqdn, (LONG)sizeof(instfqdn), rs->rs_Instance,
            rs->rs_Type, rs->rs_Domain);
        if (Stricmp(instfqdn, name) == 0) {
            mdnsRegRename(rs);
            rs->rs_Step = 0;
            rs->rs_NextTick = mdnsEngineTicks() + MDNS_PROBE_TICKS;
        }
    }
}

/*
 * Moves each service along: three probes, then two announcements. Returns
 * TRUE while a service is being probed, so that the caller comes back in a
 * quarter of a second.
 */
BOOL
mdnsRegTick(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;
    ULONG now = mdnsEngineTicks();
    BOOL probing = FALSE;

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State == MDNS_RS_PROBING) {
            probing = TRUE;
            if ((LONG)(now - rs->rs_NextTick) < 0) {
                continue;
            }
            if (rs->rs_Step < MDNS_PROBE_COUNT) {
                mdnsRegSendProbe(tok, rs);
                rs->rs_Step++;
                rs->rs_NextTick = now + MDNS_PROBE_TICKS;
                continue;
            }
            /* Nobody objected: the name is this host's. */
            rs->rs_State = MDNS_RS_ANNOUNCED;
            rs->rs_Step = 1;
            rs->rs_NextTick = now + 50;
            mdnsRegSendService(tok, rs, FALSE);
            mdnsBrowseAnnounceLocal(tok, rs);
        } else if (rs->rs_Step < MDNS_ANNOUNCE_COUNT &&
                   (LONG)(now - rs->rs_NextTick) >= 0) {
            rs->rs_Step++;
            mdnsRegSendService(tok, rs, FALSE);
        }
    }
    return probing;
}

/* Answers "which service types are there" with one PTR record per type. */
static void
mdnsRegSendTypes(struct MDNSEngineToken *tok)
{
    UBYTE *pkt = tok->et_TxBuf;
    struct MDNSHeader *hdr = (struct MDNSHeader *)pkt;
    struct MDNSRegisteredService *rs, *earlier;
    struct Node *n, *m;
    char typefqdn[256];
    UBYTE target[256];
    LONG pos = (LONG)sizeof(struct MDNSHeader), targetlen;
    UWORD ancount = 0;

    mdnsInitHeader(hdr, DNS_FLAG_QR | DNS_FLAG_AA, 0);
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State != MDNS_RS_ANNOUNCED) {
            continue;
        }
        /* once per type */
        for (m = tok->et_Services.lh_Head; m != n; m = m->ln_Succ) {
            earlier = (struct MDNSRegisteredService *)m;
            if (earlier->rs_State == MDNS_RS_ANNOUNCED &&
                Stricmp(earlier->rs_Type, rs->rs_Type) == 0) {
                break;
            }
        }
        if (m != n) {
            continue;
        }
        mdnsRegBuildTypeFqdn(typefqdn, (LONG)sizeof(typefqdn), rs->rs_Type,
            rs->rs_Domain);
        if (mdnsNameToWire(typefqdn, target, (LONG)sizeof(target),
                &targetlen) == 0) {
            mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount,
                MDNS_TYPE_SERVICES_DNS_SD "." MDNS_DOMAIN_LOCAL, DNS_TYPE_PTR,
                DNS_CLASS_IN, MDNS_TTL_OTHER, target, (UWORD)targetlen);
        }
    }
    if (ancount != 0) {
        hdr->ancount = htons(ancount);
        mdnsNetSendMulticast(tok, pkt, pos);
    }
}

/*
 * One question of a received query. "known" holds the PTR targets the asker
 * says it already has. A service answers at most once for one query packet,
 * always with all of its records.
 */
void
mdnsRegHandleQuery(struct MDNSEngineToken *tok, struct MDNSQuestion *q,
    char (*known)[256], LONG knownCount)
{
    char fqdn[256];
    struct MDNSRegisteredService *rs;
    struct Node *n;
    BOOL any, answer;
    LONG k;

    if (tok == NULL || q == NULL) {
        return;
    }
    any = q->qtype == DNS_TYPE_ANY;

    if ((any || q->qtype == DNS_TYPE_PTR) &&
        Stricmp(q->qname, MDNS_TYPE_SERVICES_DNS_SD "." MDNS_DOMAIN_LOCAL) == 0) {
        mdnsRegSendTypes(tok);
        return;
    }

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State != MDNS_RS_ANNOUNCED ||
            rs->rs_Answered == tok->et_PacketSerial) {
            continue;
        }
        answer = FALSE;

        mdnsRegBuildFqdn(fqdn, (LONG)sizeof(fqdn), rs->rs_Instance,
            rs->rs_Type, rs->rs_Domain);
        if (Stricmp(q->qname, fqdn) == 0) {
            answer = any || q->qtype == DNS_TYPE_SRV ||
                q->qtype == DNS_TYPE_TXT;
        } else if (Stricmp(q->qname, rs->rs_HostTarget) == 0) {
            answer = any || q->qtype == DNS_TYPE_A;
        } else if (any || q->qtype == DNS_TYPE_PTR) {
            char typefqdn[256];

            mdnsRegBuildTypeFqdn(typefqdn, (LONG)sizeof(typefqdn),
                rs->rs_Type, rs->rs_Domain);
            if (Stricmp(q->qname, typefqdn) == 0) {
                answer = TRUE;
                for (k = 0; k < knownCount; k++) {
                    if (Stricmp(known[k], fqdn) == 0) {
                        answer = FALSE;
                        break;
                    }
                }
            }
        }
        if (answer) {
            rs->rs_Answered = tok->et_PacketSerial;
            mdnsRegSendService(tok, rs, FALSE);
        }
    }
}

LONG
MDNSEngineRegister(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;
    APTR ref;

    if (tok == NULL || msg == NULL) {
        return MDNSERR_BADPARAM;
    }

    if (msg->bim_Flags & BONAMI_REGF_NIPC) {
        char *dot;

        /* Named after the host; the task's earlier publication goes. */
        MDNSEngineUnpublishNIPC(tok, msg->bim_Task);
        strncpy(msg->bim_Name, tok->et_HostName, MDNS_MAX_NAME);
        msg->bim_Name[MDNS_MAX_NAME] = '\0';
        if ((dot = strchr(msg->bim_Name, '.')) != NULL) {
            *dot = '\0';
        }
        if (msg->bim_Name[0] == '\0') {
            return MDNSERR_NETWORK;
        }
    }

    /* One instance name per type on this host. */
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (Stricmp(rs->rs_Instance, msg->bim_Name) == 0 &&
            Stricmp(rs->rs_Type, msg->bim_ServiceType) == 0) {
            return MDNSERR_DUPLICATE;
        }
    }

    rs = (struct MDNSRegisteredService *)AllocMem(sizeof(*rs), MEMF_CLEAR);
    if (rs == NULL) {
        return MDNSERR_NOMEM;
    }

    strcpy(rs->rs_Instance, msg->bim_Name);
    strcpy(rs->rs_Type, msg->bim_ServiceType);
    strcpy(rs->rs_Domain, msg->bim_Domain);
    mdnsRegBuildHostFqdn(rs->rs_HostTarget, (LONG)sizeof(rs->rs_HostTarget),
        tok->et_HostName, MDNS_DOMAIN_LOCAL);
    strcpy(rs->rs_TXT, msg->bim_TXT);
    rs->rs_Port = msg->bim_Port;
    rs->rs_HostAddr = tok->et_HostAddr;
    rs->rs_Task = msg->bim_Task;
    rs->rs_NIPC = (msg->bim_Flags & BONAMI_REGF_NIPC) ? TRUE : FALSE;
    if (msg->bim_HostName[0] != '\0' && msg->bim_HostAddr != 0) {
        /* Announced on behalf of a host that does not speak mDNS itself. */
        rs->rs_Proxy = TRUE;
        mdnsRegBuildHostFqdn(rs->rs_HostTarget,
            (LONG)sizeof(rs->rs_HostTarget), msg->bim_HostName,
            MDNS_DOMAIN_LOCAL);
        rs->rs_HostAddr = msg->bim_HostAddr;
    }

    ref = mdnsEngineNextHandle(tok);
    rs->rs_ClientRef = ref;

    ObtainSemaphore(&tok->et_ServiceLock);
    AddTail(&tok->et_Services, &rs->rs_Node);
    ReleaseSemaphore(&tok->et_ServiceLock);

    /*
     * The name is probed first; mdnsRegTick() announces it, on the link and
     * to browses on this host, about a second from now. If another host has
     * the name, the service is announced under the next free one.
     */
    rs->rs_State = MDNS_RS_PROBING;
    rs->rs_Step = 0;
    rs->rs_NextTick = mdnsEngineTicks();
    msg->bim_Handle = ref;
    return MDNS_OK;
}

void
MDNSEngineDeregister(struct MDNSEngineToken *tok, APTR ref)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    if (tok == NULL || ref == NULL) {
        return;
    }

    rs = NULL;
    ObtainSemaphore(&tok->et_ServiceLock);
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_ClientRef == ref) {
            Remove(n);
            break;
        }
        rs = NULL;
    }
    ReleaseSemaphore(&tok->et_ServiceLock);

    /*
     * Notify local browsers and drop the cache entry outside the service lock
     * (mdnsBrowseRemoveLocal takes the session lock), then free the service.
     */
    if (rs != NULL) {
        if (rs->rs_State == MDNS_RS_ANNOUNCED) {
            mdnsRegSendService(tok, rs, TRUE);
            mdnsBrowseRemoveLocal(tok, rs);
        }
        FreeMem(rs, sizeof(*rs));
    }
}

/* Withdraws every service a client registered. */
void
MDNSEngineReleaseServices(struct MDNSEngineToken *tok, struct Task *task)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;
    struct Node *next;

    if (tok == NULL || task == NULL) {
        return;
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_Task == task) {
            MDNSEngineDeregister(tok, rs->rs_ClientRef);
        }
    }
}

/*
 * The socket was opened again, perhaps on another address or under another
 * host name: every registration takes the host's current ones and is
 * announced again.
 */
void
mdnsRegHostChanged(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    if (tok == NULL) {
        return;
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (!rs->rs_Proxy) {
            mdnsRegBuildHostFqdn(rs->rs_HostTarget,
                (LONG)sizeof(rs->rs_HostTarget), tok->et_HostName,
                MDNS_DOMAIN_LOCAL);
            rs->rs_HostAddr = tok->et_HostAddr;
        }
        if (rs->rs_State == MDNS_RS_ANNOUNCED) {
            mdnsBrowseAnnounceLocal(tok, rs);
        }
    }
    mdnsRegAnnounceAll(tok);
}

void
MDNSEngineUnpublishNIPC(struct MDNSEngineToken *tok, struct Task *task)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_NIPC && rs->rs_Task == task) {
            MDNSEngineDeregister(tok, rs->rs_ClientRef);
            return;
        }
    }
}
