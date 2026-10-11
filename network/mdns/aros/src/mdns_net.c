/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * mdns_net.c - one UDP 5353 socket for each interface (bsdsocket.library)
 *
 * mDNS is a protocol of the link: what is heard on an interface is answered
 * there, and an address record carries the address of the interface it is
 * sent on. Each interface that is up, can multicast and has an IPv4 address
 * gets a socket bound to it with SO_BINDTODEVICE.
 */

#include <exec/types.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_limits.h"

struct found
{
    char    name[IFNAMSIZ + 1];
    ULONG   addr;               /* 0xAABBCCDD == A.B.C.D */
};

/* The interfaces mDNS can use now. Returns how many were written. */
static LONG
find_interfaces(struct Library *SocketBase, struct found *out, LONG max)
{
    char buf[2048];
    struct ifconf ifc;
    struct ifreq *ifr, *end, flags;
    struct sockaddr *sa;
    LONG count = 0;
    int sock;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return 0;
    }
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (IoctlSocket(sock, SIOCGIFCONF, (char *)&ifc) < 0) {
        CloseSocket(sock);
        return 0;
    }

    ifr = ifc.ifc_req;
    end = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];
    while (ifr < end && count < max) {
        sa = &ifr->ifr_addr;
        if (sa->sa_family == AF_INET) {
            ULONG addr = (ULONG)ntohl((unsigned long)
                ((struct sockaddr_in *)sa)->sin_addr.s_addr);

            memset(&flags, 0, sizeof(flags));
            strncpy(flags.ifr_name, ifr->ifr_name, IFNAMSIZ);
            if (addr != 0 &&
                IoctlSocket(sock, SIOCGIFFLAGS, (char *)&flags) == 0 &&
                (flags.ifr_flags & (IFF_UP | IFF_MULTICAST | IFF_LOOPBACK)) ==
                    (IFF_UP | IFF_MULTICAST)) {
                memset(out[count].name, 0, sizeof(out[count].name));
                strncpy(out[count].name, ifr->ifr_name, IFNAMSIZ);
                out[count].addr = addr;
                count++;
            }
        }
        /* an entry is the name and an address of at least sockaddr's size */
        if (sa->sa_len < sizeof(*sa)) {
            ifr = (struct ifreq *)((char *)sa + sizeof(*sa));
        } else {
            ifr = (struct ifreq *)((char *)sa + sa->sa_len);
        }
    }
    CloseSocket(sock);
    return count;
}

static LONG
open_socket(struct Library *SocketBase, const char *name, ULONG addr)
{
    struct sockaddr_in sin;
    struct ip_mreq mreq;
    int on;
    int sock;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* Every responder of the host shares the port, here one per interface. */
    on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
#ifdef SO_REUSEPORT
    on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (char *)&on, sizeof(on));
#endif
    /* Only what arrives on this interface; what is sent leaves there. */
    setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (char *)name,
        (int)strlen(name));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(MDNS_DNS_PORT);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        CloseSocket(sock);
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(addr);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq,
            sizeof(mreq)) < 0) {
        CloseSocket(sock);
        return -1;
    }
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
        (char *)&mreq.imr_interface, sizeof(mreq.imr_interface));
    {
        /*
         * BSD takes these two as one byte. No copy of what mdnsd sends comes
         * back to it: a copy sent just after the interface changed its
         * address would look like another host using this host's names.
         */
        unsigned char value;

        value = 255;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&value,
            sizeof(value));
        value = 0;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&value,
            sizeof(value));
    }
    return (LONG)sock;
}

static void
set_primary(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase = tok->et_SocketBase;

    tok->et_Socket = tok->et_IfCount > 0 ? tok->et_Ifs[0].mi_Socket : -1;
    tok->et_HostAddr = tok->et_IfCount > 0 ? tok->et_Ifs[0].mi_Addr : 0;

    tok->et_HostName[0] = '\0';
    if (gethostname(tok->et_HostName, MDNS_MAX_HOST) != 0 ||
        tok->et_HostName[0] == '\0') {
        strcpy(tok->et_HostName, "aros");
    }
    tok->et_HostName[MDNS_MAX_HOST] = '\0';
}

/*
 * Brings the sockets in line with the interfaces the stack has now: one that
 * went away or changed its address loses its socket, a new one gets one.
 * Returns TRUE if anything changed.
 */
BOOL
mdnsNetRescan(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase;
    struct found now[MDNS_IFS_MAX];
    BOOL changed = FALSE;
    LONG count, i, k;

    if (tok == NULL || tok->et_SocketBase == NULL) {
        return FALSE;
    }
    SocketBase = tok->et_SocketBase;
    count = find_interfaces(SocketBase, now, MDNS_IFS_MAX);

    for (i = 0; i < tok->et_IfCount; ) {
        for (k = 0; k < count; k++) {
            if (now[k].addr == tok->et_Ifs[i].mi_Addr &&
                strcmp(now[k].name, tok->et_Ifs[i].mi_Name) == 0) {
                break;
            }
        }
        if (k < count) {
            now[k].addr = 0;        /* kept */
            i++;
            continue;
        }
        CloseSocket((int)tok->et_Ifs[i].mi_Socket);
        tok->et_Ifs[i] = tok->et_Ifs[--tok->et_IfCount];
        changed = TRUE;
    }
    for (k = 0; k < count; k++) {
        LONG sock;

        if (now[k].addr == 0 || tok->et_IfCount >= MDNS_IFS_MAX) {
            continue;
        }
        sock = open_socket(SocketBase, now[k].name, now[k].addr);
        if (sock < 0) {
            continue;       /* tried again at the next look */
        }
        strcpy(tok->et_Ifs[tok->et_IfCount].mi_Name, now[k].name);
        tok->et_Ifs[tok->et_IfCount].mi_Addr = now[k].addr;
        tok->et_Ifs[tok->et_IfCount].mi_Socket = sock;
        tok->et_IfCount++;
        changed = TRUE;
    }
    if (changed) {
        set_primary(tok);
    }
    return changed;
}

LONG
mdnsNetOpen(struct MDNSEngineToken *tok)
{
    if (tok == NULL || tok->et_SocketBase == NULL) {
        return MDNSERR_BADPARAM;
    }
    tok->et_SendIf = -1;
    mdnsNetRescan(tok);
    return tok->et_IfCount > 0 ? MDNS_OK : MDNSERR_NETWORK;
}

void
mdnsNetClose(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase;

    if (tok == NULL || tok->et_SocketBase == NULL) {
        return;
    }
    SocketBase = tok->et_SocketBase;
    while (tok->et_IfCount > 0) {
        CloseSocket((int)tok->et_Ifs[--tok->et_IfCount].mi_Socket);
    }
    tok->et_Socket = -1;
    tok->et_HostAddr = 0;
}

/* Is this one of the host's own addresses? */
BOOL
mdnsNetIsOwn(struct MDNSEngineToken *tok, ULONG addr)
{
    LONG i;

    for (i = 0; i < tok->et_IfCount; i++) {
        if (tok->et_Ifs[i].mi_Addr == addr) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Sends to the mDNS group on one interface. */
LONG
mdnsNetSendOn(struct MDNSEngineToken *tok, LONG index, const UBYTE *pkt,
    LONG pktlen)
{
    struct Library *SocketBase;
    struct sockaddr_in sin;

    if (tok == NULL || tok->et_SocketBase == NULL || pkt == NULL ||
        pktlen <= 0 || index < 0 || index >= tok->et_IfCount) {
        return MDNSERR_BADPARAM;
    }
    SocketBase = tok->et_SocketBase;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(MDNS_DNS_PORT);
    sin.sin_addr.s_addr = inet_addr(MDNS_MCAST_ADDR);
    if (sendto((int)tok->et_Ifs[index].mi_Socket, (char *)pkt, (int)pktlen, 0,
            (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        return MDNSERR_NETWORK;
    }
    tok->et_TxPackets++;
    return MDNS_OK;
}

/*
 * Sends a packet that is the same on every link: to the interface a packet
 * being answered came in on, or else to all of them.
 */
LONG
mdnsNetSendMulticast(struct MDNSEngineToken *tok, const UBYTE *pkt, LONG pktlen)
{
    LONG rc = MDNSERR_NETWORK;
    LONG i;

    if (tok == NULL) {
        return MDNSERR_BADPARAM;
    }
    for (i = 0; i < tok->et_IfCount; i++) {
        if (tok->et_SendIf >= 0 && i != tok->et_SendIf) {
            continue;
        }
        if (mdnsNetSendOn(tok, i, pkt, pktlen) == MDNS_OK) {
            rc = MDNS_OK;
        }
    }
    return rc;
}
