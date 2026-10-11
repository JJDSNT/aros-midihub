/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * Nearby - see what services are on your local network, in plain English.
 *
 * A friendly Amiga-style front end to bonami.library's mDNS/DNS-SD browse.
 * Uses a standard ReadArgs template and a service-type alias table so nobody
 * has to type "_afpovertcp._tcp".
 *
 *   Nearby [TYPE] [ALL] [DOMAIN <d>] [TIME <secs>] [RAW] [PORTS]
 *
 *   TYPE    a plain word ("web", "printer", "files", "ssh", ...) or a real
 *           DNS-SD type ("_http._tcp").  Omit it (or say ALL) to list the
 *           kinds of service present on the network instead of instances.
 *   ALL     list the service kinds on the network (not individual services).
 *   DOMAIN  network domain to search (default: your local network).
 *   TIME    how many seconds to look; omit to keep looking until Ctrl-C.
 *   RAW     also show the underlying DNS-SD type strings.
 *   PORTS   show each service's host and port (and any extra info).
 *
 * Examples:
 *   Nearby                 ; what kinds of things are out there?
 *   Nearby web             ; who has a web server?
 *   Nearby printer PORTS   ; printers, with host and port
 *   Nearby ssh TIME 10     ; look for SSH logins for 10 seconds
 *
 * Native AmigaOS only.  The browse Hook runs in the bonami engine task and just
 * pushes events into a BARing; this main loop drains and prints them.
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

#define ARG_TYPE    0
#define ARG_ALL     1
#define ARG_DOMAIN  2
#define ARG_TIME    3
#define ARG_RAW     4
#define ARG_PORTS   5
#define ARG_COUNT   6

static const char *NearbyTemplate = "TYPE,ALL/S,DOMAIN/K,TIME/N/K,RAW/S,PORTS/S";
static const char version_tag[] __attribute__((used)) = "\0$VER: Nearby 40.0 (08.07.26)";

/*------------------------------------------------------------------------*/
/* Hooks (engine-task context): push events into the ring only. */

BA_HOOK(NearbyInstanceHook)
{
    BA_HOOK_INIT
    struct MDNSBrowseMsg *msg;

    (void)object;
    msg = (struct MDNSBrowseMsg *)message;
    if (msg == NULL) {
        return 0;
    }
    if (msg->bm_Code == MEVENT_ADD) {
        BARingPush((struct BARing *)hook->h_Data, BA_EV_ADD, &msg->bm_Instance);
    } else if (msg->bm_Code == MEVENT_REMOVE) {
        BARingPush((struct BARing *)hook->h_Data, BA_EV_REMOVE,
            &msg->bm_Instance);
    }
    return 0;
    BA_HOOK_EXIT
}

BA_HOOK(NearbyMetaHook)
{
    BA_HOOK_INIT
    struct MDNSBrowseMsg *msg;

    (void)object;
    msg = (struct MDNSBrowseMsg *)message;
    if (msg == NULL || msg->bm_Code != MEVENT_ADD) {
        return 0;
    }
    BARingPush((struct BARing *)hook->h_Data, BA_EV_TYPE, &msg->bm_Instance);
    return 0;
    BA_HOOK_EXIT
}

/*------------------------------------------------------------------------*/

static int
NearbyStop(void)
{
    return (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? 1 : 0;
}

/*
 * Loop draining the ring until Ctrl-C, or until timeSecs elapse when timeSecs
 * is > 0.  handler() formats each popped event.
 */
static void
NearbyLoop(struct BARing *ring, LONG timeSecs,
    void (*handler)(struct BAEvent *, BOOL, BOOL), BOOL raw, BOOL ports)
{
    struct BAEvent ev;
    LONG halfSecsLeft;

    halfSecsLeft = (timeSecs > 0) ? (timeSecs * 2) : 0;

    (void)SetSignal(0L, SIGBREAKF_CTRL_C);
    for (;;) {
        while (BARingPop(ring, &ev)) {
            handler(&ev, raw, ports);
        }
        if (NearbyStop()) {
            break;
        }
        if (timeSecs > 0) {
            if (halfSecsLeft <= 0) {
                break;
            }
            halfSecsLeft--;
        }
        Delay(25);
    }
}

static void
NearbyPrintKind(struct BAEvent *ev, BOOL raw, BOOL ports)
{
    char realType[MDNS_MAX_TYPE + MDNS_MAX_NAME + 4];

    (void)ports;

    /* Meta events carry the discovered type split as name + "."-tail. */
    BAStrCopy(realType, ev->ev_Inst.si_Name, (LONG)sizeof(realType));
    BAStrAppend(realType, ".", (LONG)sizeof(realType));
    BAStrAppend(realType, ev->ev_Inst.si_Type, (LONG)sizeof(realType));

    if (raw) {
        BAPrintf("  %-26s  %s\n", BATypeFriendly((CONST_STRPTR)realType),
            realType);
    } else {
        BAPrintf("  %s\n", BATypeFriendly((CONST_STRPTR)realType));
    }
}

static void
NearbyPrintInstance(struct BAEvent *ev, BOOL raw, BOOL ports)
{
    struct MDNSServiceInstance *si;

    si = &ev->ev_Inst;

    if (ev->ev_Kind == BA_EV_REMOVE) {
        BAPrintf("  gone: %s\n", si->si_Name);
        return;
    }

    if (!ports) {
        if (raw) {
            BAPrintf("  %-28s  (%s)\n", si->si_Name, si->si_Type);
        } else {
            BAPrintf("  %s\n", si->si_Name);
        }
        return;
    }

    BAPrintf("  %-26s %-20s port %ld",
        si->si_Name,
        (si->si_HostName[0] != '\0') ? (STRPTR)si->si_HostName : (STRPTR)"?",
        (LONG)si->si_Port);
    if (si->si_HostAddr != 0) {
        BAPrintf("  (%ld.%ld.%ld.%ld)",
            BA_IP_ARGS(si->si_HostAddr));
    }
    BAPrintf("\n");
    if (si->si_TXTLength > 0) {
        char txt[MDNS_MAX_TXT + 1];
        LONG tlen;

        tlen = (LONG)si->si_TXTLength;
        if (tlen > MDNS_MAX_TXT) {
            tlen = MDNS_MAX_TXT;
        }
        CopyMem((APTR)si->si_TXT, (APTR)txt, (ULONG)tlen);
        txt[tlen] = '\0';
        BAPrintf("      info: %s\n", txt);
    }
}

/*------------------------------------------------------------------------*/

static int
NearbyRun(struct BARing *ring, CONST_STRPTR word, BOOL all, LONG timeSecs,
    BOOL raw, BOOL ports)
{
    struct Hook hook;
    struct TagItem tags[2];
    MDNSBrowseRef ref;
    CONST_STRPTR type;
    LONG rc;
    BOOL meta;

    meta = (all || word == NULL) ? TRUE : FALSE;
    type = meta ? (CONST_STRPTR)MDNS_TYPE_SERVICES_DNS_SD : BATypeFromAlias(word);

    ref = NULL;
    hook.h_Entry = BA_HOOK_ENTRY(meta ? NearbyMetaHook : NearbyInstanceHook);
    hook.h_SubEntry = NULL;
    hook.h_Data = (APTR)ring;

    tags[0].ti_Tag = MDNS_BrowseRef;
    tags[0].ti_Data = (IPTR)&ref;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;

    rc = BrowseServiceTypeA((STRPTR)type, &hook, tags);
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("browse", rc);
        return 20;
    }

    if (meta) {
        BAPrintf("Kinds of service on your local network");
    } else {
        BAPrintf("Looking for %s", BATypeFriendly(type));
    }
    if (timeSecs > 0) {
        BAPrintf(" (%ld seconds)...\n\n", timeSecs);
    } else {
        BAPrintf(" (Ctrl-C to stop)...\n\n");
    }

    NearbyLoop(ring, timeSecs,
        meta ? NearbyPrintKind : NearbyPrintInstance, raw, ports);

    BAPrintf("\nDone.\n");

    if (ref != NULL) {
        StopBrowse(ref);
    }
    return 0;
}

int
main(void)
{
    struct RDArgs *rda;
    struct BARing *ring;
    IPTR args[ARG_COUNT];
    CONST_STRPTR word;
    LONG timeSecs;
    BOOL all;
    BOOL raw;
    BOOL ports;
    LONG rc;
    int result;
    int i;

    for (i = 0; i < ARG_COUNT; i++) {
        args[i] = 0;
    }

    rda = ReadArgs((STRPTR)NearbyTemplate, args, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "Nearby");
        return 20;
    }

    word = (CONST_STRPTR)args[ARG_TYPE];
    all = (args[ARG_ALL] != 0) ? TRUE : FALSE;
    raw = (args[ARG_RAW] != 0) ? TRUE : FALSE;
    ports = (args[ARG_PORTS] != 0) ? TRUE : FALSE;
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

    result = NearbyRun(ring, word, all, timeSecs, raw, ports);

    BAAppClose();
    BARingFree(ring);
    FreeArgs(rda);
    return result;
}
