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
 * resolvable.  Append the mDNS domain to make "amiga1.local" unless the host
 * name already carries a dotted domain.
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
    if (strchr(buf, '.') == NULL) {
        strncat(buf, ".", buflen - 1 - strlen(buf));
        strncat(buf, domain, buflen - 1 - strlen(buf));
    }
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

void
mdnsRegAnnounceService(struct MDNSEngineToken *tok, struct MDNSRegisteredService *rs)
{
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
    mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, typefqdn,
        DNS_TYPE_PTR, DNS_CLASS_IN | 0x8000, MDNS_DEFAULT_TTL, ptrtarget,
        (UWORD)ptrlen);

    srvlen = mdnsRegAppendSrv(srvdata, hostwire, hostwirelen, rs->rs_Port);
    mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, instfqdn,
        DNS_TYPE_SRV, DNS_CLASS_IN | 0x8000, MDNS_DEFAULT_TTL, srvdata,
        (UWORD)srvlen);

    txtlen = 0;
    if (rs->rs_TXT[0] != '\0') {
        /*
         * A single DNS TXT character-string is length-prefixed by one byte, so
         * it can hold at most 255 bytes.  Clamp before copying: an unclamped
         * strcpy of a 256-byte rs_TXT would both overrun txtdata[] by one byte
         * (heap/stack corruption) and truncate the length prefix to 0.
         */
        txtsl = (LONG)strlen(rs->rs_TXT);
        if (txtsl > 255) {
            txtsl = 255;
        }
        txtdata[0] = (UBYTE)txtsl;
        memcpy(txtdata + 1, rs->rs_TXT, (size_t)txtsl);
        txtlen = 1 + txtsl;
        mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, instfqdn,
            DNS_TYPE_TXT, DNS_CLASS_IN | 0x8000, MDNS_DEFAULT_TTL, txtdata,
            (UWORD)txtlen);
    }

    /*
     * Advertise an A record mapping the host FQDN (the SRV target) to our IP,
     * so a resolver that receives this announcement can finish the lookup
     * without a separate query.  et_HostAddr is the ULONG from gethostid()
     * arranged as 0xAABBCCDD == A.B.C.D; emit it big-endian to match the wire
     * order (and our own A-record decoder in mdns_browse.c).  Skip when the
     * address is unknown (0).
     */
    if (tok->et_HostAddr != 0) {
        addrdata[0] = (UBYTE)((tok->et_HostAddr >> 24) & 0xFF);
        addrdata[1] = (UBYTE)((tok->et_HostAddr >> 16) & 0xFF);
        addrdata[2] = (UBYTE)((tok->et_HostAddr >> 8) & 0xFF);
        addrdata[3] = (UBYTE)(tok->et_HostAddr & 0xFF);
        mdnsAppendRecord(pkt, (LONG)MDNS_MAX_PACKET, &pos, &ancount, hostfqdn,
            DNS_TYPE_A, DNS_CLASS_IN | 0x8000, MDNS_DEFAULT_TTL, addrdata,
            (UWORD)4);
    }

    hdr->flags = htons((unsigned short)(DNS_FLAG_QR | DNS_FLAG_AA));
    hdr->ancount = htons(ancount);
    mdnsNetSendMulticast(tok, pkt, pos);
}

void
mdnsRegAnnounceAll(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct Node *n;

    ObtainSemaphore(&tok->et_ServiceLock);
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        mdnsRegAnnounceService(tok, rs);
    }
    ReleaseSemaphore(&tok->et_ServiceLock);
}

void
mdnsRegHandleQuery(struct MDNSEngineToken *tok, struct MDNSQuestion *q)
{
    char typefqdn[256];
    struct MDNSRegisteredService *rs;
    struct Node *n;

    if (tok == NULL || q == NULL) {
        return;
    }

    ObtainSemaphore(&tok->et_ServiceLock);
    for (n = tok->et_Services.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = n->ln_Succ) {
        rs = (struct MDNSRegisteredService *)n;
        mdnsRegBuildTypeFqdn(typefqdn, (LONG)sizeof(typefqdn), rs->rs_Type,
            rs->rs_Domain);
        if (Stricmp(q->qname, typefqdn) == 0 && q->qtype == DNS_TYPE_PTR) {
            mdnsRegAnnounceService(tok, rs);
        }
    }
    ReleaseSemaphore(&tok->et_ServiceLock);
}

LONG
MDNSEngineRegister(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSRegisteredService *rs;
    APTR ref;

    if (tok == NULL || msg == NULL) {
        return MDNSERR_BADPARAM;
    }

    rs = (struct MDNSRegisteredService *)AllocMem(sizeof(*rs), MEMF_CLEAR);
    if (rs == NULL) {
        return MDNSERR_NOMEM;
    }

    strcpy(rs->rs_Instance, msg->bim_Name);
    strcpy(rs->rs_Type, msg->bim_ServiceType);
    strcpy(rs->rs_Domain, msg->bim_Domain);
    strcpy(rs->rs_HostTarget, tok->et_HostName);
    strcpy(rs->rs_TXT, msg->bim_TXT);
    rs->rs_Port = msg->bim_Port;
    rs->rs_HostAddr = tok->et_HostAddr;

    ref = mdnsEngineNextHandle(tok);
    rs->rs_ClientRef = ref;

    ObtainSemaphore(&tok->et_ServiceLock);
    AddTail(&tok->et_Services, &rs->rs_Node);
    ReleaseSemaphore(&tok->et_ServiceLock);

    /* Announce on the wire, and deliver to any local browsers + seed cache so a
     * local browse/resolve on this same machine sees it without needing the RX
     * path (which bsdsocket emulation may not provide). */
    mdnsRegAnnounceService(tok, rs);
    mdnsBrowseAnnounceLocal(tok, rs);
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
        mdnsBrowseRemoveLocal(tok, rs);
        FreeMem(rs, sizeof(*rs));
    }
}
