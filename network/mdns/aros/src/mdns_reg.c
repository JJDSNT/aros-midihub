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
mdnsRegSendServiceOn(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs, BOOL goodbye, LONG ifindex)
{
    /* The address record names the interface it leaves on, unless the
     * service stands for another host. */
    ULONG addr = rs->rs_Proxy ? rs->rs_HostAddr : tok->et_Ifs[ifindex].mi_Addr;
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
    if (addr != 0) {
        addrdata[0] = (UBYTE)((addr >> 24) & 0xFF);
        addrdata[1] = (UBYTE)((addr >> 16) & 0xFF);
        addrdata[2] = (UBYTE)((addr >> 8) & 0xFF);
        addrdata[3] = (UBYTE)(addr & 0xFF);
        mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, hostfqdn,
            DNS_TYPE_A, DNS_CLASS_IN | 0x8000, hostttl, addrdata,
            (UWORD)4);
    }

    hdr->flags = htons((unsigned short)(DNS_FLAG_QR | DNS_FLAG_AA));
    hdr->ancount = htons(ancount);
    mdnsNetSendOn(tok, ifindex, pkt, pos);
}

/* To the interface a question came in on, or else to every one. */
static void
mdnsRegSendService(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs, BOOL goodbye)
{
    LONG i;

    for (i = 0; i < tok->et_IfCount; i++) {
        if (tok->et_SendIf < 0 || i == tok->et_SendIf) {
            mdnsRegSendServiceOn(tok, rs, goodbye, i);
        }
    }
}

void
mdnsRegAnnounceService(struct MDNSEngineToken *tok, struct MDNSRegisteredService *rs)
{
    if (rs->rs_State == MDNS_RS_ANNOUNCED) {
        mdnsRegSendService(tok, rs, FALSE);
    }
}

static void host_send_addr(struct MDNSEngineToken *tok, BOOL goodbye);

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
    if (tok->et_HostState == MDNS_RS_ANNOUNCED) {
        host_send_addr(tok, TRUE);
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
 * A record in a response from another host is owned by "name". If one of
 * this host's services has that name, the name is contested: the service
 * takes the next name and is probed from the start. For a service that was
 * already announced only its own kinds of record count, and browses on this
 * host are told that the old name is gone. No goodbye is sent for it: the
 * records under that name now are the other host's.
 */
void
mdnsRegHandleAnswerName(struct MDNSEngineToken *tok, const char *name,
    UWORD type, ULONG srcaddr)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;
    char instfqdn[256];

    if (tok == NULL || name == NULL || mdnsNetIsOwn(tok, srcaddr)) {
        return;
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State == MDNS_RS_ANNOUNCED && type != DNS_TYPE_SRV &&
            type != DNS_TYPE_TXT) {
            continue;
        }
        mdnsRegBuildFqdn(instfqdn, (LONG)sizeof(instfqdn), rs->rs_Instance,
            rs->rs_Type, rs->rs_Domain);
        if (Stricmp(instfqdn, name) != 0) {
            continue;
        }
        if (rs->rs_State == MDNS_RS_ANNOUNCED) {
            mdnsBrowseRemoveLocal(tok, rs);
        }
        mdnsRegRename(rs);
        rs->rs_State = MDNS_RS_PROBING;
        rs->rs_Step = 0;
        rs->rs_NextTick = mdnsEngineTicks() + MDNS_PROBE_TICKS;
    }
}

/*
 * The host's own name. It is a record of its own, apart from any service:
 * probed when the network comes up, announced, answered for, defended, and
 * changed to "name-2", "name-3" when another host has it (RFC 6762 sections
 * 8 and 9). Each interface gets the address record of that interface.
 */
static void
host_fqdn(struct MDNSEngineToken *tok, char *buf, LONG buflen)
{
    mdnsRegBuildHostFqdn(buf, buflen, tok->et_HostName, MDNS_DOMAIN_LOCAL);
}

static void
host_addr_data(ULONG addr, UBYTE *data)
{
    data[0] = (UBYTE)((addr >> 24) & 0xFF);
    data[1] = (UBYTE)((addr >> 16) & 0xFF);
    data[2] = (UBYTE)((addr >> 8) & 0xFF);
    data[3] = (UBYTE)(addr & 0xFF);
}

/* Asks every link whether anyone has the name, with the address record this
 * host would publish there in the authority section. */
static void
host_send_probe(struct MDNSEngineToken *tok)
{
    UBYTE *pkt = tok->et_TxBuf;
    struct MDNSHeader *hdr = (struct MDNSHeader *)pkt;
    char fqdn[256];
    UBYTE addrdata[4];
    LONG pos, i;
    UWORD nscount;

    host_fqdn(tok, fqdn, (LONG)sizeof(fqdn));
    for (i = 0; i < tok->et_IfCount; i++) {
        nscount = 0;
        if (mdnsBuildQuery(pkt, (LONG)MDNS_MAX_PACKET, 0, fqdn, DNS_TYPE_ANY,
                (UWORD)(DNS_CLASS_IN |
                    (tok->et_HostStep == 0 ? DNS_QU_BIT : 0)), &pos) != 0) {
            return;
        }
        host_addr_data(tok->et_Ifs[i].mi_Addr, addrdata);
        if (mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &nscount, fqdn,
                DNS_TYPE_A, DNS_CLASS_IN, MDNS_TTL_HOST, addrdata, 4) != 0) {
            return;
        }
        hdr->nscount = htons(nscount);
        mdnsNetSendOn(tok, i, pkt, pos);
    }
}

/* The host's address record, to the interface a question came in on or to
 * every one; as a goodbye with no time to live. */
static void
host_send_addr(struct MDNSEngineToken *tok, BOOL goodbye)
{
    UBYTE *pkt = tok->et_TxBuf;
    struct MDNSHeader *hdr = (struct MDNSHeader *)pkt;
    char fqdn[256];
    UBYTE addrdata[4];
    LONG pos, i;
    UWORD ancount;

    host_fqdn(tok, fqdn, (LONG)sizeof(fqdn));
    for (i = 0; i < tok->et_IfCount; i++) {
        if (tok->et_SendIf >= 0 && i != tok->et_SendIf) {
            continue;
        }
        mdnsInitHeader(hdr, DNS_FLAG_QR | DNS_FLAG_AA, 0);
        pos = (LONG)sizeof(struct MDNSHeader);
        ancount = 0;
        host_addr_data(tok->et_Ifs[i].mi_Addr, addrdata);
        if (mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, fqdn,
                DNS_TYPE_A, DNS_CLASS_IN | 0x8000,
                goodbye ? 0 : MDNS_TTL_HOST, addrdata, 4) != 0) {
            continue;
        }
        hdr->ancount = htons(ancount);
        mdnsNetSendOn(tok, i, pkt, pos);
    }
}

/* Services that stand for this host point at its name. */
static void
host_retarget_services(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (!rs->rs_Proxy) {
            mdnsRegBuildHostFqdn(rs->rs_HostTarget,
                (LONG)sizeof(rs->rs_HostTarget), tok->et_HostName,
                MDNS_DOMAIN_LOCAL);
            rs->rs_HostAddr = tok->et_HostAddr;
        }
    }
}

/* Starts asking for the name, from the first probe. */
static void
host_probe_from(struct MDNSEngineToken *tok, ULONG ticks)
{
    tok->et_HostState = MDNS_RS_PROBING;
    tok->et_HostStep = 0;
    tok->et_HostNextTick = mdnsEngineTicks() + ticks;
}

/* Another host has the name: take the next one and ask for that. */
static void
host_rename(struct MDNSEngineToken *tok)
{
    tok->et_HostSuffix = tok->et_HostSuffix == 0 ? 2 : tok->et_HostSuffix + 1;
    mdnsNetNameHost(tok);
    host_retarget_services(tok);
    host_probe_from(tok, MDNS_PROBE_TICKS);
}

/*
 * A record from another host. An address record under this host's name
 * whose address is not one of this host's says somebody else has the name,
 * whether this host is still asking for it or has announced it already.
 */
void
mdnsHostHandleRecord(struct MDNSEngineToken *tok, const struct MDNSRecord *r)
{
    char fqdn[256];
    ULONG addr;

    if (tok == NULL || r == NULL || tok->et_IfCount == 0 ||
        r->type != DNS_TYPE_A || r->rdlength != 4 || r->ttl == 0) {
        return;
    }
    host_fqdn(tok, fqdn, (LONG)sizeof(fqdn));
    if (Stricmp(fqdn, r->name) != 0) {
        return;
    }
    addr = ((ULONG)r->rdata[0] << 24) | ((ULONG)r->rdata[1] << 16) |
           ((ULONG)r->rdata[2] << 8) | (ULONG)r->rdata[3];
    if (mdnsNetIsOwn(tok, addr)) {
        return;
    }
    host_rename(tok);
}

/*
 * A record as it is compared when two hosts probe for one name: class
 * without the cache-flush bit, type, and the data with no name compressed
 * (RFC 6762 section 8.2). Returns the length of the data, or -1.
 */
static LONG
probe_rdata(const struct MDNSRecord *r, const UBYTE *pkt, LONG pktlen,
    UBYTE *out, LONG outmax)
{
    char target[256];
    LONG wirelen;

    if (r->type != DNS_TYPE_SRV) {
        if ((LONG)r->rdlength > outmax) {
            return -1;
        }
        memcpy(out, r->rdata, r->rdlength);
        return (LONG)r->rdlength;
    }
    /* priority, weight and port, then a name that may be compressed */
    if (r->rdlength < 7 || outmax < 6 ||
        mdnsWireToName(pkt, pktlen, r->rdoffset + 6, pkt, pktlen, target,
            (LONG)sizeof(target)) < 0 ||
        mdnsNameToWire(target, out + 6, outmax - 6, &wirelen) != 0) {
        return -1;
    }
    memcpy(out, r->rdata, 6);
    return 6 + wirelen;
}

/* <0 when (class, type, data) a sorts before b, 0 when equal, >0 after. */
static LONG
probe_compare(UWORD classA, UWORD typeA, const UBYTE *a, LONG lenA,
    UWORD classB, UWORD typeB, const UBYTE *b, LONG lenB)
{
    LONG n, d;

    classA &= 0x7fff;
    classB &= 0x7fff;
    if (classA != classB) {
        return (LONG)classA - (LONG)classB;
    }
    if (typeA != typeB) {
        return (LONG)typeA - (LONG)typeB;
    }
    n = lenA < lenB ? lenA : lenB;
    d = (LONG)memcmp(a, b, (size_t)n);
    if (d != 0) {
        return d;
    }
    return lenA - lenB;
}

/*
 * Another host is probing, and these are the records it proposes. For a
 * name this host is probing for too, the two sets of records are compared
 * and the host whose set is lexicographically later keeps going; the other
 * waits a second and probes again, by which time the winner has announced
 * and the usual conflict gives the loser another name (RFC 6762 section
 * 8.2). This host proposes one record for a service, its SRV, so the other
 * host's set is compared from its earliest record: an equal first record
 * and more behind it makes the other set the later one.
 */
void
mdnsRegHandleProbe(struct MDNSEngineToken *tok,
    const struct MDNSRecord *theirs, LONG count, const UBYTE *pkt,
    LONG pktlen)
{
    static UBYTE mine[6 + 256], other[512], first[512];
    struct MDNSRegisteredService *rs;
    struct Node *n;
    char instfqdn[256];
    char hostwire[256];
    LONG hostwirelen, minelen, otherlen, firstlen, i, matches, d;
    UWORD firstclass = 0, firsttype = 0;

    if (tok == NULL || theirs == NULL || count <= 0) {
        return;
    }
    /*
     * The host's own name, with the address of the interface the probe came
     * in on. An equal record is this same host seen from somewhere else, or
     * a host with this host's address, which is not for mDNS to sort out.
     */
    if (tok->et_HostState == MDNS_RS_PROBING && tok->et_HostStep != 0 &&
        tok->et_IfCount > 0) {
        LONG index = tok->et_SendIf >= 0 ? tok->et_SendIf : 0;

        mdnsRegBuildHostFqdn(instfqdn, (LONG)sizeof(instfqdn),
            tok->et_HostName, MDNS_DOMAIN_LOCAL);
        host_addr_data(tok->et_Ifs[index].mi_Addr, mine);
        for (i = 0; i < count; i++) {
            if (Stricmp(theirs[i].name, instfqdn) == 0 &&
                theirs[i].type == DNS_TYPE_A && theirs[i].rdlength == 4 &&
                probe_compare(DNS_CLASS_IN, DNS_TYPE_A, mine, 4,
                    theirs[i].rclass, theirs[i].type, theirs[i].rdata,
                    4) < 0) {
                /* theirs is later: give way for a second, then ask again */
                host_probe_from(tok, 50);
                break;
            }
        }
    }
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State != MDNS_RS_PROBING || rs->rs_Step == 0) {
            continue;       /* not probing, or has not asked yet */
        }
        mdnsRegBuildFqdn(instfqdn, (LONG)sizeof(instfqdn), rs->rs_Instance,
            rs->rs_Type, rs->rs_Domain);
        /* the earliest of the other host's records for this name */
        matches = 0;
        firstlen = 0;
        for (i = 0; i < count; i++) {
            if (Stricmp(theirs[i].name, instfqdn) != 0) {
                continue;
            }
            otherlen = probe_rdata(&theirs[i], pkt, pktlen, other,
                (LONG)sizeof(other));
            if (otherlen < 0) {
                continue;
            }
            if (matches == 0 ||
                probe_compare(theirs[i].rclass, theirs[i].type, other,
                    otherlen, firstclass, firsttype, first, firstlen) < 0) {
                memcpy(first, other, (size_t)otherlen);
                firstlen = otherlen;
                firstclass = theirs[i].rclass;
                firsttype = theirs[i].type;
            }
            matches++;
        }
        if (matches == 0 ||
            mdnsNameToWire(rs->rs_HostTarget, hostwire,
                (LONG)sizeof(hostwire), &hostwirelen) != 0) {
            continue;
        }
        minelen = mdnsRegAppendSrv(mine, (UBYTE *)hostwire, hostwirelen,
            rs->rs_Port);
        d = probe_compare(DNS_CLASS_IN, DNS_TYPE_SRV, mine, minelen,
            firstclass, firsttype, first, firstlen);
        if (d < 0 || (d == 0 && matches > 1)) {
            /* theirs is later: give way for a second, then ask again */
            rs->rs_Step = 0;
            rs->rs_NextTick = mdnsEngineTicks() + 50;
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
    BOOL hostReady;

    /* The host's name first: a service's records point at it and carry its
     * address, so no service is announced before the name is this host's. */
    if (tok->et_IfCount > 0 && tok->et_HostState == MDNS_RS_PROBING) {
        probing = TRUE;
        if ((LONG)(now - tok->et_HostNextTick) >= 0) {
            if (tok->et_HostStep < MDNS_PROBE_COUNT) {
                host_send_probe(tok);
                tok->et_HostStep++;
                tok->et_HostNextTick = now + MDNS_PROBE_TICKS;
            } else {
                /* Nobody objected. Services announced under an earlier
                 * name or address are announced again with this one. */
                tok->et_HostState = MDNS_RS_ANNOUNCED;
                tok->et_HostStep = 1;
                tok->et_HostNextTick = now + 50;
                host_send_addr(tok, FALSE);
                for (n = tok->et_Services.lh_Head;
                    n != NULL && n->ln_Succ != NULL; n = n->ln_Succ) {
                    rs = (struct MDNSRegisteredService *)n;
                    if (rs->rs_State == MDNS_RS_ANNOUNCED) {
                        mdnsBrowseAnnounceLocal(tok, rs);
                    }
                }
                mdnsRegAnnounceAll(tok);
            }
        }
    } else if (tok->et_IfCount > 0 &&
               tok->et_HostStep < MDNS_ANNOUNCE_COUNT &&
               (LONG)(now - tok->et_HostNextTick) >= 0) {
        tok->et_HostStep++;
        host_send_addr(tok, FALSE);
    }
    hostReady = tok->et_HostState == MDNS_RS_ANNOUNCED;

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
            if (!hostReady && !rs->rs_Proxy) {
                continue;       /* waits for the host's name */
            }
            /* Nobody objected: the name is this host's. */
            rs->rs_State = MDNS_RS_ANNOUNCED;
            rs->rs_Step = 1;
            rs->rs_NextTick = now + 50;
            mdnsRegSendService(tok, rs, FALSE);
            mdnsBrowseAnnounceLocal(tok, rs);
        } else if (rs->rs_Step < MDNS_ANNOUNCE_COUNT &&
                   (LONG)(now - rs->rs_NextTick) >= 0 &&
                   (hostReady || rs->rs_Proxy)) {
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

    /* The host's own name, whether or not it offers a service. */
    if ((any || q->qtype == DNS_TYPE_A) && tok->et_IfCount > 0 &&
        tok->et_HostState == MDNS_RS_ANNOUNCED) {
        mdnsRegBuildHostFqdn(fqdn, (LONG)sizeof(fqdn), tok->et_HostName,
            MDNS_DOMAIN_LOCAL);
        if (Stricmp(q->qname, fqdn) == 0) {
            host_send_addr(tok, FALSE);
            return;
        }
    }

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_State != MDNS_RS_ANNOUNCED ||
            rs->rs_Answered == tok->et_PacketSerial) {
            continue;
        }
        /* a service of this host waits while the host's name is in doubt */
        if (!rs->rs_Proxy && tok->et_HostState != MDNS_RS_ANNOUNCED) {
            continue;
        }
        answer = FALSE;

        mdnsRegBuildFqdn(fqdn, (LONG)sizeof(fqdn), rs->rs_Instance,
            rs->rs_Type, rs->rs_Domain);
        if (Stricmp(q->qname, fqdn) == 0) {
            answer = any || q->qtype == DNS_TYPE_SRV ||
                q->qtype == DNS_TYPE_TXT;
        } else if (rs->rs_Proxy &&
                   Stricmp(q->qname, rs->rs_HostTarget) == 0) {
            /* the host a service stands in for; this host's own name was
             * answered above */
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

/* Changes a registered service's text or port and tells the link. Only
 * the client that registered it may. */
LONG
MDNSEngineUpdate(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        if (rs->rs_ClientRef != msg->bim_Handle) {
            continue;
        }
        if (rs->rs_Task != msg->bim_Task) {
            return MDNSERR_BADPARAM;
        }
        if (msg->bim_Flags & BONAMI_UPDF_TXT) {
            strcpy(rs->rs_TXT, msg->bim_TXT);
        }
        if (msg->bim_Port != 0) {
            rs->rs_Port = msg->bim_Port;
        }
        if (rs->rs_State == MDNS_RS_ANNOUNCED) {
            mdnsRegSendService(tok, rs, FALSE);
            mdnsBrowseAnnounceLocal(tok, rs);
        }
        return MDNS_OK;
    }
    return MDNSERR_NOTFOUND;
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
    (void)rs;
    (void)n;
    /* The name is asked for again on what is there now; mdnsRegTick()
     * announces it and the services once nobody has objected. */
    host_retarget_services(tok);
    host_probe_from(tok, 0);
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
