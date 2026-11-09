/* Native Bonami 41.8 dual-stack discovery smoke. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>
#include <string.h>
#include <stdio.h>

struct Library *BonamiBase;
static int failures;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL line %d: %s\n", __LINE__, #e); failures++; \
} } while (0)

static LONG browse(APTR *ref, UWORD mode)
{
    struct TagItem tags[] = {
        {MDNS_EventMode, mode},
        {MDNS_BrowseRef, (IPTR)ref},
        {TAG_DONE, 0}
    };
    return BrowseServiceTypeA((STRPTR)"_v2test._udp", NULL, tags);
}

static LONG resolve(APTR *ref)
{
    struct TagItem tags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE_V2},
        {MDNS_ResolveRef, (IPTR)ref},
        {TAG_DONE, 0}
    };
    return ResolveServiceA((STRPTR)"IPv6 Only", (STRPTR)"_v2test._udp",
        NULL, tags);
}

static LONG wait_v2(APTR ref, struct MDNSDiscoveryEvent2 *event)
{
    LONG rc = MDNSERR_NOTREADY;
    int i;

    for (i = 0; i < 200 && rc == MDNSERR_NOTREADY; i++) {
        rc = ReadMDNSEvent2(ref, event);
        if (rc == MDNSERR_NOTREADY) {
            Delay(5);
        }
    }
    return rc;
}

static void address(const struct MDNSDiscoveryEvent2 *event, UWORD at,
    UWORD family, const UBYTE *expected, UWORD length)
{
    CHECK(at < event->de_AddressCount);
    if (at < event->de_AddressCount) {
        CHECK(event->de_Addresses[at].da_Family == family);
        CHECK(!memcmp(event->de_Addresses[at].da_Address, expected, length));
    }
}

static void check_ipv6(const struct MDNSDiscoveryEvent2 *event, ULONG code)
{
    static const UBYTE target[] = {
        5,'p','e','e','r','6',5,'l','o','c','a','l',0
    };
    static const UBYTE ip6[] = {
        0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0x61
    };

    CHECK(event->de_Code == code);
    CHECK(!strcmp(event->de_Name, "IPv6 Only"));
    CHECK(!strcmp(event->de_Type, "_v2test._udp"));
    CHECK(event->de_InterfaceIndex != 0);
    CHECK(event->de_Port == 5601);
    CHECK(event->de_Priority == 0);
    CHECK(event->de_Weight == 0);
    CHECK(event->de_TargetLength == sizeof(target));
    CHECK(!memcmp(event->de_Target, target, sizeof(target)));
    CHECK(event->de_AddressCount == 1);
    address(event, 0, MDNS_DISCOVERY_ADDR_IPV6, ip6, sizeof(ip6));
}

static void check_ipv4(const struct MDNSDiscoveryEvent2 *event)
{
    static const UBYTE target[] = {
        5,'p','e','e','r','4',5,'l','o','c','a','l',0
    };
    static const UBYTE ip4[] = {192,168,166,41};

    CHECK(event->de_Code == MEVENT_ADD);
    CHECK(!strcmp(event->de_Name, "IPv4 Only"));
    CHECK(!strcmp(event->de_Type, "_v2test._udp"));
    CHECK(event->de_InterfaceIndex != 0);
    CHECK(event->de_Port == 5600);
    CHECK(event->de_Priority == 0);
    CHECK(event->de_Weight == 0);
    CHECK(event->de_TargetLength == sizeof(target));
    CHECK(!memcmp(event->de_Target, target, sizeof(target)));
    CHECK(event->de_AddressCount == 1);
    address(event, 0, MDNS_DISCOVERY_ADDR_IPV4, ip4, sizeof(ip4));
}

static void check_dual(const struct MDNSDiscoveryEvent2 *event)
{
    static const UBYTE a1[] = {192,168,166,11};
    static const UBYTE a2[] = {192,168,166,12};
    static const UBYTE aaaa1[] = {
        0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0x71
    };
    static const UBYTE aaaa2[] = {
        0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0x72
    };

    CHECK(event->de_Code == MEVENT_ADD);
    CHECK(!strcmp(event->de_Name, "Dual Multi"));
    CHECK(event->de_Priority == 0);
    CHECK(event->de_Weight == 0);
    CHECK(event->de_AddressCount == 4);
    CHECK(!(event->de_Flags & MDNS_DISCOVERY_ADDRESSES_TRUNCATED));
    address(event, 0, MDNS_DISCOVERY_ADDR_IPV4, a1, sizeof(a1));
    address(event, 1, MDNS_DISCOVERY_ADDR_IPV4, a2, sizeof(a2));
    address(event, 2, MDNS_DISCOVERY_ADDR_IPV6, aaaa1, sizeof(aaaa1));
    address(event, 3, MDNS_DISCOVERY_ADDR_IPV6, aaaa2, sizeof(aaaa2));
}

int main(void)
{
    APTR oldRef = NULL, newRef = NULL, resolveRef = NULL;
    struct MDNSDiscoveryEvent oldEvent;
    struct MDNSDiscoveryEvent2 event;
    int gotIPv6 = 0, gotIPv4V2 = 0, gotDualV2 = 0;
    int gotIPv4V1 = 0, gotDualV1 = 0, i;

    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (BonamiBase == NULL) {
        return 20;
    }
    CHECK(BonamiBase->lib_Revision >= 8);
    CHECK(browse(&oldRef, MDNS_EVENT_QUEUE) == MDNS_OK);
    CHECK(browse(&newRef, MDNS_EVENT_QUEUE_V2) == MDNS_OK);
    CHECK(ReadMDNSEvent2(oldRef, &event) == MDNSERR_BADPARAM);
    CHECK(ReadMDNSEvent(newRef, &oldEvent) == MDNSERR_BADPARAM);

    for (i = 0; i < 6 && (!gotIPv6 || !gotIPv4V2 || !gotDualV2); i++) {
        CHECK(wait_v2(newRef, &event) == MDNS_OK);
        if (!strcmp(event.de_Name, "IPv6 Only")) {
            check_ipv6(&event, MEVENT_ADD);
            gotIPv6 = 1;
        } else if (!strcmp(event.de_Name, "IPv4 Only")) {
            check_ipv4(&event);
            gotIPv4V2 = 1;
        } else if (!strcmp(event.de_Name, "Dual Multi")) {
            check_dual(&event);
            gotDualV2 = 1;
        }
    }
    CHECK(gotIPv6 && gotIPv4V2 && gotDualV2);

    for (i = 0; i < 300 && (!gotIPv4V1 || !gotDualV1); i++) {
        LONG rc = ReadMDNSEvent(oldRef, &oldEvent);

        if (rc == MDNS_OK) {
            CHECK(strcmp(oldEvent.de_Name, "IPv6 Only") != 0);
            gotIPv4V1 |= !strcmp(oldEvent.de_Name, "IPv4 Only");
            gotDualV1 |= !strcmp(oldEvent.de_Name, "Dual Multi");
        } else if (rc == MDNSERR_NOTREADY) {
            Delay(5);
        } else {
            CHECK(0);
            break;
        }
    }
    CHECK(gotIPv4V1 && gotDualV1);

    /* Keep observing V1 after the valid dual-stack event.  An IPv6-only
       event queued behind it would otherwise escape this compatibility
       check. */
    for (i = 0; i < 50; i++) {
        LONG rc = ReadMDNSEvent(oldRef, &oldEvent);

        if (rc == MDNS_OK) {
            CHECK(strcmp(oldEvent.de_Name, "IPv6 Only") != 0);
        } else if (rc == MDNSERR_NOTREADY) {
            Delay(2);
        } else {
            CHECK(0);
            break;
        }
    }

    CHECK(resolve(&resolveRef) == MDNS_OK);
    CHECK(wait_v2(resolveRef, &event) == MDNS_OK);
    check_ipv6(&event, MEVENT_RESOLVED);

    StopResolve(resolveRef);
    StopBrowse(newRef);
    StopBrowse(oldRef);
    CloseMDNSEngine();
    CloseLibrary(BonamiBase);
    printf("BONAMI-DISCOVERY-V2: %s (%d failures)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
