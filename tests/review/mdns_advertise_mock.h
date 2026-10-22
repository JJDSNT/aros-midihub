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

#ifdef NETMIDI_BROWSE
/* The queued browse of mdnsd's DISCOVERY.md: one queue per search, read
   in the caller's task. */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
typedef char *STRPTR;
enum { MDNS_EventMode = 100, MDNS_BrowseRef, MDNS_EVENT_QUEUE = 1 };
enum { MDNSERR_NOTREADY = 20, MDNSERR_BUSY, MDNSERR_CANCELLED };
enum { MEVENT_ADD = 1, MEVENT_REMOVE, MEVENT_RESOLVED };
#define MDNS_DISCOVERY_TXT_MAX 512
struct MDNSDiscoveryEvent {
    ULONG de_Code;
    ULONG de_Address;
    ULONG de_Interface;
    UWORD de_Port;
    UWORD de_TXTLength;
    char de_Name[64];
    char de_Type[32];
    UBYTE de_TXT[MDNS_DISCOVERY_TXT_MAX];
};
static struct MDNSDiscoveryEvent queued[8];
static int queued_count, browses_started, browses_stopped, reads;
static LONG forced_read;
static uintptr_t next_browse = 100;
static LONG BrowseServiceTypeA(STRPTR type, void *hook, struct TagItem *tags)
{
    void **result = NULL;
    int queue_mode = 0;

    assert(type && !hook);
    if (!available) return MDNSERR_NOENGINE;
    for (; tags->ti_Tag; ++tags) {
        if (tags->ti_Tag == MDNS_EventMode)
            queue_mode = tags->ti_Data == MDNS_EVENT_QUEUE;
        if (tags->ti_Tag == MDNS_BrowseRef) result = (void **)tags->ti_Data;
    }
    assert(queue_mode && result);
    *result = (void *)++next_browse;
    ++browses_started;
    return MDNS_OK;
}
static void StopBrowse(void *reference) { assert(reference); ++browses_stopped; }
static LONG ReadMDNSEvent(void *reference, struct MDNSDiscoveryEvent *event)
{
    int i;

    assert(reference && event);
    ++reads;
    if (forced_read) return forced_read;
    if (!queued_count) return MDNSERR_NOTREADY;
    *event = queued[0];
    for (i = 1; i < queued_count; ++i)
        queued[i - 1] = queued[i];
    --queued_count;
    return MDNS_OK;
}
static void queue_event(ULONG code, const char *name, const UBYTE *address,
                        UWORD port, const UBYTE *txt, UWORD txt_length)
{
    struct MDNSDiscoveryEvent *e = &queued[queued_count++];

    memset(e, 0, sizeof(*e));
    e->de_Code = code;
    memcpy(&e->de_Address, address, 4);
    e->de_Port = port;
    strcpy(e->de_Name, name);
    memcpy(e->de_TXT, txt, txt_length);
    e->de_TXTLength = txt_length;
}
#endif
