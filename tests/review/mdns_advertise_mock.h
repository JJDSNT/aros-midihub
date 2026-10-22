#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "discovery.h"

typedef uintptr_t IPTR;
typedef long LONG;
typedef const char *CONST_STRPTR;
struct Library { int unused; };
struct TagItem { IPTR ti_Tag, ti_Data; };
enum { TAG_DONE, TAG_IGNORE, MDNS_ServiceName, MDNS_ServiceType, MDNS_Port,
       MDNS_RegisterRef, MDNS_TXTRecord, MDNS_TimeToLive };
enum { MDNS_OK, MDNSERR_NOENGINE, MDNSERR_NOTFOUND, MDNSERR_BADPARAM,
       MDNSERR_NOMEM, MDNSERR_DUPLICATE };
#define TAG "netmidid: "
static int available = 1, generation = 1, active, opens, registrations;
static int refreshes, withdrawals, releases, library_available = 1;
static LONG forced_update, forced_register;
static const char *expected_name = "AROS Test";
static struct Library library;
static struct Library *OpenLibrary(CONST_STRPTR name, int version)
{
    assert(!strcmp(name, "bonami.library") && version == 40);
    ++opens;
    return library_available ? &library : NULL;
}
static void CloseLibrary(void *base) { assert(base == &library); }
static LONG OpenMDNSEngine(void) { return available ? MDNS_OK : MDNSERR_NOENGINE; }
static void CloseMDNSEngine(void) { ++releases; active = 0; }
static LONG RegisterServiceA(struct TagItem *tags)
{
    void **result = NULL;
    if (!available) return MDNSERR_NOENGINE;
    if (forced_register) return forced_register;
    for (; tags->ti_Tag; ++tags) {
        if (tags->ti_Tag == MDNS_ServiceName)
            assert(!strcmp((const char *)tags->ti_Data, expected_name));
        if (tags->ti_Tag == MDNS_ServiceType)
            assert(!strcmp((const char *)tags->ti_Data, "_apple-midi._udp"));
        if (tags->ti_Tag == MDNS_Port) assert(tags->ti_Data == 5004);
        if (tags->ti_Tag == MDNS_RegisterRef) result = (void **)tags->ti_Data;
    }
    assert(result);
    *result = (void *)(uintptr_t)generation;
    active = 1;
    ++registrations;
    return MDNS_OK;
}
static LONG UpdateServiceA(void *reference, struct TagItem *tags)
{
    ++refreshes;
    assert(tags->ti_Tag == TAG_DONE);
    if (forced_update) return forced_update;
    if (!available) return MDNSERR_NOENGINE;
    return (uintptr_t)reference == (uintptr_t)generation ? MDNS_OK : MDNSERR_NOTFOUND;
}
static void DeregisterService(void *reference)
{
    assert((uintptr_t)reference == (uintptr_t)generation);
    ++withdrawals;
    active = 0;
}
