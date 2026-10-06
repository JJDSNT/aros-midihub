/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_engine.c - mdns.task main loop and IPC dispatch
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <bonami/mdns.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"
#include "private/bonami_bases.h"
#include "private/bonami_debug.h"

void
MDNSEngineHandlePacket(struct MDNSEngineToken *tok,
    const UBYTE *pkt, LONG pktlen, ULONG srcaddr)
{
    struct MDNSHeader *hdr;
    struct MDNSQuestion q;
    struct MDNSRecord r;
    LONG offset;
    LONG consumed;
    UWORD i;
    UWORD flags;

    (void)srcaddr;

    if (tok == NULL || pkt == NULL || pktlen < (LONG)sizeof(struct MDNSHeader)) {
        return;
    }

    hdr = (struct MDNSHeader *)pkt;
    flags = ntohs(hdr->flags);
    offset = (LONG)sizeof(struct MDNSHeader);

    if ((flags & DNS_FLAG_QR) == 0) {
        UWORD qd;

        qd = ntohs(hdr->qdcount);
        for (i = 0; i < qd; i++) {
            if (mdnsParseQuestion(pkt, pktlen, offset, &q, &consumed) != 0) {
                return;
            }
            mdnsRegHandleQuery(tok, &q);
            offset += consumed;
        }
        return;
    }

    for (i = 0; i < ntohs(hdr->qdcount); i++) {
        if (mdnsParseQuestion(pkt, pktlen, offset, &q, &consumed) != 0) {
            return;
        }
        offset += consumed;
    }

    for (i = 0; i < ntohs(hdr->ancount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        tok->et_RxAnswers++;
        mdnsBrowseHandleRecord(tok, &r, pkt, pktlen);
        offset += consumed;
    }

    /*
     * Authority section is not useful to us, but we must still walk it to reach
     * the additional section, where responders normally place the SRV/TXT/A
     * records that accompany a PTR answer (especially in legacy-unicast
     * replies).  Feeding the additionals through the same record handler lets a
     * single response yield a fully-populated cache entry (host + port + txt).
     */
    for (i = 0; i < ntohs(hdr->nscount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        offset += consumed;
    }

    for (i = 0; i < ntohs(hdr->arcount); i++) {
        if (mdnsParseRecord(pkt, pktlen, offset, &r, &consumed) != 0) {
            return;
        }
        mdnsBrowseHandleRecord(tok, &r, pkt, pktlen);
        offset += consumed;
    }
}

void
MDNSEngineDispatch(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    if (tok == NULL || msg == NULL) {
        return;
    }

    switch (msg->bim_Command) {
    case BONAMI_MSG_REGISTER:
        msg->bim_Result = MDNSEngineRegister(tok, msg);
        break;
    case BONAMI_MSG_UNREGISTER:
        MDNSEngineDeregister(tok, msg->bim_Handle);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_BROWSE:
        msg->bim_Result = MDNSEngineBrowse(tok, msg);
        break;
    case BONAMI_MSG_STOPBROWSE:
        MDNSEngineStopBrowse(tok, msg->bim_Handle);
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_RESOLVE:
        msg->bim_Result = MDNSEngineResolve(tok, msg);
        break;
    case BONAMI_MSG_STATUS:
        msg->bim_HostAddr = tok->et_RxPackets;
        msg->bim_Flags = tok->et_TxPackets;
        msg->bim_Port = (UWORD)tok->et_RxAnswers;
        msg->bim_Dbg0 = tok->et_DbgRecs;
        msg->bim_Dbg1 = tok->et_DbgPtr;
        msg->bim_Dbg2 = tok->et_DbgNotify;
        msg->bim_Result = MDNS_OK;
        break;
    case BONAMI_MSG_INQUIRY:
        msg->bim_Result = MDNSEngineInquiryNIPC(tok, msg);
        break;
    default:
        msg->bim_Result = MDNSERR_BADPARAM;
        break;
    }
}

void
MDNSEngineTick(struct MDNSEngineToken *tok)
{
    static ULONG lastAnnounce;

    if (tok == NULL) {
        return;
    }

    MDNSEngineInquiryTick(tok);
    mdnsBrowseRetick(tok);

    if (mdnsEngineNow() - lastAnnounce > 60) {
        mdnsRegAnnounceAll(tok);
        lastAnnounce = mdnsEngineNow();
    }
}

LONG
MDNSEnginePrepare(struct MDNSEngineToken *tok)
{
    struct MsgPort *port;

    if (tok == NULL) {
        return MDNSERR_BADPARAM;
    }

    if (tok->et_Port != NULL && tok->et_Socket >= 0) {
        tok->et_Ready = TRUE;
        return MDNS_OK;
    }

    baDbgPut("bonami: MDNSEnginePrepare enter\n");
    if (mdnsNetOpen(tok) != MDNS_OK) {
        baDbgPut("bonami: MDNSEnginePrepare mdnsNetOpen failed\n");
        return MDNSERR_NETWORK;
    }

    port = CreateMsgPort();
    if (port == NULL) {
        tok->et_StartStage = MDNS_STAGE_MSGPORT;
        baDbgPut("bonami: MDNSEnginePrepare CreateMsgPort failed\n");
        mdnsNetClose(tok);
        return MDNSERR_NOMEM;
    }

    port->mp_Node.ln_Name = (STRPTR)BONAMI_ENGINE_PORT;
    AddPort(port);
    tok->et_Port = port;
    tok->et_Ready = TRUE;
    baDbgPutPtr("bonami: MDNSEnginePrepare port=%lx ready\n", port);
    return MDNS_OK;
}

void
MDNSEnginePrepareUndo(struct MDNSEngineToken *tok)
{
    struct MsgPort *port;

    if (tok == NULL) {
        return;
    }

    port = tok->et_Port;
    if (port != NULL) {
        RemPort(port);
        DeleteMsgPort(port);
        tok->et_Port = NULL;
    }
    mdnsNetClose(tok);
    tok->et_Ready = FALSE;
}

void
MDNSEngineMain(void)
{
    struct MDNSEngineToken *tok;
    struct Library *sockbase;
    struct MsgPort *port;
    struct MDNSIPCMsg *msg;
    struct MDNSIPCMsg *shutdownMsg;
    struct timeval tv;
    fd_set readfds;
    UBYTE *buf;
    struct sockaddr_in from;
    socklen_t fromlen;
    int engineErrno;
    int got;
    LONG sock;
    ULONG sigmask;
    BOOL running;
    ULONG srcip;
    LONG rc;

    tok = MDNSTokenGet();
    if (tok == NULL) {
        baDbgPut("bonami: MDNSEngineMain no token\n");
        RemTask(NULL);
        return;
    }

    /*
     * bsdsocket.library sockets and their event signals belong to the task
     * that made the socket calls.  The engine task must therefore open its
     * own SocketBase and create/use the socket and MsgPort itself, rather
     * than inheriting resources built in the caller's context.
     */
    engineErrno = 0;
    sockbase = OpenLibrary("bsdsocket.library", 4);
    if (sockbase == NULL) {
        baDbgPut("bonami: MDNSEngineMain OpenLibrary(bsdsocket) failed\n");
        tok->et_SocketBase = NULL;
        tok->et_StartRc = MDNSERR_NETWORK;
        tok->et_Running = FALSE;
        tok->et_Ready = FALSE;
        tok->et_StartDone = TRUE;
        RemTask(NULL);
        return;
    }
    tok->et_SocketBase = sockbase;

    {
        struct Library *SocketBase = sockbase;
        SocketBaseTags(
            SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(engineErrno))),
            (IPTR)&engineErrno,
            SBTM_SETREF(SBTC_LOGTAGPTR), (IPTR)"bonami.task",
            TAG_END);
    }

    rc = MDNSEnginePrepare(tok);
    if (rc != MDNS_OK) {
        baDbgPutLong("bonami: MDNSEngineMain prepare rc=%ld\n", rc);
        CloseLibrary(sockbase);
        tok->et_SocketBase = NULL;
        tok->et_StartRc = rc;
        tok->et_Running = FALSE;
        tok->et_Ready = FALSE;
        tok->et_StartDone = TRUE;
        RemTask(NULL);
        return;
    }

    port = tok->et_Port;
    sock = tok->et_Socket;
    buf = tok->et_RxBuf;

    /* Handshake: signal the starter that the engine is up and running. */
    tok->et_StartRc = MDNS_OK;
    tok->et_StartDone = TRUE;

    baDbgPut("bonami: MDNSEngineMain loop enter\n");
    running = TRUE;
    shutdownMsg = NULL;

    {
        struct Library *SocketBase = sockbase;

        while (running) {
            sigmask = 1UL << port->mp_SigBit;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            WaitSelect(sock + 1, &readfds, NULL, NULL, &tv, &sigmask);

            if (FD_ISSET(sock, &readfds)) {
                fromlen = sizeof(from);
                got = recvfrom(sock, (char *)buf, MDNS_MAX_PACKET, 0,
                    (struct sockaddr *)&from, &fromlen);
                if (got > 0) {
                    tok->et_RxPackets++;
                    srcip = (ULONG)ntohl((unsigned long)from.sin_addr.s_addr);
                    MDNSEngineHandlePacket(tok, buf, (LONG)got, srcip);
                }
            }

            while ((msg = (struct MDNSIPCMsg *)GetMsg(port)) != NULL) {
                if (msg->bim_Command == BONAMI_MSG_SHUTDOWN) {
                    /*
                     * Do NOT reply to SHUTDOWN here.  The reply is the signal
                     * the stopping task waits on before it frees the token, so
                     * it must be the engine's *last* action once every scrap of
                     * token cleanup is done (see below).  Replying now would let
                     * the stopper free the token while this task is still
                     * running teardown code against it - an intermittent
                     * use-after-free that corrupts the allocator free-list and
                     * surfaces as a "wild free" a run or two later.
                     */
                    running = FALSE;
                    msg->bim_Result = MDNS_OK;
                    shutdownMsg = msg;
                } else {
                    MDNSEngineDispatch(tok, msg);
                    ReplyMsg(&msg->bim_Msg);
                }
            }

            if (running) {
                MDNSEngineTick(tok);
            }
        }
    }

    RemPort(port);
    DeleteMsgPort(port);
    tok->et_Port = NULL;
    mdnsNetClose(tok);
    CloseLibrary(sockbase);
    tok->et_SocketBase = NULL;
    tok->et_Ready = FALSE;
    tok->et_Running = FALSE;

    /*
     * Reply to SHUTDOWN as the final token access.  MDNSEngineStopTask() blocks
     * in WaitPort() for this reply and only then frees the token, so replying
     * last guarantees the token is untouched by this task from here on.  After
     * ReplyMsg the message memory belongs to the stopper again; RemTask(NULL)
     * touches only Exec state (and the library code segment, which stays
     * resident), never the freed token.
     */
    if (shutdownMsg != NULL) {
        ReplyMsg(&shutdownMsg->bim_Msg);
    }
    RemTask(NULL);
}
