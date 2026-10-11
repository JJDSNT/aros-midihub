/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * ba_common.h - shared helpers for bonami.library SDK examples
 *
 * AROS version.  All helpers use native calls only (dos.library PutStr,
 * exec.library VNewRawDoFmt/CopyMem/Forbid).  No C library (printf/stdio/string) is used, so the
 * examples do not link libc for I/O.
 *
 * IMPORTANT - Hook context:
 *   Browse/resolve Hooks passed to bonami.library are invoked by mdnsd, in
 *   its process and not in the client's.  A Hook therefore MUST NOT:
 *     - perform any DOS/stdio output (it would be mdnsd's, not the tool's), or
 *     - call back into bonami.library (RegisterServiceA, BrowseServiceTypeA,
 *       StopBrowse, ...): those post a message to mdnsd and wait for the
 *       reply, but mdnsd is the one running the Hook, so it would deadlock
 *       waiting on itself.
 *   Instead a Hook only copies the event into a BARing with BARingPush(), and
 *   the client's main loop drains it with BARingPop() where output and library
 *   calls are legal.  Hooks are declared with BA_HOOK() because bonami calls
 *   them through CallHookPkt() (A0=hook, A2=obj, A1=msg).
 */

#ifndef BONAMI_SDK_BA_COMMON_H
#define BONAMI_SDK_BA_COMMON_H

#include <exec/types.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>
#include <bonami/mdns.h>

/*
 * A Hook for bonami.library, written once for every AROS architecture:
 *
 *   BA_HOOK(MyHook)
 *   {
 *       BA_HOOK_INIT
 *       ... hook, object and message are its arguments ...
 *       return 0;
 *       BA_HOOK_EXIT
 *   }
 */
#define BA_HOOK(name) \
    AROS_UFH3(static IPTR, name, \
        AROS_UFHA(struct Hook *, hook, A0), \
        AROS_UFHA(APTR, object, A2), \
        AROS_UFHA(APTR, message, A1))
#define BA_HOOK_INIT    AROS_USERFUNC_INIT
#define BA_HOOK_EXIT    AROS_USERFUNC_EXIT
#define BA_HOOK_ENTRY(f) ((HOOKFUNC)(APTR)(f))

/* The four numbers of an address in network byte order, for "%ld.%ld.%ld.%ld". */
#define BA_IP_ARGS(a) \
    (LONG)((UBYTE *)&(a))[0], (LONG)((UBYTE *)&(a))[1], \
    (LONG)((UBYTE *)&(a))[2], (LONG)((UBYTE *)&(a))[3]

/*------------------------------------------------------------------------*/
/* Library / engine lifecycle */

int  BAAppOpen(void);
void BAAppClose(void);
LONG BAAppOpenEngine(void);
void BAAppPrintMDNSError(CONST_STRPTR where, LONG code);

/*------------------------------------------------------------------------*/
/* Native output and string helpers (no stdio / string.h) */

void BAPrintf(CONST_STRPTR fmt, ...);
void BAPrintInstance(CONST_STRPTR action, struct MDNSServiceInstance *si);

LONG BAStrLen(const char *s);
void BAStrCopy(char *d, const char *s, LONG max);
void BAStrAppend(char *d, const char *s, LONG max);
BOOL BAStrEqual(const char *a, const char *b);
LONG BAParseNum(const char *s);

/*------------------------------------------------------------------------*/
/* Event capture ring - bridges engine-task Hooks to the client main loop */

#define BA_EV_ADD       0   /* browse: instance appeared */
#define BA_EV_REMOVE    1   /* browse: instance went away */
#define BA_EV_TYPE      2   /* meta browse: a service type was discovered */
#define BA_EV_RESOLVED  3   /* resolve: instance resolved to host/port */
#define BA_EV_FAILED    4   /* resolve: resolution failed */

#define BA_RING_SIZE    64

struct BAEvent
{
    ULONG                       ev_Kind;
    struct MDNSServiceInstance  ev_Inst;
};

struct BARing
{
    volatile LONG   r_Head;         /* written by Hook (engine task) */
    LONG            r_Tail;         /* advanced by client main loop */
    struct BAEvent  r_Events[BA_RING_SIZE];
};

struct BARing *BARingAlloc(void);
void BARingFree(struct BARing *ring);
void BARingPush(struct BARing *ring, ULONG kind, struct MDNSServiceInstance *si);
int  BARingPop(struct BARing *ring, struct BAEvent *out);

#endif /* BONAMI_SDK_BA_COMMON_H */
