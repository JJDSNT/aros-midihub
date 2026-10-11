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
#include <exec/execbase.h>
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

/* Seconds between looks at the stack's interfaces: for the first address,
 * for one that changed, for an interface that came or went. */
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

/* Fiftieths of a second; only differences mean anything. */
ULONG
mdnsEngineTicks(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 4320000UL + (ULONG)ds.ds_Minute * 3000UL +
        (ULONG)ds.ds_Tick;
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
    free_list(&tok->et_Queries, sizeof(struct MDNSQuerySession));
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
    NewList(&tok->et_Queries);
    NewList(&tok->et_Lookups);
    tok->et_Socket = -1;
    tok->et_SendIf = -1;
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

/* Seconds between looks for clients that ended without closing the library. */
#define MDNSD_REAP_SECONDS 5
#define MDNSD_REAP_MAX 16

static BOOL
task_alive(struct Task *task)
{
    struct Node *n;
    BOOL alive = FALSE;

    Disable();
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ != NULL && !alive;
        n = n->ln_Succ) {
        alive = n == &task->tc_Node;
    }
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ != NULL && !alive;
        n = n->ln_Succ) {
        alive = n == &task->tc_Node;
    }
    Enable();
    return alive;
}

static void
note_owner(struct Task **dead, LONG *count, struct Task *task)
{
    LONG i;

    if (task == NULL || task == FindTask(NULL) || *count >= MDNSD_REAP_MAX) {
        return;
    }
    for (i = 0; i < *count; i++) {
        if (dead[i] == task) {
            return;
        }
    }
    if (!task_alive(task)) {
        dead[(*count)++] = task;
    }
}

/*
 * A client that ends without closing bonami.library sends no release. What
 * it left would be announced for ever and its hooks would be called in
 * memory that is no longer its own, so the owners are checked against the
 * system's task lists now and then. A task that started at the same address
 * as one that ended is taken for it; nothing here can tell them apart.
 */
static void
reap_dead_clients(struct MDNSEngineToken *tok)
{
    struct Task *dead[MDNSD_REAP_MAX];
    struct Node *n;
    LONG count = 0, i;

    for (n = tok->et_Services.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        note_owner(dead, &count, ((struct MDNSRegisteredService *)n)->rs_Task);
    }
    for (n = tok->et_BrowseSessions.lh_Head; n->ln_Succ != NULL;
        n = n->ln_Succ) {
        note_owner(dead, &count, ((struct MDNSBrowseSession *)n)->bs_Task);
    }
    for (n = tok->et_Queries.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        note_owner(dead, &count, ((struct MDNSQuerySession *)n)->qs_Task);
    }
    for (n = tok->et_InquirySessions.lh_Head; n->ln_Succ != NULL;
        n = n->ln_Succ) {
        note_owner(dead, &count, ((struct MDNSInquirySession *)n)->is_Task);
    }
    for (i = 0; i < count; i++) {
        MDNSEngineReleaseServices(tok, dead[i]);
        MDNSEngineReleaseBrowses(tok, dead[i]);
        MDNSEngineReleaseQueries(tok, dead[i]);
        MDNSEngineReleaseInquiries(tok, dead[i]);
    }
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
    /* An interface came, went or changed its address: the host's records
     * are told to the links again with the addresses as they are now. */
    if (mdnsNetRescan(tok)) {
        mdnsRegHostChanged(tok);
    }
}

/* Waits for a signal, a datagram on any interface, or a second to pass: a
 * quarter of one while a name is being probed. */
static ULONG
wait_and_receive(struct MDNSEngineToken *tok, ULONG sigmask, BOOL probing)
{
    struct Library *SocketBase = tok->et_SocketBase;
    struct sockaddr_in from;
    struct timeval tv;
    socklen_t fromlen;
    fd_set readfds;
    LONG i, sock, top = -1;
    int got;

    if (SocketBase == NULL) {
        /* Between "reconfigure begin" and "end" there is no stack to ask. */
        return Wait(sigmask);
    }

    FD_ZERO(&readfds);
    tv.tv_sec = probing ? 0 : 1;
    tv.tv_usec = probing ? 250000 : 0;
    for (i = 0; i < tok->et_IfCount; i++) {
        sock = tok->et_Ifs[i].mi_Socket;
        FD_SET(sock, &readfds);
        if (sock > top) {
            top = sock;
        }
    }
    if (WaitSelect(top + 1, top >= 0 ? &readfds : NULL, NULL, NULL, &tv,
            &sigmask) <= 0) {
        return sigmask;
    }

    for (i = 0; i < tok->et_IfCount; i++) {
        sock = tok->et_Ifs[i].mi_Socket;
        if (!FD_ISSET(sock, &readfds)) {
            continue;
        }
        fromlen = sizeof(from);
        got = recvfrom(sock, (char *)tok->et_RxBuf, MDNS_MAX_PACKET, 0,
            (struct sockaddr *)&from, &fromlen);
        if (got > 0) {
            tok->et_RxPackets++;
            /* what this packet causes to be sent goes back to its link */
            tok->et_SendIf = i;
            MDNSEngineHandlePacket(tok, tok->et_RxBuf, (LONG)got,
                (ULONG)ntohl((unsigned long)from.sin_addr.s_addr));
            tok->et_SendIf = -1;
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
    BOOL probing = FALSE;
    ULONG lastReap = 0;
    ULONG lastScan = 0;

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
            SIGBREAKF_CTRL_C, probing);

        if (got & (sig.ms_Stop | SIGBREAKF_CTRL_C)) {
            running = FALSE;
        }
        /*
         * The stack signals the start and the end of a reconfiguration, and
         * may well have finished before this task runs: then both signals
         * are here at once, and the sockets are closed and opened again in
         * one go. Between a start and an end that come apart the base is
         * closed, which keeps the periodic look from opening anything.
         */
        if (got & sig.ms_Begin) {
            net_down(tok);
        }
        if ((got & sig.ms_End) ||
            mdnsEngineNow() - lastScan >= MDNSD_RETRY_SECONDS) {
            lastScan = mdnsEngineNow();
            if (tok->et_SocketBase != NULL || (got & sig.ms_End)) {
                net_up(tok);
            }
        }

        while ((msg = (struct MDNSIPCMsg *)GetMsg(port)) != NULL) {
            if (MDNSEngineDispatch(tok, msg)) {
                ReplyMsg(&msg->bim_Msg);
            }
        }

        if (running && mdnsEngineNow() - lastReap >= MDNSD_REAP_SECONDS) {
            lastReap = mdnsEngineNow();
            reap_dead_clients(tok);
        }

        probing = FALSE;
        if (running && tok->et_Socket >= 0) {
            MDNSEngineTick(tok);
            probing = mdnsRegTick(tok);
        } else if (running) {
            mdnsQueryTick(tok);     /* lookups still run out of time */
        }
    }

    /* No client may find the port any more; whoever already sent is told
     * that the engine is gone. */
    RemPort(port);
    mdnsLookupAbort(tok);
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
