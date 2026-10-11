/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * Handshake - find out how to reach one named service on the local network.
 *
 * You already know a service's name (e.g. from Nearby); Handshake resolves it
 * to the host, IP address and port you need to connect, plus any extra info the
 * service advertises.
 *
 *   Handshake NAME/A TYPE/A [DOMAIN <d>] [TIME <secs>]
 *
 *   NAME    the service's name, e.g. "Tim's Mac" (quote names with spaces).
 *   TYPE    a plain word ("web", "files", "ssh", ...) or a real DNS-SD type.
 *   DOMAIN  network domain (default: your local network).
 *   TIME    how many seconds to keep trying (default 30).
 *
 * Examples:
 *   Handshake "Tim's Mac" web
 *   Handshake AmigaShare files
 *
 * Native AmigaOS only.  The resolve Hook runs in the bonami engine task and
 * just pushes the result into a BARing; this main loop drains and prints it.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

#include <bonami/mdns.h>
#include <proto/bonami.h>

#include "ba_common.h"
#include "ba_types.h"

#define ARG_NAME    0
#define ARG_TYPE    1
#define ARG_DOMAIN  2
#define ARG_TIME    3
#define ARG_COUNT   4

static const char *HandshakeTemplate = "NAME/A,TYPE/A,DOMAIN/K,TIME/N/K";

BA_HOOK(HandshakeHook)
{
    BA_HOOK_INIT
    struct MDNSResolveMsg *msg;

    (void)object;
    msg = (struct MDNSResolveMsg *)message;
    if (msg == NULL) {
        return 0;
    }
    if (msg->rm_Code == MEVENT_RESOLVED) {
        BARingPush((struct BARing *)hook->h_Data, BA_EV_RESOLVED,
            &msg->rm_Instance);
    } else {
        BARingPush((struct BARing *)hook->h_Data, BA_EV_FAILED,
            &msg->rm_Instance);
    }
    return 0;
    BA_HOOK_EXIT
}

static int
HandshakeStop(void)
{
    return (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? 1 : 0;
}

static void
HandshakeReport(struct MDNSServiceInstance *si)
{
    BAPrintf("\nReached \"%s\":\n", si->si_Name);
    if (si->si_HostName[0] != '\0') {
        BAPrintf("  Host:  %s\n", si->si_HostName);
    }
    if (si->si_HostAddr != 0) {
        BAPrintf("  IP:    %ld.%ld.%ld.%ld\n",
            BA_IP_ARGS(si->si_HostAddr));
    }
    BAPrintf("  Port:  %ld\n", (LONG)si->si_Port);
    if (si->si_TXTLength > 0) {
        char txt[MDNS_MAX_TXT + 1];
        LONG tlen;

        tlen = (LONG)si->si_TXTLength;
        if (tlen > MDNS_MAX_TXT) {
            tlen = MDNS_MAX_TXT;
        }
        CopyMem((APTR)si->si_TXT, (APTR)txt, (ULONG)tlen);
        txt[tlen] = '\0';
        BAPrintf("  Info:  %s\n", txt);
    }
}

static int
HandshakeRun(struct BARing *ring, CONST_STRPTR name, CONST_STRPTR word,
    LONG timeSecs)
{
    struct Hook hook;
    struct BAEvent ev;
    CONST_STRPTR type;
    LONG rc;
    LONG tries;
    LONG maxTries;

    type = BATypeFromAlias(word);

    hook.h_Entry = BA_HOOK_ENTRY(HandshakeHook);
    hook.h_SubEntry = NULL;
    hook.h_Data = (APTR)ring;

    BAPrintf("Contacting \"%s\" (%s)...\n", name, BATypeFriendly(type));

    /* Poll once per second; ResolveServiceA primes a query and answers from
     * cache once the records arrive. */
    maxTries = (timeSecs > 0) ? timeSecs : 30;
    (void)SetSignal(0L, SIGBREAKF_CTRL_C);
    for (tries = 0; tries < maxTries; tries++) {
        if (HandshakeStop()) {
            break;
        }
        rc = ResolveServiceA((STRPTR)name, (STRPTR)type, &hook, NULL);
        if (rc == MDNS_OK) {
            while (BARingPop(ring, &ev)) {
                if (ev.ev_Kind == BA_EV_RESOLVED) {
                    HandshakeReport(&ev.ev_Inst);
                    return 0;
                }
            }
        }
        Delay(50);
    }

    BAPrintf("\nCould not reach \"%s\".  Is it switched on and advertising?\n",
        name);
    return 5;
}

int
main(void)
{
    struct RDArgs *rda;
    struct BARing *ring;
    IPTR args[ARG_COUNT];
    CONST_STRPTR name;
    CONST_STRPTR word;
    LONG timeSecs;
    LONG rc;
    int result;
    int i;

    for (i = 0; i < ARG_COUNT; i++) {
        args[i] = 0;
    }

    rda = ReadArgs((STRPTR)HandshakeTemplate, args, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "Handshake");
        return 20;
    }

    name = (CONST_STRPTR)args[ARG_NAME];
    word = (CONST_STRPTR)args[ARG_TYPE];
    timeSecs = 0;
    if (args[ARG_TIME] != 0) {
        timeSecs = *((LONG *)args[ARG_TIME]);
    }

    if (!BAAppOpen()) {
        BAPrintf("Cannot open %s (v%ld+).\n", BONAMINAME,
            (LONG)BONAMI_LIB_VERSION);
        FreeArgs(rda);
        return 20;
    }

    ring = BARingAlloc();
    if (ring == NULL) {
        BAPrintf("Out of memory.\n");
        BAAppClose();
        FreeArgs(rda);
        return 20;
    }

    rc = BAAppOpenEngine();
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("start mDNS", rc);
        BAAppClose();
        BARingFree(ring);
        FreeArgs(rda);
        return 20;
    }

    result = HandshakeRun(ring, name, word, timeSecs);

    BAAppClose();
    BARingFree(ring);
    FreeArgs(rda);
    return result;
}
