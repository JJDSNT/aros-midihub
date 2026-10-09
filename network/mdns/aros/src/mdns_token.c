/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * mdns_token.c - NamedObject engine token (Envoy.mdns)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <utility/name.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/utility.h>

#include <bonami/mdns.h>
#include "private/bonami_internal.h"
#include "private/bonami_ipc.h"
#include "private/mdns_engine.h"
#include "private/bonami_bases.h"
#include "private/bonami_debug.h"

static struct MDNSEngineToken *MDNSGlobalToken;

struct MDNSEngineToken *
MDNSTokenGet(void)
{
    return MDNSGlobalToken;
}

static void
MDNSTokenFreeLists(struct MDNSEngineToken *tok)
{
    struct MDNSRegisteredService *rs;
    struct MDNSBrowseSession *bs;
    struct MDNSInquirySession *is;
    struct MDNSCacheEntry *ce;
    struct Node *n;

    while ((n = tok->et_Services.lh_Head) != NULL && n->ln_Succ != NULL) {
        rs = (struct MDNSRegisteredService *)n;
        Remove(n);
        FreeMem(rs, sizeof(*rs));
    }
    while ((n = tok->et_BrowseSessions.lh_Head) != NULL && n->ln_Succ != NULL) {
        bs = (struct MDNSBrowseSession *)n;
        Remove(n);
        FreeMem(bs, sizeof(*bs));
    }
    while ((n = tok->et_InquirySessions.lh_Head) != NULL && n->ln_Succ != NULL) {
        is = (struct MDNSInquirySession *)n;
        Remove(n);
        FreeMem(is, sizeof(*is));
    }
    while ((n = tok->et_Cache.lh_Head) != NULL && n->ln_Succ != NULL) {
        ce = (struct MDNSCacheEntry *)n;
        Remove(n);
        FreeMem(ce, sizeof(*ce));
    }
}

static LONG
MDNSEngineStartTask(struct MDNSEngineToken *tok)
{
    struct Task *task;
    LONG i;

    baDbgPut("bonami: MDNSEngineStartTask enter\n");
    if (tok->et_Task != NULL && tok->et_Ready) {
        baDbgPut("bonami: MDNSEngineStartTask already ready\n");
        return MDNS_OK;
    }

    if (tok->et_Task != NULL) {
        baDbgPut("bonami: MDNSEngineStartTask task already running\n");
        return MDNS_OK;
    }

    /*
     * The engine task opens its own bsdsocket.library base and creates the
     * socket and MsgPort in its own context, because bsdsocket sockets (and
     * their event signals) belong to the task that made the socket calls.
     * We only spawn the task here and wait for its readiness handshake.
     */
    tok->et_StartDone = FALSE;
    tok->et_StartRc = MDNSERR_NOENGINE;
    tok->et_StartStage = MDNS_STAGE_NONE;
    tok->et_StartErrno = 0;
    tok->et_Running = TRUE;

    task = CreateTask("mdns.task", 0, (APTR)MDNSEngineMain, MDNS_ENGINE_STACK);
    if (task == NULL) {
        baDbgPut("bonami: MDNSEngineStartTask CreateTask failed\n");
        tok->et_Running = FALSE;
        return MDNSERR_NOENGINE;
    }

    tok->et_Task = task;

    for (i = 0; i < 500 && !tok->et_StartDone; i++) {
        Delay(1L);
    }

    if (!tok->et_StartDone || tok->et_StartRc != MDNS_OK) {
        baDbgPutLong("bonami: MDNSEngineStartTask engine start rc=%ld\n",
            (LONG)tok->et_StartRc);
        baDbgPutLong("bonami: MDNSEngineStartTask fail stage=%ld ",
            (LONG)tok->et_StartStage);
        baDbgPutLong("errno=%ld\n", (LONG)tok->et_StartErrno);
        tok->et_Task = NULL;
        return tok->et_StartDone ? tok->et_StartRc : MDNSERR_NOENGINE;
    }

    tok->et_Ready = TRUE;
    baDbgPutPtr("bonami: MDNSEngineStartTask task=%lx\n", task);
    baDbgPut("bonami: MDNSEngineStartTask ready\n");
    return MDNS_OK;
}

static void
MDNSEngineStopTask(struct MDNSEngineToken *tok)
{
    struct MDNSIPCMsg *msg;
    struct MsgPort *replyPort;

    if (tok == NULL || tok->et_Task == NULL) {
        return;
    }

    /*
     * Send SHUTDOWN with a real reply port and block in WaitPort() for the
     * engine's ReplyMsg.  The engine defers that reply until it has finished
     * ALL of its token teardown (socket, port, SocketBase, flags), so the
     * reply doubles as a "engine is completely done touching the token" signal.
     * Only after it arrives is it safe for the caller to free the token; there
     * is therefore no need for the old et_Running poll, whose bounded timeout
     * could let the caller free the token while the engine was still winding
     * down (an intermittent use-after-free / "wild free").
     */
    replyPort = CreateMsgPort();
    if (tok->et_Port != NULL && replyPort != NULL) {
        msg = (struct MDNSIPCMsg *)AllocMem(sizeof(*msg), MEMF_CLEAR);
        if (msg != NULL) {
            msg->bim_Msg.mn_Node.ln_Type = NT_MESSAGE;
            msg->bim_Msg.mn_Length = sizeof(*msg);
            msg->bim_Msg.mn_ReplyPort = replyPort;
            msg->bim_Command = BONAMI_MSG_SHUTDOWN;
            PutMsg(tok->et_Port, &msg->bim_Msg);
            WaitPort(replyPort);
            while (GetMsg(replyPort) != NULL) {
                /* drain */
            }
            FreeMem(msg, sizeof(*msg));
        }
    }

    if (replyPort != NULL) {
        DeleteMsgPort(replyPort);
    }

    tok->et_Task = NULL;
    tok->et_Port = NULL;
    tok->et_Ready = FALSE;
}

/*
 * MDNSTokenJoin - attach this opener to an already-published token.
 *
 * Serialised by the token's own et_Lock semaphore (the token lives in the
 * library's shared data segment via the NamedObject namespace, so the
 * semaphore is a genuine cross-task lock).  The engine task is started on
 * demand by whichever opener first finds et_Ready clear; all other openers
 * block on et_Lock until that opener has finished and marked the engine
 * ready, so exactly one mdns.task engine ever runs.
 */
static LONG
MDNSTokenJoin(struct BonamiPrivate *pb, struct MDNSEngineToken *tok)
{
    LONG rc;

    ObtainSemaphore(&tok->et_Lock);
    if (!tok->et_Ready) {
        /*
         * Publish the global token BEFORE spawning the engine task: the
         * engine task reads it via MDNSTokenGet() during its own start-up,
         * which happens while MDNSEngineStartTask is still waiting for the
         * readiness handshake.  If it is not set first the task bails out
         * with "no token" and start times out with MDNSERR_NOENGINE.
         */
        MDNSGlobalToken = tok;
        rc = MDNSEngineStartTask(tok);
        if (rc != MDNS_OK) {
            baDbgPutLong("bonami: MDNSTokenJoin start rc=%ld\n", rc);
            MDNSGlobalToken = NULL;
            ReleaseSemaphore(&tok->et_Lock);
            return rc;
        }
    }

    tok->et_UseCnt++;
    MDNSGlobalToken = tok;
    pb->EngineToken = tok;
    pb->EngineOpen = TRUE;
    ReleaseSemaphore(&tok->et_Lock);
    baDbgPutLong("bonami: MDNSTokenJoin ok useCnt=%ld\n",
        (LONG)tok->et_UseCnt);
    return MDNS_OK;
}

/*
 * Read the mDNS UDP port from the MDNS_PORT_ENVVAR environment variable, so a
 * tester can move the engine off 5353 (e.g. when the host stack's own mDNS
 * responder already owns that port).  Defaults to the standard 5353.  Runs in
 * the caller's Process context, where GetVar() is valid.
 */
static UWORD
MDNSReadPort(void)
{
    char buf[16];
    LONG len;
    LONG val;
    LONG i;

    len = GetVar((STRPTR)MDNS_PORT_ENVVAR, (STRPTR)buf, (LONG)sizeof(buf) - 1,
        0);
    if (len <= 0) {
        /* AROS is the native socket host, so full responder mode is normal. */
        return (UWORD)MDNS_DNS_PORT;
    }
    buf[len] = '\0';
    val = 0;
    for (i = 0; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            break;
        }
        val = val * 10 + (buf[i] - '0');
    }
    if (val < 0 || val > 65535) {
        return (UWORD)0;
    }
    return (UWORD)val;
}

/*
 * Drop a NamedObject whose payload token is no longer valid (left dangling by
 * a previous run that ended abnormally).  The find reference must already be
 * released by the caller so RemNamedObject() does not block.
 */
static void
MDNSTokenDropStale(struct NamedObject *no)
{
    baDbgPut("bonami: MDNSTokenAttach dropping stale token\n");
    no->no_Object = NULL;
    RemNamedObject(no, NULL);
    FreeNamedObject(no);
}

/*
 * Free a token and its owned scratch buffers.  The MDNS_MAX_PACKET (jumbo)
 * receive/transmit buffers are heap-allocated rather than kept on the engine
 * task stack, which is far too small to hold two of them at once.
 */
static void
MDNSTokenFreeMem(struct MDNSEngineToken *tok)
{
    if (tok == NULL) {
        return;
    }
    if (tok->et_RxBuf != NULL) {
        FreeMem(tok->et_RxBuf, MDNS_MAX_PACKET);
        tok->et_RxBuf = NULL;
    }
    if (tok->et_TxBuf != NULL) {
        FreeMem(tok->et_TxBuf, MDNS_MAX_PACKET);
        tok->et_TxBuf = NULL;
    }
    tok->et_Magic = 0;
    FreeMem(tok, sizeof(*tok));
}

static LONG
MDNSTokenCreate(struct BonamiPrivate *pb)
{
    struct MDNSEngineToken *tok;
    struct NamedObject *no;
    struct TagItem tags[2];
    LONG rc;

    baDbgPut("bonami: MDNSTokenCreate enter\n");
    tok = (struct MDNSEngineToken *)AllocMem(sizeof(*tok), MEMF_CLEAR);
    if (tok == NULL) {
        baDbgPut("bonami: MDNSTokenCreate nomem\n");
        return MDNSERR_NOMEM;
    }

    /*
     * The token must be fully initialised before it becomes findable via
     * AddNamedObject, because a racing opener that finds it will immediately
     * use et_Lock and the lists.
     */
    InitSemaphore(&tok->et_Lock);
    InitSemaphore(&tok->et_ServiceLock);
    InitSemaphore(&tok->et_SessionLock);
    NewList(&tok->et_Services);
    NewList(&tok->et_BrowseSessions);
    NewList(&tok->et_InquirySessions);
    NewList(&tok->et_Cache);
    tok->et_Socket = -1;
    tok->et_NextHandle = 1;
    tok->et_QueryId = 1;
    tok->et_UdpPort = MDNSReadPort();
    tok->et_Magic = MDNS_TOKEN_MAGIC;

    tok->et_RxBuf = (UBYTE *)AllocMem(MDNS_MAX_PACKET, MEMF_CLEAR);
    tok->et_TxBuf = (UBYTE *)AllocMem(MDNS_MAX_PACKET, MEMF_CLEAR);
    if (tok->et_RxBuf == NULL || tok->et_TxBuf == NULL) {
        baDbgPut("bonami: MDNSTokenCreate buffer nomem\n");
        MDNSTokenFreeMem(tok);
        return MDNSERR_NOMEM;
    }

    tags[0].ti_Tag = ANO_Flags;
    tags[0].ti_Data = NSF_NODUPS;
    tags[1].ti_Tag = TAG_END;

    no = AllocNamedObjectA((STRPTR)MDNS_ENGINE_TOKEN, tags);
    if (no == NULL) {
        baDbgPut("bonami: MDNSTokenCreate AllocNamedObject failed\n");
        MDNSTokenFreeMem(tok);
        return MDNSERR_NOMEM;
    }

    no->no_Object = (APTR)tok;
    tok->et_NamedObject = no;

    /*
     * AddNamedObject with NSF_NODUPS is atomic: if another opener published
     * the token first, this fails and we simply drop ours and attach to the
     * winner's token.  This closes the create/create race without a separate
     * global lock.
     */
    if (!AddNamedObject(NULL, no)) {
        baDbgPut("bonami: MDNSTokenCreate lost publish race\n");
        FreeNamedObject(no);
        MDNSTokenFreeMem(tok);
        return (LONG)MDNSTokenAttach(pb);
    }

    rc = MDNSTokenJoin(pb, tok);
    if (rc != MDNS_OK) {
        /* Engine failed to start: withdraw the published token. */
        baDbgPutLong("bonami: MDNSTokenCreate join rc=%ld\n", rc);
        RemNamedObject(no, NULL);
        FreeNamedObject(no);
        MDNSTokenFreeMem(tok);
        MDNSGlobalToken = NULL;
        return rc;
    }

    baDbgPut("bonami: MDNSTokenCreate ok\n");
    return MDNS_OK;
}

int
MDNSTokenAttach(struct BonamiPrivate *pb)
{
    struct NamedObject *no;
    struct MDNSEngineToken *tok;
    LONG rc;

    baDbgPut("bonami: MDNSTokenAttach enter\n");
    if (pb == NULL) {
        baDbgPut("bonami: MDNSTokenAttach pb=NULL\n");
        return MDNSERR_BADPARAM;
    }

    no = FindNamedObject(NULL, (STRPTR)MDNS_ENGINE_TOKEN, NULL);
    if (no == NULL) {
        baDbgPut("bonami: MDNSTokenAttach create new token\n");
        return (int)MDNSTokenCreate(pb);
    }

    baDbgPut("bonami: MDNSTokenAttach join existing\n");
    tok = (struct MDNSEngineToken *)no->no_Object;

    /*
     * The token's lifetime is guarded by et_UseCnt, not by the NamedObject
     * use count, so we can release the find reference right away and still
     * safely use tok while any opener holds it.
     */
    ReleaseNamedObject(no);

    /*
     * A NULL payload or a bad magic means the published object is stale (its
     * token memory was freed by a run that ended abnormally).  Drop it and
     * create a fresh engine rather than dereferencing a dangling pointer.
     */
    if (tok == NULL || tok->et_Magic != MDNS_TOKEN_MAGIC) {
        MDNSTokenDropStale(no);
        return (int)MDNSTokenCreate(pb);
    }

    rc = MDNSTokenJoin(pb, tok);
    return (int)rc;
}

void
MDNSTokenDetach(struct BonamiPrivate *pb)
{
    struct MDNSEngineToken *tok;
    struct NamedObject *no;
    BOOL last;

    if (pb == NULL || pb->EngineToken == NULL) {
        return;
    }

    tok = pb->EngineToken;
    pb->EngineToken = NULL;
    pb->EngineOpen = FALSE;

    ObtainSemaphore(&tok->et_Lock);
    if (tok->et_UseCnt > 0) {
        tok->et_UseCnt--;
    }
    last = (tok->et_UseCnt == 0) ? TRUE : FALSE;
    ReleaseSemaphore(&tok->et_Lock);

    if (!last) {
        return;
    }

    /*
     * Last opener: shut the engine down and withdraw the published token.
     * RemNamedObject(no, NULL) blocks until no other opener holds a find
     * reference; because MDNSTokenAttach releases its find reference
     * immediately, that count is already zero here.
     */
    MDNSEngineStopTask(tok);
    mdnsNetClose(tok);
    MDNSTokenFreeLists(tok);

    no = tok->et_NamedObject;
    if (no != NULL) {
        no->no_Object = NULL;
        RemNamedObject(no, NULL);
        FreeNamedObject(no);
    }

    MDNSTokenFreeMem(tok);
    MDNSGlobalToken = NULL;
}

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
