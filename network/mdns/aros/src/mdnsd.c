/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * mdnsd - the system's one mDNS/DNS-SD responder and browser.
 *
 * The AROSTCP service manager starts it from db/services.d/mdns and stops it
 * with the stack. bonami.library reaches it through the public message port
 * BONAMI_ENGINE_PORT; no client shares memory with it. The loop and the
 * engine's state come from Bonami's mdns.task.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <bonami/mdns.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"
#include "private/mdnsd_service.h"

/* Seconds between attempts to open the socket while the host has no usable
 * address yet. */
#define MDNSD_RETRY_SECONDS 3

static int engineErrno;

ULONG
mdnsEngineNow(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL +
        (ULONG)(ds.ds_Tick / 50);
}

APTR
mdnsEngineNextHandle(struct MDNSEngineToken *tok)
{
    APTR h;

    h = (APTR)(IPTR)tok->et_NextHandle;
    tok->et_NextHandle++;
    return h;
}

static void
free_list(struct List *list, ULONG size)
{
    struct Node *n;

    while ((n = RemHead(list)) != NULL) {
        FreeMem(n, size);
    }
}

static void
token_free(struct MDNSEngineToken *tok)
{
    if (tok == NULL) {
        return;
    }
    free_list(&tok->et_Services, sizeof(struct MDNSRegisteredService));
    free_list(&tok->et_BrowseSessions, sizeof(struct MDNSBrowseSession));
    free_list(&tok->et_InquirySessions, sizeof(struct MDNSInquirySession));
    free_list(&tok->et_Cache, sizeof(struct MDNSCacheEntry));
    free_list(&tok->et_Hosts, sizeof(struct MDNSHostEntry));
    if (tok->et_RxBuf != NULL) {
        FreeMem(tok->et_RxBuf, MDNS_MAX_PACKET);
    }
    if (tok->et_TxBuf != NULL) {
        FreeMem(tok->et_TxBuf, MDNS_MAX_PACKET);
    }
    FreeMem(tok, sizeof(*tok));
}

static struct MDNSEngineToken *
token_create(void)
{
    struct MDNSEngineToken *tok;

    tok = (struct MDNSEngineToken *)AllocMem(sizeof(*tok), MEMF_CLEAR);
    if (tok == NULL) {
        return NULL;
    }
    InitSemaphore(&tok->et_Lock);
    InitSemaphore(&tok->et_ServiceLock);
    InitSemaphore(&tok->et_SessionLock);
    NewList(&tok->et_Services);
    NewList(&tok->et_BrowseSessions);
    NewList(&tok->et_InquirySessions);
    NewList(&tok->et_Cache);
    NewList(&tok->et_Hosts);
    tok->et_Socket = -1;
    tok->et_NextHandle = 1;
    tok->et_QueryId = 1;
    tok->et_UdpPort = (UWORD)MDNS_DNS_PORT;
    tok->et_Magic = MDNS_TOKEN_MAGIC;
    tok->et_Task = FindTask(NULL);
    tok->et_RxBuf = (UBYTE *)AllocMem(MDNS_MAX_PACKET, MEMF_CLEAR);
    tok->et_TxBuf = (UBYTE *)AllocMem(MDNS_MAX_PACKET, MEMF_CLEAR);
    if (tok->et_RxBuf == NULL || tok->et_TxBuf == NULL) {
        token_free(tok);
        return NULL;
    }
    return tok;
}

/* The stack's base and the socket belong to this task and go away together:
 * around a reconfiguration, and for good at the end. */
static void
net_down(struct MDNSEngineToken *tok)
{
    mdnsRegGoodbyeAll(tok);
    mdnsNetClose(tok);
    if (tok->et_SocketBase != NULL) {
        CloseLibrary(tok->et_SocketBase);
        tok->et_SocketBase = NULL;
    }
}

static void
net_up(struct MDNSEngineToken *tok)
{
    struct Library *SocketBase;

    if (tok->et_SocketBase == NULL) {
        SocketBase = OpenLibrary("bsdsocket.library", 4);
        if (SocketBase == NULL) {
            return;
        }
        tok->et_SocketBase = SocketBase;
        SocketBaseTags(
            SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(engineErrno))),
            (IPTR)&engineErrno,
            SBTM_SETREF(SBTC_LOGTAGPTR), (IPTR)"mdnsd",
            TAG_END);
    }
    if (tok->et_Socket < 0 && mdnsNetOpen(tok) == MDNS_OK) {
        /* The host's name or address may be new: tell the link again. */
        mdnsRegHostChanged(tok);
    }
}

/* Waits for a signal, a datagram, or a second to pass. */
static ULONG
wait_and_receive(struct MDNSEngineToken *tok, ULONG sigmask)
{
    struct Library *SocketBase = tok->et_SocketBase;
    struct sockaddr_in from;
    struct timeval tv;
    socklen_t fromlen;
    fd_set readfds;
    LONG sock = tok->et_Socket;
    int got;

    if (SocketBase == NULL) {
        /* Between "reconfigure begin" and "end" there is no stack to ask. */
        return Wait(sigmask);
    }

    FD_ZERO(&readfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (sock < 0) {
        tv.tv_sec = MDNSD_RETRY_SECONDS;
        WaitSelect(0, NULL, NULL, NULL, &tv, &sigmask);
        return sigmask;
    }

    FD_SET(sock, &readfds);
    if (WaitSelect(sock + 1, &readfds, NULL, NULL, &tv, &sigmask) > 0 &&
        FD_ISSET(sock, &readfds)) {
        fromlen = sizeof(from);
        got = recvfrom(sock, (char *)tok->et_RxBuf, MDNS_MAX_PACKET, 0,
            (struct sockaddr *)&from, &fromlen);
        if (got > 0) {
            tok->et_RxPackets++;
            MDNSEngineHandlePacket(tok, tok->et_RxBuf, (LONG)got,
                (ULONG)ntohl((unsigned long)from.sin_addr.s_addr));
        }
    }
    return sigmask;
}

int
main(void)
{
    struct MDNSEngineToken *tok;
    struct MDNSDSignals sig;
    struct MDNSIPCMsg *msg;
    struct MsgPort *port;
    ULONG portmask, got;
    BOOL running = TRUE;

    /* One responder owns the host's names. */
    Forbid();
    port = FindPort((STRPTR)BONAMI_ENGINE_PORT);
    Permit();
    if (port != NULL) {
        PutStr("mdnsd: already running\n");
        return RETURN_WARN;
    }

    tok = token_create();
    port = CreateMsgPort();
    if (tok == NULL || port == NULL) {
        if (port != NULL) {
            DeleteMsgPort(port);
        }
        token_free(tok);
        return RETURN_FAIL;
    }
    tok->et_Port = port;
    portmask = 1UL << port->mp_SigBit;

    /* Without the service registry, for example when started by hand, it
     * still runs and Ctrl-C stops it. */
    mdnsdServiceOpen(&sig);

    net_up(tok);
    tok->et_Running = TRUE;
    tok->et_Ready = TRUE;
    port->mp_Node.ln_Name = (STRPTR)BONAMI_ENGINE_PORT;
    AddPort(port);

    while (running) {
        got = wait_and_receive(tok,
            portmask | sig.ms_Stop | sig.ms_Begin | sig.ms_End |
            SIGBREAKF_CTRL_C);

        if (got & (sig.ms_Stop | SIGBREAKF_CTRL_C)) {
            running = FALSE;
        }
        if (got & sig.ms_Begin) {
            net_down(tok);
        }
        if ((got & sig.ms_End) ||
            (tok->et_SocketBase != NULL && tok->et_Socket < 0)) {
            net_up(tok);
        }

        while ((msg = (struct MDNSIPCMsg *)GetMsg(port)) != NULL) {
            MDNSEngineDispatch(tok, msg);
            ReplyMsg(&msg->bim_Msg);
        }

        if (running && tok->et_Socket >= 0) {
            MDNSEngineTick(tok);
        }
    }

    /* No client may find the port any more; whoever already sent is told
     * that the engine is gone. */
    RemPort(port);
    while ((msg = (struct MDNSIPCMsg *)GetMsg(port)) != NULL) {
        msg->bim_Result = MDNSERR_NOENGINE;
        ReplyMsg(&msg->bim_Msg);
    }
    DeleteMsgPort(port);
    tok->et_Port = NULL;

    net_down(tok);
    mdnsdServiceClose();
    token_free(tok);
    return RETURN_OK;
}
