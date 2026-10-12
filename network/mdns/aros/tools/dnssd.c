/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * dnssd - an AmigaOS DNS-SD command-line tool in the spirit of Apple's
 *         `dns-sd` / Avahi's `avahi-browse`, built on bonami.library.
 *
 * The command surface mirrors Apple's dns-sd(1) as closely as bonami.library
 * allows:
 *
 *   dnssd -E                              list domains for registering services
 *   dnssd -F                              list domains for browsing services
 *   dnssd -R name type domain port [k=v ...]   register (advertise) a service
 *   dnssd -B type [domain]               browse for instances of a service type
 *   dnssd -L name type [domain]          look up (resolve) one instance
 *   dnssd -Z type [domain]               browse, output in zone-file format
 *   dnssd -V                             show the bonami.library version
 *   dnssd -h                             this help
 *
 * The domain argument (where dns-sd accepts one) may be omitted or given as "."
 * to mean "pick a sensible default", which on bonami is always the multicast
 * ".local" domain - the only domain the engine implements.
 *
 * -R / -B / -Z run until Ctrl-C; -L retries until the instance resolves or
 * Ctrl-C.  Examples:
 *   dnssd -B _http._tcp
 *   dnssd -B _services._dns-sd._udp        (enumerate the service types present)
 *   dnssd -L "My Mac" _companion-link._tcp
 *   dnssd -R "AmigaShare" _http._tcp . 80 path=/ vers=1
 *
 * Three more modes use what the AROS bonami.library adds to the Bonami API
 * (MDNS_HostAddr, QueryRecordA, LookupHostA):
 *   dnssd -P name type domain port host ip [k=v ...]   register for another host
 *   dnssd -q name rrtype                  ask for any record and print answers
 *   dnssd -G v4 hostname                  look up a host's address
 * rrtype is A, PTR, SRV, TXT, ANY or a number.
 *
 * Native AmigaOS only (see ba_common.h).  Browse/resolve Hooks run in the
 * bonami engine task and merely push events into a BARing; the main loop drains
 * and prints them, and issues all further library calls, where that is safe.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

#include <libraries/bonamibase.h>
#include <bonami/mdns.h>
#include <proto/bonami.h>

#include "ba_common.h"

/*------------------------------------------------------------------------*/
/* Hooks - engine-task context: only push into the ring. */

BA_HOOK(DSBrowseHook)
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

BA_HOOK(DSMetaHook)
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

BA_HOOK(DSResolveHook)
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

/*------------------------------------------------------------------------*/
/* Helpers */

static int
DSStop(void)
{
    return (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? 1 : 0;
}

/*
 * bonami implements only the multicast ".local" domain.  Treat dns-sd's "pick a
 * default" spellings ("", ".") and the explicit "local"/"local." as valid; warn
 * (but proceed against .local) for anything else so scripts are not silently
 * misled.
 */
static int
DSDomainLocal(CONST_STRPTR domain)
{
    if (domain == NULL) {
        return 1;
    }
    if (BAStrEqual((const char *)domain, "") ||
        BAStrEqual((const char *)domain, ".") ||
        BAStrEqual((const char *)domain, "local") ||
        BAStrEqual((const char *)domain, "local.")) {
        return 1;
    }
    return 0;
}

static int
DSIsMetaType(CONST_STRPTR type)
{
    return BAStrEqual((const char *)type,
        (const char *)MDNS_TYPE_SERVICES_DNS_SD) ? 1 : 0;
}

/*------------------------------------------------------------------------*/
/* -E / -F : domain enumeration.  bonami only serves the link-local mDNS
 * domain, so both lists contain exactly "local". */

static int
DSEnumDomains(CONST_STRPTR which)
{
    BAPrintf("Browse domains for %s:\n", which);
    BAPrintf("  Added     local\n");
    BAPrintf("\n(bonami.library serves only the multicast .local domain)\n");
    return 0;
}

/*------------------------------------------------------------------------*/
/* -V : library version. */

static int
DSVersion(void)
{
    struct Library *lib;

    lib = (struct Library *)BonamiBase;
    if (lib == NULL) {
        BAPrintf("bonami.library not open\n");
        return 20;
    }
    BAPrintf("%s %ld.%ld\n", BONAMINAME,
        (LONG)lib->lib_Version, (LONG)lib->lib_Revision);
    return 0;
}

/*------------------------------------------------------------------------*/
/* -B / -Z : browse.  meta==1 selects service-type enumeration; zone==1 selects
 * zone-file style output instead of the columnar browse listing. */

static void
DSPrintZoneAdd(struct MDNSServiceInstance *si)
{
    /* Approximate dns-sd -Z output: a PTR line plus SRV/TXT when known. */
    BAPrintf("%s.local.\tPTR\t%s.%s.local.\n",
        si->si_Type, si->si_Name, si->si_Type);
    if (si->si_Port != 0 || si->si_HostName[0] != '\0') {
        BAPrintf("%s.%s.local.\tSRV\t0 0 %ld %s.local.\n",
            si->si_Name, si->si_Type, (LONG)si->si_Port,
            (si->si_HostName[0] != '\0') ? (STRPTR)si->si_HostName : (STRPTR)"?");
    }
    if (si->si_TXTLength > 0) {
        char txt[MDNS_MAX_TXT + 1];
        LONG tlen;

        tlen = (LONG)si->si_TXTLength;
        if (tlen > MDNS_MAX_TXT) {
            tlen = MDNS_MAX_TXT;
        }
        CopyMem((APTR)si->si_TXT, (APTR)txt, (ULONG)tlen);
        txt[tlen] = '\0';
        BAPrintf("%s.%s.local.\tTXT\t\"%s\"\n", si->si_Name, si->si_Type, txt);
    }
}

static int
DSBrowse(struct BARing *ring, CONST_STRPTR type, CONST_STRPTR domain, int zone)
{
    struct Hook hook;
    struct TagItem tags[2];
    struct BAEvent ev;
    char typeName[80];
    MDNSBrowseRef ref;
    LONG rc;
    int meta;

    if (!DSDomainLocal(domain)) {
        BAPrintf("(note: domain \"%s\" not served; using local)\n", domain);
    }

    meta = DSIsMetaType(type);
    ref = NULL;
    hook.h_Entry = BA_HOOK_ENTRY(meta ? DSMetaHook : DSBrowseHook);
    hook.h_SubEntry = NULL;
    hook.h_Data = (APTR)ring;

    tags[0].ti_Tag = MDNS_BrowseRef;
    tags[0].ti_Data = (IPTR)&ref;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;

    rc = BrowseServiceTypeA((STRPTR)type, &hook, tags);
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("BrowseServiceTypeA", rc);
        return 20;
    }

    if (meta) {
        BAPrintf("Enumerating service types (Ctrl-C to stop)...\n\n");
    } else if (zone) {
        BAPrintf("; browsing %s (Ctrl-C to stop)\n\n", type);
    } else {
        BAPrintf("Browsing %s (Ctrl-C to stop)...\n\n", type);
        BAPrintf("%-7s %-28s %-22s %-18s %5s  TXT\n",
            "Event", "Instance", "Type", "Host", "Port");
    }

    (void)SetSignal(0L, SIGBREAKF_CTRL_C);
    while (!DSStop()) {
        while (BARingPop(ring, &ev)) {
            if (ev.ev_Kind == BA_EV_TYPE) {
                BAStrCopy(typeName, ev.ev_Inst.si_Name, (LONG)sizeof(typeName));
                BAStrAppend(typeName, ".", (LONG)sizeof(typeName));
                BAStrAppend(typeName, ev.ev_Inst.si_Type,
                    (LONG)sizeof(typeName));
                BAPrintf("TYPE    %s\n", typeName);
            } else if (ev.ev_Kind == BA_EV_ADD) {
                if (zone) {
                    DSPrintZoneAdd(&ev.ev_Inst);
                } else {
                    BAPrintInstance("ADD", &ev.ev_Inst);
                }
            } else if (ev.ev_Kind == BA_EV_REMOVE) {
                if (zone) {
                    BAPrintf("; remove %s.%s.local.\n",
                        ev.ev_Inst.si_Name, ev.ev_Inst.si_Type);
                } else {
                    BAPrintInstance("REMOVE", &ev.ev_Inst);
                }
            }
        }
        Delay(25);
    }
    BAPrintf("\n(Ctrl-C)\n");

    if (ref != NULL) {
        StopBrowse(ref);
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* -L : resolve one instance. */

static int
DSResolve(struct BARing *ring, CONST_STRPTR name, CONST_STRPTR type,
    CONST_STRPTR domain)
{
    struct Hook hook;
    struct BAEvent ev;
    LONG rc;
    LONG tries;

    if (!DSDomainLocal(domain)) {
        BAPrintf("(note: domain \"%s\" not served; using local)\n", domain);
    }

    hook.h_Entry = BA_HOOK_ENTRY(DSResolveHook);
    hook.h_SubEntry = NULL;
    hook.h_Data = (APTR)ring;

    BAPrintf("Resolving \"%s\" %s (Ctrl-C to stop)...\n", name, type);

    (void)SetSignal(0L, SIGBREAKF_CTRL_C);
    for (tries = 0; tries < 60 && !DSStop(); tries++) {
        /*
         * ResolveServiceA answers from cache and, on a miss, primes a query and
         * returns NOTFOUND (no later async callback), so we poll: each call
         * both checks the cache and re-issues the query until the instance's
         * SRV/A/TXT records have arrived and populated the cache.
         */
        rc = ResolveServiceA((STRPTR)name, (STRPTR)type, &hook, NULL);
        if (rc == MDNS_OK) {
            while (BARingPop(ring, &ev)) {
                if (ev.ev_Kind == BA_EV_RESOLVED) {
                    BAPrintInstance("RESOLVED", &ev.ev_Inst);
                    return 0;
                }
            }
        }
        Delay(50);
    }

    BAPrintf("Not resolved.\n");
    return 20;
}

/*------------------------------------------------------------------------*/
/* -R : register a service. */

static int
DSRegister(CONST_STRPTR name, CONST_STRPTR type, CONST_STRPTR domain,
    UWORD port, CONST_STRPTR proxyHost, ULONG proxyAddr,
    int txtArgc, char **txtArgv)
{
    struct TagItem tags[10];
    char txt[MDNS_MAX_TXT + 1];
    MDNSRegisterRef ref;
    LONG rc;
    int i;
    int nt;

    if (!DSDomainLocal(domain)) {
        BAPrintf("(note: domain \"%s\" not served; registering in local)\n",
            domain);
    }

    /* Each argument is one item of the text. The library takes them as
     * one string with spaces between, so a space or a backslash that is
     * part of an item gets a backslash before it. */
    txt[0] = '\0';
    {
        LONG out = 0;

        for (i = 0; i < txtArgc; i++) {
            const char *arg = txtArgv[i];

            if (i > 0 && out + 1 < (LONG)sizeof(txt)) {
                txt[out++] = ' ';
            }
            for (; *arg != '\0' && out + 2 < (LONG)sizeof(txt); arg++) {
                if (*arg == ' ' || *arg == '\\') {
                    txt[out++] = '\\';
                }
                txt[out++] = *arg;
            }
        }
        txt[out] = '\0';
    }

    ref = NULL;
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
    if (txt[0] != '\0') {
        tags[nt].ti_Tag = MDNS_TXTRecord;
        tags[nt].ti_Data = (IPTR)txt;
        nt++;
    }
    if (proxyHost != NULL) {
        tags[nt].ti_Tag = MDNS_HostName;
        tags[nt].ti_Data = (IPTR)proxyHost;
        nt++;
        tags[nt].ti_Tag = MDNS_HostAddr;
        tags[nt].ti_Data = (IPTR)proxyAddr;
        nt++;
    }
    tags[nt].ti_Tag = MDNS_RegisterRef;
    tags[nt].ti_Data = (IPTR)&ref;
    nt++;
    tags[nt].ti_Tag = TAG_END;
    tags[nt].ti_Data = 0;

    rc = RegisterServiceA(tags);
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("RegisterServiceA", rc);
        return 20;
    }

    BAPrintf("Registered \"%s\" %s port %lu", name, type, (LONG)port);
    if (txt[0] != '\0') {
        BAPrintf(" txt=\"%s\"", txt);
    }
    BAPrintf("\nPress Ctrl-C to deregister...\n");
    Wait(SIGBREAKF_CTRL_C);

    if (ref != NULL) {
        DeregisterService(ref);
    }
    BAPrintf("Deregistered.\n");
    return 0;
}

/*------------------------------------------------------------------------*/

/* -G : a host name's address. */

static int
DSLookup(CONST_STRPTR host)
{
    ULONG addr = 0;
    LONG rc;

    rc = LookupHostA((STRPTR)host, &addr, NULL);
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("LookupHostA", rc);
        return rc == MDNSERR_TIMEOUT ? 5 : 20;
    }
    BAPrintf("%s  %ld.%ld.%ld.%ld\n", host, BA_IP_ARGS(addr));
    return 0;
}

/*------------------------------------------------------------------------*/
/* -q : any record.  The Hook keeps the newest records in a small ring of its
 * own; the main loop prints them. */

#define DS_RECORDS 16

struct DSRecordRing
{
    volatile LONG       head;
    LONG                tail;
    struct MDNSRecordMsg records[DS_RECORDS];
};

BA_HOOK(DSRecordHook)
{
    BA_HOOK_INIT
    struct DSRecordRing *ring = (struct DSRecordRing *)hook->h_Data;
    LONG next;

    (void)object;
    Forbid();
    next = (ring->head + 1) % DS_RECORDS;
    if (next != ring->tail) {
        CopyMem(message, &ring->records[ring->head], sizeof(ring->records[0]));
        ring->head = next;
    }
    Permit();
    return 0;
    BA_HOOK_EXIT
}

static UWORD
DSRecordType(const char *word)
{
    if (BAStrEqual(word, "A")) return 1;
    if (BAStrEqual(word, "PTR")) return 12;
    if (BAStrEqual(word, "TXT")) return 16;
    if (BAStrEqual(word, "AAAA")) return 28;
    if (BAStrEqual(word, "SRV")) return 33;
    if (BAStrEqual(word, "ANY")) return 255;
    return (UWORD)BAParseNum(word);
}

/* Prints an uncompressed DNS name and returns the offset after it. */
static LONG
DSPrintName(const UBYTE *data, LONG pos, LONG length)
{
    char label[64];
    LONG len;

    while (pos < length && (len = data[pos]) != 0 && len < 64 &&
           pos + 1 + len <= length) {
        CopyMem((APTR)&data[pos + 1], label, (ULONG)len);
        label[len] = '\0';
        BAPrintf("%s.", label);
        pos += 1 + len;
    }
    return pos + 1;
}

static void
DSPrintRecord(struct MDNSRecordMsg *r)
{
    char text[256];
    LONG pos, len, i;

    BAPrintf("%s  type %ld  ttl %ld  ", r->qm_Name, (LONG)r->qm_Type,
        (LONG)r->qm_TTL);
    switch (r->qm_Type) {
    case 1:
        if (r->qm_Length == 4) {
            BAPrintf("%ld.%ld.%ld.%ld", (LONG)r->qm_Data[0],
                (LONG)r->qm_Data[1], (LONG)r->qm_Data[2], (LONG)r->qm_Data[3]);
        }
        break;
    case 12:
        DSPrintName(r->qm_Data, 0, r->qm_Length);
        break;
    case 33:
        if (r->qm_Length > 6) {
            BAPrintf("%ld %ld %ld ",
                (LONG)((r->qm_Data[0] << 8) | r->qm_Data[1]),
                (LONG)((r->qm_Data[2] << 8) | r->qm_Data[3]),
                (LONG)((r->qm_Data[4] << 8) | r->qm_Data[5]));
            DSPrintName(r->qm_Data, 6, r->qm_Length);
        }
        break;
    case 16:
        for (pos = 0; pos < r->qm_Length; pos += 1 + len) {
            len = r->qm_Data[pos];
            if (pos + 1 + len > r->qm_Length) {
                break;
            }
            CopyMem(&r->qm_Data[pos + 1], text, (ULONG)len);
            text[len] = '\0';
            BAPrintf("\"%s\" ", text);
        }
        break;
    default:
        for (i = 0; i < r->qm_Length && i < 32; i++) {
            BAPrintf("%02lx", (LONG)r->qm_Data[i]);
        }
        break;
    }
    BAPrintf("\n");
}

static int
DSQuery(CONST_STRPTR name, CONST_STRPTR typeWord)
{
    struct DSRecordRing *ring;
    struct MDNSRecordMsg record;
    struct TagItem tags[2];
    struct Hook hook;
    MDNSQueryRef ref = NULL;
    UWORD type;
    LONG rc;
    BOOL got;

    type = DSRecordType((const char *)typeWord);
    if (type == 0) {
        BAPrintf("dnssd -q: unknown record type \"%s\"\n", typeWord);
        return 5;
    }
    ring = (struct DSRecordRing *)AllocMem(sizeof(*ring), MEMF_CLEAR);
    if (ring == NULL) {
        BAPrintf("out of memory\n");
        return 20;
    }
    hook.h_Entry = BA_HOOK_ENTRY(DSRecordHook);
    hook.h_SubEntry = NULL;
    hook.h_Data = (APTR)ring;
    tags[0].ti_Tag = MDNS_QueryRef;
    tags[0].ti_Data = (IPTR)&ref;
    tags[1].ti_Tag = TAG_END;
    tags[1].ti_Data = 0;

    rc = QueryRecordA((STRPTR)name, type, &hook, tags);
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("QueryRecordA", rc);
        FreeMem(ring, sizeof(*ring));
        return 20;
    }
    BAPrintf("Asking for %s %s (Ctrl-C to stop)...\n\n", name, typeWord);

    (void)SetSignal(0L, SIGBREAKF_CTRL_C);
    while (!DSStop()) {
        do {
            got = FALSE;
            Forbid();
            if (ring->tail != ring->head) {
                CopyMem(&ring->records[ring->tail], &record, sizeof(record));
                ring->tail = (ring->tail + 1) % DS_RECORDS;
                got = TRUE;
            }
            Permit();
            if (got) {
                DSPrintRecord(&record);
            }
        } while (got);
        Delay(25);
    }

    /* After this the Hook is not called again and the ring may go. */
    StopQuery(ref);
    FreeMem(ring, sizeof(*ring));
    return 0;
}

/* "192.168.1.20" as an address in network byte order; 0 if it is not one. */
static ULONG
DSParseAddr(const char *s)
{
    ULONG addr = 0;
    UBYTE *b = (UBYTE *)&addr;
    LONG part, value;

    for (part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        for (value = 0; *s >= '0' && *s <= '9'; s++) {
            value = value * 10 + (*s - '0');
        }
        if (value > 255 || (part < 3 && *s++ != '.')) {
            return 0;
        }
        b[part] = (UBYTE)value;
    }
    return *s == '\0' ? addr : 0;
}

static void
DSUsage(void)
{
    BAPrintf("dnssd - AmigaOS DNS-SD tool (bonami.library)\n\n");
    BAPrintf("  dnssd -E                      list registration domains\n");
    BAPrintf("  dnssd -F                      list browsing domains\n");
    BAPrintf("  dnssd -R name type dom port [k=v ...]  register a service\n");
    BAPrintf("  dnssd -B type [domain]        browse a service type\n");
    BAPrintf("  dnssd -L name type [domain]   look up (resolve) an instance\n");
    BAPrintf("  dnssd -Z type [domain]        browse, zone-file output\n");
    BAPrintf("  dnssd -P name type dom port host ip [k=v ...]  register for another host\n");
    BAPrintf("  dnssd -q name rrtype          ask for any record\n");
    BAPrintf("  dnssd -G v4 hostname          look up a host's address\n");
    BAPrintf("  dnssd -V                      show library version\n");
    BAPrintf("  dnssd -h                      this help\n\n");
    BAPrintf("Examples:\n");
    BAPrintf("  dnssd -B _http._tcp\n");
    BAPrintf("  dnssd -B _services._dns-sd._udp     (list service types)\n");
    BAPrintf("  dnssd -L \"My Mac\" _companion-link._tcp\n");
    BAPrintf("  dnssd -R AmigaShare _http._tcp . 80 path=/ vers=1\n");
}

/*
 * dns-sd modes that need no engine (-E/-F/-V/-h) are handled before opening the
 * library; the rest open the shared engine and run until Ctrl-C.
 */
int
main(int argc, char **argv)
{
    struct BARing *ring;
    CONST_STRPTR mode;
    LONG rc;
    int result;

    if (argc < 2) {
        DSUsage();
        return 5;
    }
    mode = (CONST_STRPTR)argv[1];

    if (BAStrEqual(argv[1], "-h") || BAStrEqual(argv[1], "-?")) {
        DSUsage();
        return 0;
    }

    /* Validate argument counts up front. */
    if (BAStrEqual(argv[1], "-B") && argc < 3) {
        BAPrintf("dnssd -B needs a <type>\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-Z") && argc < 3) {
        BAPrintf("dnssd -Z needs a <type>\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-L") && argc < 4) {
        BAPrintf("dnssd -L needs <name> <type> [domain]\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-R") && argc < 6) {
        BAPrintf("dnssd -R needs <name> <type> <domain> <port>\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-P") &&
        (argc < 8 || DSParseAddr(argv[7]) == 0)) {
        BAPrintf("dnssd -P needs <name> <type> <domain> <port> <host> <ip>\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-q") && argc < 4) {
        BAPrintf("dnssd -q needs <name> <rrtype>\n");
        return 5;
    }
    if (BAStrEqual(argv[1], "-G") && argc < 4) {
        BAPrintf("dnssd -G needs v4 <hostname>\n");
        return 5;
    }

    if (!BAAppOpen()) {
        BAPrintf("OpenLibrary(\"%s\") failed\n", BONAMINAME);
        return 20;
    }

    /* Modes that need neither the engine nor an event ring. */
    if (BAStrEqual(argv[1], "-V")) {
        result = DSVersion();
        BAAppClose();
        return result;
    }
    if (BAStrEqual(argv[1], "-E")) {
        result = DSEnumDomains("registration");
        BAAppClose();
        return result;
    }
    if (BAStrEqual(argv[1], "-F")) {
        result = DSEnumDomains("browsing");
        BAAppClose();
        return result;
    }
    if (!BAStrEqual(argv[1], "-B") && !BAStrEqual(argv[1], "-Z") &&
        !BAStrEqual(argv[1], "-L") && !BAStrEqual(argv[1], "-R") &&
        !BAStrEqual(argv[1], "-P") && !BAStrEqual(argv[1], "-q") &&
        !BAStrEqual(argv[1], "-G")) {
        BAPrintf("Unknown mode: %s\n", mode);
        DSUsage();
        BAAppClose();
        return 5;
    }

    /* Engine-backed modes. */
    ring = BARingAlloc();
    if (ring == NULL) {
        BAPrintf("out of memory\n");
        BAAppClose();
        return 20;
    }

    rc = BAAppOpenEngine();
    if (rc != MDNS_OK) {
        BAAppPrintMDNSError("OpenMDNSEngine", rc);
        BAAppClose();
        BARingFree(ring);
        return 20;
    }

    if (BAStrEqual(argv[1], "-B")) {
        result = DSBrowse(ring, (CONST_STRPTR)argv[2],
            (argc > 3) ? (CONST_STRPTR)argv[3] : NULL, 0);
    } else if (BAStrEqual(argv[1], "-Z")) {
        result = DSBrowse(ring, (CONST_STRPTR)argv[2],
            (argc > 3) ? (CONST_STRPTR)argv[3] : NULL, 1);
    } else if (BAStrEqual(argv[1], "-L")) {
        result = DSResolve(ring, (CONST_STRPTR)argv[2], (CONST_STRPTR)argv[3],
            (argc > 4) ? (CONST_STRPTR)argv[4] : NULL);
    } else if (BAStrEqual(argv[1], "-q")) {
        result = DSQuery((CONST_STRPTR)argv[2], (CONST_STRPTR)argv[3]);
    } else if (BAStrEqual(argv[1], "-G")) {
        result = DSLookup((CONST_STRPTR)argv[3]);
    } else if (BAStrEqual(argv[1], "-P")) {
        result = DSRegister((CONST_STRPTR)argv[2], (CONST_STRPTR)argv[3],
            (CONST_STRPTR)argv[4], (UWORD)BAParseNum(argv[5]),
            (CONST_STRPTR)argv[6], DSParseAddr(argv[7]),
            argc - 8, &argv[8]);
    } else {
        result = DSRegister((CONST_STRPTR)argv[2], (CONST_STRPTR)argv[3],
            (CONST_STRPTR)argv[4], (UWORD)BAParseNum(argv[5]), NULL, 0,
            argc - 6, &argv[6]);
    }

    BAAppClose();
    BARingFree(ring);
    return result;
}
