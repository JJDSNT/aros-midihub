/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_net.c - UDP 5353 / multicast (Roadshow bsdsocket)
 */

#include <exec/types.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_bases.h"
#include "private/bonami_debug.h"
#include "private/bonami_limits.h"

LONG
mdnsNetOpen(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase;
    struct sockaddr_in sin;
    struct ip_mreq mreq;
    int on;
    int sock;
    LONG err;

    if (tok == NULL || tok->et_SocketBase == NULL) {
        return MDNSERR_BADPARAM;
    }

    SocketBase = tok->et_SocketBase;

    if (tok->et_Socket >= 0) {
        return MDNS_OK;
    }

    baDbgPut("bonami: mdnsNetOpen enter\n");
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        tok->et_StartStage = MDNS_STAGE_SOCKET;
        tok->et_StartErrno = (LONG)Errno();
        baDbgPut("bonami: mdnsNetOpen socket failed\n");
        return MDNSERR_NETWORK;
    }

    /*
     * mDNS responders share UDP 5353, so both SO_REUSEADDR and SO_REUSEPORT
     * must be set before bind() or a second binder (another responder, or our
     * own re-open) fails with EADDRINUSE.  SO_REUSEPORT is only honoured when
     * every binder sets it, so a socket leaked by a crashed task (bound
     * without it) still blocks the port until the stack is restarted/rebooted.
     */
    on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
#ifdef SO_REUSEPORT
    on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (char *)&on, sizeof(on));
#endif

    /*
     * et_UdpPort is the *bind* (source) port.  Default 0 means "ephemeral":
     * the stack picks a free port so we never collide with the host's mDNS
     * responder on 5353, and our queries become legacy-unicast queries that
     * are answered directly to this port.  A non-zero value (env override to
     * 5353) selects full multicast/server mode.
     */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)tok->et_UdpPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    err = bind(sock, (struct sockaddr *)&sin, sizeof(sin));
    if (err < 0) {
        tok->et_StartStage = MDNS_STAGE_BIND;
        tok->et_StartErrno = (LONG)Errno();
        baDbgPut("bonami: mdnsNetOpen bind failed\n");
        CloseSocket(sock);
        return MDNSERR_NETWORK;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));

    on = 255;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&on, sizeof(on));

    /*
     * Enable multicast loopback so the engine also receives its own outgoing
     * multicast.  This is primarily a diagnostic aid: if et_RxPackets climbs
     * while nothing external answers, the socket receive path works and any
     * missing external replies point at the host/emulator multicast bridge.
     */
#ifdef IP_MULTICAST_LOOP
    on = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&on, sizeof(on));
#endif

    tok->et_Socket = (LONG)sock;
    tok->et_HostAddr = (ULONG)gethostid();

    tok->et_HostName[0] = '\0';
    if (gethostname(tok->et_HostName, MDNS_MAX_HOST) != 0) {
        strcpy(tok->et_HostName, "amiga");
    } else {
        tok->et_HostName[MDNS_MAX_HOST] = '\0';
    }

    baDbgPutLong("bonami: mdnsNetOpen ok sock=%ld\n", (LONG)sock);
    return MDNS_OK;
}

void
mdnsNetClose(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase;

    if (tok == NULL || tok->et_Socket < 0 || tok->et_SocketBase == NULL) {
        return;
    }
    SocketBase = tok->et_SocketBase;
    CloseSocket((int)tok->et_Socket);
    tok->et_Socket = -1;
}

LONG
mdnsNetSend(struct MDNSEngineToken *tok, const UBYTE *pkt, LONG pktlen,
    ULONG destaddr, UWORD destport)
{
    struct Library *SocketBase;
    struct sockaddr_in sin;
    int rc;

    if (tok == NULL || tok->et_Socket < 0 || tok->et_SocketBase == NULL ||
        pkt == NULL || pktlen <= 0) {
        return MDNSERR_BADPARAM;
    }

    SocketBase = tok->et_SocketBase;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)destport);
    sin.sin_addr.s_addr = htonl(destaddr);

    rc = sendto((int)tok->et_Socket, (char *)pkt, (int)pktlen, 0,
        (struct sockaddr *)&sin, sizeof(sin));
    if (rc < 0) {
        return MDNSERR_NETWORK;
    }
    tok->et_TxPackets++;
    return MDNS_OK;
}

LONG
mdnsNetSendMulticast(struct MDNSEngineToken *tok, const UBYTE *pkt, LONG pktlen)
{
    struct Library *SocketBase;
    ULONG mcast;

    if (tok == NULL || tok->et_SocketBase == NULL) {
        return MDNSERR_BADPARAM;
    }

    SocketBase = tok->et_SocketBase;
    mcast = (ULONG)inet_addr(MDNS_MCAST_ADDR);
    /*
     * The destination is always the well-known mDNS group port (5353); only
     * our own source/bind port is ephemeral.  Sending queries to 5353 from a
     * non-5353 source is what triggers legacy-unicast responses back to us.
     */
    return mdnsNetSend(tok, pkt, pktlen, mcast, (UWORD)MDNS_DNS_PORT);
}
