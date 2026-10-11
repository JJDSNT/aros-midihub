/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_mdns.c - bonami.library hooks, event ring, browse/resolve/share.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <libraries/bonamibase.h>
#include <bonami/mdns.h>
#include <proto/bonami.h>

#include "../tools/ba_common.h"
#include "bonami_mdns.h"
#include "bonami_types.h"

struct BonamiMDNS g_MDNS;

struct Library *BonamiBase;

/*****************************************************************************/
/* Native string helpers (no C library). */

static LONG
bcStrLen(const char *s)
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

static void
bcStrCopy(char *d, const char *s, LONG max)
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

static void
bcStrAppend(char *d, const char *s, LONG max)
{
    LONG len;

    len = bcStrLen(d);
    if (len < max) {
        bcStrCopy(d + len, s, max - len);
    }
}

static void
bcULongToStr(ULONG v, char *buf, LONG max)
{
    char tmp[12];
    LONG i;
    LONG start;
    LONG pos;
    LONG a;
    LONG b;
    char t;

    if (buf == NULL || max <= 0) {
        return;
    }
    i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0) {
            tmp[i++] = (char)('0' + (v % 10));
            v = v / 10;
        }
        start = 0;
        b = i - 1;
        a = start;
        while (a < b) {
            t = tmp[a];
            tmp[a] = tmp[b];
            tmp[b] = t;
            a++;
            b--;
        }
    }
    tmp[i] = '\0';
    pos = 0;
    while (tmp[pos] != '\0' && pos < max - 1) {
        buf[pos] = tmp[pos];
        pos++;
    }
    buf[pos] = '\0';
}

static void
bcFormatIp(ULONG addr, char *buf, LONG max)
{
    char part[12];
    LONG pos;

    if (buf == NULL || max < 8) {
        return;
    }
    pos = 0;
    /* addr is in network byte order: its bytes are the four numbers */
    bcULongToStr(((UBYTE *)&addr)[0], part, (LONG)sizeof(part));
    bcStrCopy(buf, part, max);
    pos = bcStrLen(buf);
    if (pos < max - 1) {
        buf[pos++] = '.';
        buf[pos] = '\0';
    }
    bcULongToStr(((UBYTE *)&addr)[1], part, (LONG)sizeof(part));
    bcStrAppend(buf, part, max);
    bcStrAppend(buf, ".", max);
    bcULongToStr(((UBYTE *)&addr)[2], part, (LONG)sizeof(part));
    bcStrAppend(buf, part, max);
    bcStrAppend(buf, ".", max);
    bcULongToStr(((UBYTE *)&addr)[3], part, (LONG)sizeof(part));
    bcStrAppend(buf, part, max);
}

static BOOL
bcStrEqual(const char *a, const char *b)
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

/*****************************************************************************/
/* Event ring - producer in engine task, consumer in commodity main loop. */

static void
bcRingPush(struct BCRing *ring, ULONG kind, struct MDNSServiceInstance *si)
{
    LONG next;

    if (ring == NULL || si == NULL) {
        return;
    }
    Forbid();
    next = ring->bc_Head + 1;
    if (next >= BC_RING_SIZE) {
        next = 0;
    }
    if (next != ring->bc_Tail) {
        ring->bc_Events[ring->bc_Head].bc_Kind = kind;
        CopyMem((APTR)si, (APTR)&ring->bc_Events[ring->bc_Head].bc_Inst,
            (ULONG)sizeof(*si));
        ring->bc_Head = next;
    }
    Permit();
}

static int
bcRingPop(struct BCRing *ring, struct BCEvent *out)
{
    int got;

    got = 0;
    if (ring == NULL || out == NULL) {
        return 0;
    }
    Forbid();
    if (ring->bc_Tail != ring->bc_Head) {
        CopyMem((APTR)&ring->bc_Events[ring->bc_Tail], (APTR)out,
            (ULONG)sizeof(*out));
        ring->bc_Tail = ring->bc_Tail + 1;
        if (ring->bc_Tail >= BC_RING_SIZE) {
            ring->bc_Tail = 0;
        }
        got = 1;
    }
    Permit();
    return got;
}

/*****************************************************************************/
/* Hooks - engine-task context: ring push only. */

BA_HOOK(BCBrowseHook)
{
    BA_HOOK_INIT
    struct MDNSBrowseMsg *msg;
    struct BonamiMDNS *app;

    (void)object;
    app = (struct BonamiMDNS *)hook->h_Data;
    msg = (struct MDNSBrowseMsg *)message;
    if (msg == NULL || app == NULL) {
        return 0;
    }
    if (msg->bm_Code == MEVENT_ADD) {
        bcRingPush(&app->bc_Ring, BC_EV_ADD, &msg->bm_Instance);
    } else if (msg->bm_Code == MEVENT_REMOVE) {
        bcRingPush(&app->bc_Ring, BC_EV_REMOVE, &msg->bm_Instance);
    }
    return 0;
    BA_HOOK_EXIT
}

BA_HOOK(BCMetaHook)
{
    BA_HOOK_INIT
    struct MDNSBrowseMsg *msg;
    struct BonamiMDNS *app;

    (void)object;
    app = (struct BonamiMDNS *)hook->h_Data;
    msg = (struct MDNSBrowseMsg *)message;
    if (msg == NULL || app == NULL || msg->bm_Code != MEVENT_ADD) {
        return 0;
    }
    bcRingPush(&app->bc_Ring, BC_EV_TYPE, &msg->bm_Instance);
    return 0;
    BA_HOOK_EXIT
}

BA_HOOK(BCResolveHook)
{
    BA_HOOK_INIT
    struct MDNSResolveMsg *msg;
    struct BonamiMDNS *app;

    (void)object;
    app = (struct BonamiMDNS *)hook->h_Data;
    msg = (struct MDNSResolveMsg *)message;
    if (msg == NULL || app == NULL) {
        return 0;
    }
    if (msg->rm_Code == MEVENT_RESOLVED) {
        bcRingPush(&app->bc_Ring, BC_EV_RESOLVED, &msg->rm_Instance);
    } else {
        bcRingPush(&app->bc_Ring, BC_EV_FAILED, &msg->rm_Instance);
    }
    return 0;
    BA_HOOK_EXIT
}

/*****************************************************************************/

static void
bcFormatNearbyLabel(struct BCNearbyEntry *row, struct MDNSServiceInstance *si,
    BOOL isType)
{
    char typebuf[40];

    if (isType) {
        bcStrCopy(typebuf, si->si_Name, (LONG)sizeof(typebuf));
        bcStrAppend(typebuf, ".", (LONG)sizeof(typebuf));
        bcStrAppend(typebuf, si->si_Type, (LONG)sizeof(typebuf));
        bcStrCopy(row->bc_Label, typebuf, (LONG)sizeof(row->bc_Label));
    } else {
        bcStrCopy(row->bc_Label, si->si_Name, (LONG)sizeof(row->bc_Label));
        bcStrAppend(row->bc_Label, "  ", (LONG)sizeof(row->bc_Label));
        bcStrAppend(row->bc_Label, BCTypeFriendly(si->si_Type),
            (LONG)sizeof(row->bc_Label));
        if (si->si_Port != 0) {
            char pbuf[12];

            bcStrAppend(row->bc_Label, "  :", (LONG)sizeof(row->bc_Label));
            bcULongToStr((ULONG)si->si_Port, pbuf, (LONG)sizeof(pbuf));
            bcStrAppend(row->bc_Label, pbuf, (LONG)sizeof(row->bc_Label));
        }
    }
    row->bc_Node.ln_Name = row->bc_Label;
}

static struct BCNearbyEntry *
bcFindNearby(struct List *list, struct MDNSServiceInstance *si, BOOL isType)
{
    struct BCNearbyEntry *row;
    struct Node *n;

    for (n = list->lh_Head; n != NULL && n->ln_Succ != NULL; n = n->ln_Succ) {
        row = (struct BCNearbyEntry *)n;
        if (isType) {
            if (row->bc_IsType &&
                bcStrEqual(row->bc_Inst.si_Name, si->si_Name) &&
                bcStrEqual(row->bc_Inst.si_Type, si->si_Type)) {
                return row;
            }
        } else if (!row->bc_IsType &&
            bcStrEqual(row->bc_Inst.si_Name, si->si_Name) &&
            bcStrEqual(row->bc_Inst.si_Type, si->si_Type)) {
            return row;
        }
    }
    return NULL;
}

static void
bcAddResultLine(struct List *list, CONST_STRPTR text)
{
    struct BCResultLine *line;

    line = (struct BCResultLine *)AllocMem(sizeof(*line), MEMF_CLEAR);
    if (line == NULL) {
        return;
    }
    bcStrCopy(line->bc_Text, (const char *)text, (LONG)sizeof(line->bc_Text));
    line->bc_Node.ln_Name = line->bc_Text;
    AddTail(list, &line->bc_Node);
}

static void
bcApplyEvent(struct BonamiMDNS *app, struct BCEvent *ev)
{
    struct BCNearbyEntry *row;
    struct MDNSServiceInstance *si;
    char buf[80];
    LONG tlen;

    si = &ev->bc_Inst;

    if (ev->bc_Kind == BC_EV_ADD || ev->bc_Kind == BC_EV_TYPE) {
        row = bcFindNearby(&app->bc_Nearby, si,
            (BOOL)(ev->bc_Kind == BC_EV_TYPE));
        if (row != NULL) {
            CopyMem((APTR)si, (APTR)&row->bc_Inst, (ULONG)sizeof(*si));
            bcFormatNearbyLabel(row, si, row->bc_IsType);
            return;
        }
        row = (struct BCNearbyEntry *)AllocMem(sizeof(*row), MEMF_CLEAR);
        if (row == NULL) {
            return;
        }
        row->bc_IsType = (BOOL)(ev->bc_Kind == BC_EV_TYPE);
        CopyMem((APTR)si, (APTR)&row->bc_Inst, (ULONG)sizeof(*si));
        bcFormatNearbyLabel(row, si, row->bc_IsType);
        AddTail(&app->bc_Nearby, &row->bc_Node);
        return;
    }

    if (ev->bc_Kind == BC_EV_REMOVE) {
        row = bcFindNearby(&app->bc_Nearby, si, FALSE);
        if (row != NULL) {
            Remove(&row->bc_Node);
            FreeMem(row, sizeof(*row));
        }
        return;
    }

    if (ev->bc_Kind == BC_EV_RESOLVED || ev->bc_Kind == BC_EV_FAILED) {
        BCMdnsClearResults(app);
        if (ev->bc_Kind == BC_EV_FAILED) {
            bcAddResultLine(&app->bc_Results, "Could not resolve this service.");
            return;
        }
        bcStrCopy(buf, "Name: ", (LONG)sizeof(buf));
        bcStrAppend(buf, si->si_Name, (LONG)sizeof(buf));
        bcAddResultLine(&app->bc_Results, buf);
        if (si->si_HostName[0] != '\0') {
            bcStrCopy(buf, "Host: ", (LONG)sizeof(buf));
            bcStrAppend(buf, si->si_HostName, (LONG)sizeof(buf));
            bcAddResultLine(&app->bc_Results, buf);
        }
        if (si->si_HostAddr != 0) {
            char ip[20];

            bcStrCopy(buf, "IP: ", (LONG)sizeof(buf));
            bcFormatIp(si->si_HostAddr, ip, (LONG)sizeof(ip));
            bcStrAppend(buf, ip, (LONG)sizeof(buf));
            bcAddResultLine(&app->bc_Results, buf);
        }
        bcStrCopy(buf, "Port: ", (LONG)sizeof(buf));
        {
            char pbuf[12];

            bcULongToStr((ULONG)si->si_Port, pbuf, (LONG)sizeof(pbuf));
            bcStrAppend(buf, pbuf, (LONG)sizeof(buf));
        }
        bcAddResultLine(&app->bc_Results, buf);
        if (si->si_TXTLength > 0) {
            tlen = (LONG)si->si_TXTLength;
            if (tlen > MDNS_MAX_TXT) {
                tlen = MDNS_MAX_TXT;
            }
            bcStrCopy(buf, "Info: ", (LONG)sizeof(buf));
            bcStrAppend(buf, (const char *)si->si_TXT, (LONG)sizeof(buf));
            bcAddResultLine(&app->bc_Results, buf);
        }
    }
}

/*****************************************************************************/

BOOL
BCMdnsOpen(VOID)
{
    LONG rc;

    BonamiBase = OpenLibrary(BONAMINAME, BONAMI_LIB_VERSION);
    if (BonamiBase == NULL) {
        return FALSE;
    }
    rc = OpenMDNSEngine();
    if (rc != MDNS_OK) {
        CloseLibrary(BonamiBase);
        BonamiBase = NULL;
        return FALSE;
    }
    return TRUE;
}

VOID
BCMdnsClose(VOID)
{
    if (BonamiBase != NULL) {
        CloseMDNSEngine();
        CloseLibrary(BonamiBase);
        BonamiBase = NULL;
    }
}

VOID
BCMdnsInit(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    app->bc_Ring.bc_Head = 0;
    app->bc_Ring.bc_Tail = 0;
    NewList(&app->bc_Nearby);
    NewList(&app->bc_Results);
    app->bc_BrowseHook.h_Entry = BA_HOOK_ENTRY(BCBrowseHook);
    app->bc_BrowseHook.h_SubEntry = NULL;
    app->bc_BrowseHook.h_Data = (APTR)app;
    app->bc_MetaHook.h_Entry = BA_HOOK_ENTRY(BCMetaHook);
    app->bc_MetaHook.h_SubEntry = NULL;
    app->bc_MetaHook.h_Data = (APTR)app;
    app->bc_ResolveHook.h_Entry = BA_HOOK_ENTRY(BCResolveHook);
    app->bc_ResolveHook.h_SubEntry = NULL;
    app->bc_ResolveHook.h_Data = (APTR)app;
    app->bc_BrowseRef = NULL;
    app->bc_MetaRef = NULL;
    app->bc_ShareRef = NULL;
    app->bc_Browsing = FALSE;
    app->bc_MetaMode = FALSE;
    app->bc_Sharing = FALSE;
}

static void
bcFreeList(struct List *list, ULONG size)
{
    struct Node *n;

    while ((n = list->lh_Head) != NULL && n->ln_Succ != NULL) {
        Remove(n);
        FreeMem(n, size);
    }
}

VOID
BCMdnsFree(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    BCMdnsStopBrowse(app);
    BCMdnsUnshare(app);
    bcFreeList(&app->bc_Nearby, sizeof(struct BCNearbyEntry));
    bcFreeList(&app->bc_Results, sizeof(struct BCResultLine));
}

VOID
BCMdnsClearNearby(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    bcFreeList(&app->bc_Nearby, sizeof(struct BCNearbyEntry));
}

VOID
BCMdnsClearResults(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    bcFreeList(&app->bc_Results, sizeof(struct BCResultLine));
}

LONG
BCMdnsStartBrowse(struct BonamiMDNS *app, CONST_STRPTR type, BOOL meta)
{
    struct TagItem tags[2];
    struct Hook *hook;
    MDNSBrowseRef *refp;
    LONG rc;

    if (app == NULL || BonamiBase == NULL) {
        return MDNSERR_BADPARAM;
    }

    BCMdnsStopBrowse(app);
    BCMdnsClearNearby(app);

    if (meta) {
        hook = &app->bc_MetaHook;
        refp = &app->bc_MetaRef;
        app->bc_MetaMode = TRUE;
        type = (CONST_STRPTR)MDNS_TYPE_SERVICES_DNS_SD;
    } else {
        hook = &app->bc_BrowseHook;
        refp = &app->bc_BrowseRef;
        app->bc_MetaMode = FALSE;
    }

    *refp = NULL;
    tags[0].ti_Tag = MDNS_BrowseRef;
    tags[0].ti_Data = (IPTR)refp;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;

    rc = BrowseServiceTypeA((STRPTR)type, hook, tags);
    if (rc == MDNS_OK) {
        app->bc_Browsing = TRUE;
    }
    return rc;
}

VOID
BCMdnsStopBrowse(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    if (app->bc_BrowseRef != NULL) {
        StopBrowse(app->bc_BrowseRef);
        app->bc_BrowseRef = NULL;
    }
    if (app->bc_MetaRef != NULL) {
        StopBrowse(app->bc_MetaRef);
        app->bc_MetaRef = NULL;
    }
    app->bc_Browsing = FALSE;
    app->bc_MetaMode = FALSE;
}

LONG
BCMdnsResolve(struct BonamiMDNS *app, CONST_STRPTR name, CONST_STRPTR type)
{
    LONG rc;

    if (app == NULL || BonamiBase == NULL || name == NULL || type == NULL) {
        return MDNSERR_BADPARAM;
    }
    BCMdnsClearResults(app);
    rc = ResolveServiceA((STRPTR)name, (STRPTR)type, &app->bc_ResolveHook, NULL);
    return rc;
}

LONG
BCMdnsShare(struct BonamiMDNS *app, CONST_STRPTR name, CONST_STRPTR type,
    UWORD port, CONST_STRPTR txt)
{
    struct TagItem tags[8];
    LONG nt;
    LONG rc;

    if (app == NULL || BonamiBase == NULL) {
        return MDNSERR_BADPARAM;
    }
    BCMdnsUnshare(app);

    nt = 0;
    tags[nt].ti_Tag = MDNS_ServiceName;
    tags[nt].ti_Data = (IPTR)name;
    nt++;
    tags[nt].ti_Tag = MDNS_ServiceType;
    tags[nt].ti_Data = (IPTR)type;
    nt++;
    tags[nt].ti_Tag = MDNS_Port;
    tags[nt].ti_Data = (IPTR)port;
    nt++;
    if (txt != NULL && txt[0] != '\0') {
        tags[nt].ti_Tag = MDNS_TXTRecord;
        tags[nt].ti_Data = (IPTR)txt;
        nt++;
    }
    tags[nt].ti_Tag = MDNS_RegisterRef;
    tags[nt].ti_Data = (IPTR)&app->bc_ShareRef;
    nt++;
    tags[nt].ti_Tag = TAG_END;
    tags[nt].ti_Data = 0;

    rc = RegisterServiceA(tags);
    if (rc == MDNS_OK) {
        app->bc_Sharing = TRUE;
    }
    return rc;
}

VOID
BCMdnsUnshare(struct BonamiMDNS *app)
{
    if (app == NULL) {
        return;
    }
    if (app->bc_ShareRef != NULL) {
        DeregisterService(app->bc_ShareRef);
        app->bc_ShareRef = NULL;
    }
    app->bc_Sharing = FALSE;
}

LONG
BCMdnsDrain(struct BonamiMDNS *app)
{
    struct BCEvent ev;
    LONG count;

    if (app == NULL) {
        return 0;
    }
    count = 0;
    while (bcRingPop(&app->bc_Ring, &ev)) {
        bcApplyEvent(app, &ev);
        count++;
    }
    return count;
}

struct BCNearbyEntry *
BCMdnsNearbySelected(struct BonamiMDNS *app, UWORD index)
{
    struct Node *n;
    UWORD i;

    if (app == NULL) {
        return NULL;
    }
    n = app->bc_Nearby.lh_Head;
    i = 0;
    while (n != NULL && n->ln_Succ != NULL) {
        if (i == index) {
            return (struct BCNearbyEntry *)n;
        }
        i++;
        n = n->ln_Succ;
    }
    return NULL;
}
