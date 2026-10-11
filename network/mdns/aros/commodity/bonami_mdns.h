/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_mdns.h - bonami.library integration for the Bonami commodity.
 *
 * Hooks run in mdns.task and only push into the event ring.  The commodity
 * main loop (or IDCMP_INTUITICK handler) drains the ring and updates gadgets.
 */

#ifndef BONAMI_COMMODITY_MDNS_H
#define BONAMI_COMMODITY_MDNS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <utility/hooks.h>
#include <bonami/mdns.h>

#define BC_RING_SIZE    64
#define BC_MAX_NEARBY   128
#define BC_MAX_RESULT   8

#define BC_EV_ADD       0
#define BC_EV_REMOVE    1
#define BC_EV_TYPE      2
#define BC_EV_RESOLVED  3
#define BC_EV_FAILED    4

struct BCEvent
{
    ULONG                       bc_Kind;
    struct MDNSServiceInstance  bc_Inst;
};

struct BCRing
{
    volatile LONG   bc_Head;
    LONG            bc_Tail;
    struct BCEvent  bc_Events[BC_RING_SIZE];
};

/*
 * One row in the Nearby listview.  ln_Name is the formatted label GTLV shows;
 * bc_Inst holds the full instance for Handshake pre-fill.
 */
struct BCNearbyEntry
{
    struct Node                 bc_Node;
    char                        bc_Label[96];
    struct MDNSServiceInstance  bc_Inst;
    BOOL                        bc_IsType;
};

struct BCResultLine
{
    struct Node bc_Node;
    char        bc_Text[80];
};

struct BonamiMDNS
{
    struct BCRing           bc_Ring;
    struct List             bc_Nearby;
    struct List             bc_Results;
    struct Hook             bc_BrowseHook;
    struct Hook             bc_MetaHook;
    struct Hook             bc_ResolveHook;
    MDNSBrowseRef           bc_BrowseRef;
    MDNSBrowseRef           bc_MetaRef;
    MDNSRegisterRef         bc_ShareRef;
    BOOL                    bc_Browsing;
    BOOL                    bc_MetaMode;
    BOOL                    bc_Sharing;
};

extern struct BonamiMDNS g_MDNS;

BOOL BCMdnsOpen(VOID);
VOID BCMdnsClose(VOID);
VOID BCMdnsInit(struct BonamiMDNS *app);
VOID BCMdnsFree(struct BonamiMDNS *app);

LONG BCMdnsStartBrowse(struct BonamiMDNS *app, CONST_STRPTR type, BOOL meta);
VOID BCMdnsStopBrowse(struct BonamiMDNS *app);
LONG BCMdnsResolve(struct BonamiMDNS *app, CONST_STRPTR name, CONST_STRPTR type);
LONG BCMdnsShare(struct BonamiMDNS *app, CONST_STRPTR name, CONST_STRPTR type,
    UWORD port, CONST_STRPTR txt);
VOID BCMdnsUnshare(struct BonamiMDNS *app);

/* Drain hook ring into Nearby/Results lists; returns events processed. */
LONG BCMdnsDrain(struct BonamiMDNS *app);

VOID BCMdnsClearNearby(struct BonamiMDNS *app);
VOID BCMdnsClearResults(struct BonamiMDNS *app);

struct BCNearbyEntry *BCMdnsNearbySelected(struct BonamiMDNS *app, UWORD index);

#endif /* BONAMI_COMMODITY_MDNS_H */
