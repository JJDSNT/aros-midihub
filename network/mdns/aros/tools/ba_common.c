/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * ba_common.c - open bonami.library, shared engine, and native helpers for the
 * tools, AROS version.  Uses native calls only: dos.library PutStr for
 * output, exec.library VNewRawDoFmt for formatting, CopyMem/Forbid for the event
 * ring, and AllocMem/FreeMem for storage.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/rawfmt.h>
#include <stdarg.h>

#include <libraries/bonamibase.h>
#include <proto/bonami.h>

#include "ba_common.h"

struct Library *BonamiBase = NULL;

/*------------------------------------------------------------------------*/
/* Library / engine lifecycle */

int
BAAppOpen(void)
{
    BonamiBase = OpenLibrary(BONAMINAME, BONAMI_LIB_VERSION);
    if (BonamiBase == NULL) {
        return 0;
    }
    return 1;
}

void
BAAppClose(void)
{
    if (BonamiBase != NULL) {
        CloseMDNSEngine();
        CloseLibrary(BonamiBase);
        BonamiBase = NULL;
    }
}

LONG
BAAppOpenEngine(void)
{
    if (BonamiBase == NULL) {
        return MDNSERR_BADPARAM;
    }
    return OpenMDNSEngine();
}

/*------------------------------------------------------------------------*/
/* Native formatted output: RawDoFmt() into a buffer, then PutStr(). */

/*
 * RawDoFmt format codes: %ld/%lu/%lx take a LONG (32 bits on every AROS
 * architecture) and %s a pointer; VNewRawDoFmt() reads them from the C
 * argument list, whatever the machine's calling convention.
 */
void
BAPrintf(CONST_STRPTR fmt, ...)
{
    UBYTE buf[512];
    va_list args;

    va_start(args, fmt);
    VNewRawDoFmt(fmt, (VOID_FUNC)RAWFMTFUNC_STRING, buf, args);
    va_end(args);
    PutStr((CONST_STRPTR)buf);
}

void
BAAppPrintMDNSError(CONST_STRPTR where, LONG code)
{
    BAPrintf("%s: MDNS error %ld\n", where, code);
}

void
BAPrintInstance(CONST_STRPTR action, struct MDNSServiceInstance *si)
{
    char txt[MDNS_MAX_TXT + 1];
    const char *host;
    LONG tlen;

    if (si == NULL) {
        return;
    }

    host = (si->si_HostName[0] != '\0') ? si->si_HostName : (const char *)"?";

    BAPrintf("%-7s %-28s %-22s %-18s %5ld  ",
        action, si->si_Name, si->si_Type, host, (LONG)si->si_Port);

    if (si->si_HostAddr != 0) {
        BAPrintf("[%ld.%ld.%ld.%ld] ",
            BA_IP_ARGS(si->si_HostAddr));
    }

    if (si->si_TXTLength > 0) {
        tlen = (LONG)si->si_TXTLength;
        if (tlen > MDNS_MAX_TXT) {
            tlen = MDNS_MAX_TXT;
        }
        CopyMem((APTR)si->si_TXT, (APTR)txt, (ULONG)tlen);
        txt[tlen] = '\0';
        BAPrintf("%s", txt);
    }
    BAPrintf("\n");
}

/*------------------------------------------------------------------------*/
/* Native string helpers (no string.h) */

LONG
BAStrLen(const char *s)
{
    LONG n;

    n = 0;
    if (s != NULL) {
        while (s[n] != '\0') {
            n++;
        }
    }
    return n;
}

void
BAStrCopy(char *d, const char *s, LONG max)
{
    LONG i;

    if (d == NULL || max <= 0) {
        return;
    }
    i = 0;
    if (s != NULL) {
        while (s[i] != '\0' && i < max - 1) {
            d[i] = s[i];
            i++;
        }
    }
    d[i] = '\0';
}

void
BAStrAppend(char *d, const char *s, LONG max)
{
    LONG len;

    len = BAStrLen(d);
    if (len < max) {
        BAStrCopy(d + len, s, max - len);
    }
}

BOOL
BAStrEqual(const char *a, const char *b)
{
    LONG i;

    if (a == NULL || b == NULL) {
        return FALSE;
    }
    i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return FALSE;
        }
        i++;
    }
    return (BOOL)(a[i] == b[i]);
}

LONG
BAParseNum(const char *s)
{
    LONG v;
    LONG i;

    v = 0;
    if (s != NULL) {
        for (i = 0; s[i] >= '0' && s[i] <= '9'; i++) {
            v = v * 10 + (s[i] - '0');
        }
    }
    return v;
}

/*------------------------------------------------------------------------*/
/* Event capture ring */

struct BARing *
BARingAlloc(void)
{
    return (struct BARing *)AllocMem((ULONG)sizeof(struct BARing), MEMF_CLEAR);
}

void
BARingFree(struct BARing *ring)
{
    if (ring != NULL) {
        FreeMem(ring, (ULONG)sizeof(struct BARing));
    }
}

/*
 * Producer side, called from the engine-task Hook.  Forbid() serialises the
 * single-slot advance against the client's BARingPop().  A full ring drops the
 * newest event rather than block the engine.
 */
void
BARingPush(struct BARing *ring, ULONG kind, struct MDNSServiceInstance *si)
{
    LONG next;

    if (ring == NULL || si == NULL) {
        return;
    }
    Forbid();
    next = ring->r_Head + 1;
    if (next >= BA_RING_SIZE) {
        next = 0;
    }
    if (next != ring->r_Tail) {
        ring->r_Events[ring->r_Head].ev_Kind = kind;
        CopyMem((APTR)si, (APTR)&ring->r_Events[ring->r_Head].ev_Inst,
            (ULONG)sizeof(*si));
        ring->r_Head = next;
    }
    Permit();
}

/*
 * Consumer side, called from the client main loop.  Returns 1 and fills *out
 * when an event was available, else 0.
 */
int
BARingPop(struct BARing *ring, struct BAEvent *out)
{
    int got;

    got = 0;
    if (ring == NULL || out == NULL) {
        return 0;
    }
    Forbid();
    if (ring->r_Tail != ring->r_Head) {
        CopyMem((APTR)&ring->r_Events[ring->r_Tail], (APTR)out,
            (ULONG)sizeof(*out));
        ring->r_Tail = ring->r_Tail + 1;
        if (ring->r_Tail >= BA_RING_SIZE) {
            ring->r_Tail = 0;
        }
        got = 1;
    }
    Permit();
    return got;
}
