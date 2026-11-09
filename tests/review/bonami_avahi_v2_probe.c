/* Native Bonami 41.8 interoperability smoke against avahi-daemon. */
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

static LONG wait_event(APTR ref, struct MDNSDiscoveryEvent2 *event)
{
    LONG rc = MDNSERR_NOTREADY;
    int i;

    for (i = 0; i < 300 && rc == MDNSERR_NOTREADY; i++) {
        rc = ReadMDNSEvent2(ref, event);
        if (rc == MDNSERR_NOTREADY) {
            Delay(5);
        }
    }
    return rc;
}

static void families(const struct MDNSDiscoveryEvent2 *event,
    int *have4, int *have6)
{
    UWORD i;

    for (i = 0; i < event->de_AddressCount; i++) {
        if (event->de_Addresses[i].da_Family == MDNS_DISCOVERY_ADDR_IPV4) {
            *have4 = 1;
        } else if (event->de_Addresses[i].da_Family ==
                MDNS_DISCOVERY_ADDR_IPV6) {
            *have6 = 1;
        } else {
            CHECK(0);
        }
    }
}

static void check_common(const struct MDNSDiscoveryEvent2 *event)
{
    static const UBYTE txt[] = {12, 's','o','u','r','c','e','=',
        'a','v','a','h','i'};

    CHECK(!strcmp(event->de_Name, "AROS G2 Avahi"));
    CHECK(!strcmp(event->de_Type, "_avahitest._tcp"));
    CHECK(event->de_InterfaceIndex != 0);
    CHECK(event->de_Port == 5688);
    CHECK(event->de_Priority == 0);
    CHECK(event->de_Weight == 0);
    CHECK(event->de_TargetLength > 1);
    CHECK(event->de_TargetLength <= MDNS_DISCOVERY_TARGET_MAX);
    if (event->de_TargetLength > 0) {
        CHECK(event->de_Target[event->de_TargetLength - 1] == 0);
    }
    CHECK(event->de_TXTLength == sizeof(txt));
    CHECK(!memcmp(event->de_TXT, txt, sizeof(txt)));
    CHECK(!(event->de_Flags & MDNS_DISCOVERY_ADDRESSES_TRUNCATED));
}

int main(void)
{
    MDNSBrowseRef browseRef = NULL;
    MDNSResolveRef resolveRef = NULL;
    struct MDNSDiscoveryEvent2 event;
    struct TagItem browseTags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE_V2},
        {MDNS_BrowseRef, (IPTR)&browseRef},
        {TAG_DONE, 0}
    };
    struct TagItem resolveTags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE_V2},
        {MDNS_ResolveRef, (IPTR)&resolveRef},
        {TAG_DONE, 0}
    };
    int have4 = 0, have6 = 0, i;

    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (BonamiBase == NULL) {
        return 20;
    }
    CHECK(BonamiBase->lib_Revision >= 8);
    CHECK(BrowseServiceTypeA((STRPTR)"_avahitest._tcp", NULL,
        browseTags) == MDNS_OK);
    for (i = 0; i < 8 && (!have4 || !have6); i++) {
        if (wait_event(browseRef, &event) != MDNS_OK) {
            CHECK(0);
            break;
        }
        if (event.de_Code == MEVENT_ADD &&
            !strcmp(event.de_Name, "AROS G2 Avahi")) {
            check_common(&event);
            families(&event, &have4, &have6);
        }
    }
    CHECK(have4 && have6);

    CHECK(ResolveServiceA((STRPTR)"AROS G2 Avahi",
        (STRPTR)"_avahitest._tcp", NULL, resolveTags) == MDNS_OK);
    CHECK(wait_event(resolveRef, &event) == MDNS_OK);
    CHECK(event.de_Code == MEVENT_RESOLVED);
    check_common(&event);
    have4 = have6 = 0;
    families(&event, &have4, &have6);
    CHECK(have4 && have6);

    StopResolve(resolveRef);
    StopBrowse(browseRef);
    CloseMDNSEngine();
    CloseLibrary(BonamiBase);
    printf("BONAMI-AVAHI-V2: %s (%d failures)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
