#include "discovery.h"

#include <string.h>

#ifdef __AROS__
#include <aros/asmcall.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <utility/hooks.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>

/* The library's calls take their base from this name. */
struct Library *BonamiBase;
#endif

void netmidi_discovery_init(struct netmidi_discovery *discovery)
{
    memset(discovery, 0, sizeof(*discovery));
}

int netmidi_discovery_add_text(struct netmidi_discovery *discovery,
                               const char *name, const char *type,
                               uint16_t port, const char *text,
                               uint32_t seconds)
{
    if (!discovery || !name || !type || !port ||
        discovery->count == NETMIDI_DISCOVERY_SERVICES)
        return -1;
    discovery->service[discovery->count].name = name;
    discovery->service[discovery->count].type = type;
    discovery->service[discovery->count].port = port;
    discovery->service[discovery->count].text = text;
    discovery->service[discovery->count].seconds = seconds;
    discovery->service[discovery->count].reference = NULL;
    ++discovery->count;
    return 0;
}

int netmidi_discovery_add(struct netmidi_discovery *discovery,
                          const char *name, const char *type, uint16_t port)
{
    return netmidi_discovery_add_text(discovery, name, type, port, NULL, 0);
}

int netmidi_discovery_item(char *text, unsigned long size, const char *key,
                           const char *value)
{
    unsigned long start, out;
    const char *part[3];
    int p;

    if (!text || !size || !key || !value)
        return -1;
    for (start = 0; start < size && text[start]; ++start)
        ;
    if (start == size)
        return -1;
    out = start;
    part[0] = key;
    part[1] = "=";
    part[2] = value;
    if (out && out + 1 < size)
        text[out++] = ' ';
    for (p = 0; p < 3; ++p) {
        const char *c;

        for (c = part[p]; *c; ++c) {
            if (out + 3 > size) {
                text[start] = 0;
                return -1;
            }
            if (*c == ' ' || *c == '\\')
                text[out++] = '\\';
            text[out++] = *c;
        }
    }
    text[out] = 0;
    return 0;
}

int netmidi_discovery_browse(struct netmidi_discovery *discovery,
                             const char *type)
{
    if (!discovery || !type)
        return -1;
    discovery->browse_type = type;
    return 0;
}

int netmidi_discovery_next(struct netmidi_discovery *discovery,
                           struct netmidi_discovered *event)
{
    if (!discovery || !event || discovery->read == discovery->written)
        return 0;
    /* overtaken: the oldest ones are lost, the newest kept */
    if (discovery->written - discovery->read > NETMIDI_DISCOVERY_EVENTS)
        discovery->read = discovery->written - NETMIDI_DISCOVERY_EVENTS;
    *event = discovery->event[discovery->read % NETMIDI_DISCOVERY_EVENTS];
    ++discovery->read;
    return 1;
}

#ifdef __AROS__

/* Called in mdnsd's process, not ours: it may only copy what it is given.
 * The program looks at the ring from its own loop. */
AROS_UFH3(static IPTR, browse_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct MDNSBrowseMsg *, message, A1))
{
    AROS_USERFUNC_INIT

    struct netmidi_discovery *discovery = hook->h_Data;
    struct netmidi_discovered *event;
    unsigned int i;

    (void)object;
    if (message->bm_Code == MEVENT_ADD &&
        (!message->bm_Instance.si_HostAddr || !message->bm_Instance.si_Port))
        return 0;                   /* not resolved yet: it will come again */
    if (message->bm_Code != MEVENT_ADD && message->bm_Code != MEVENT_REMOVE)
        return 0;
    Forbid();
    event = &discovery->event[discovery->written % NETMIDI_DISCOVERY_EVENTS];
    for (i = 0; i + 1 < sizeof(event->name) &&
                message->bm_Instance.si_Name[i]; ++i)
        event->name[i] = message->bm_Instance.si_Name[i];
    event->name[i] = 0;
    /* the address arrives in network order */
    {
        const UBYTE *b = (const UBYTE *)&message->bm_Instance.si_HostAddr;
        event->address = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                         ((uint32_t)b[2] << 8) | b[3];
    }
    event->port = message->bm_Instance.si_Port;
    event->present = message->bm_Code == MEVENT_ADD;
    ++discovery->written;
    Permit();
    return 0;

    AROS_USERFUNC_EXIT
}

/* The service went away: what it held for us went with it. */
static void forget(struct netmidi_discovery *discovery)
{
    int i;
    if (discovery->engine)
        CloseMDNSEngine();
    discovery->engine = 0;
    for (i = 0; i < discovery->count; ++i)
        discovery->service[i].reference = NULL;
    discovery->browse_reference = NULL;
}

int netmidi_discovery_poll(struct netmidi_discovery *discovery, uint64_t now)
{
    int registered = 0;
    int pending = 0;
    int i;

    if (!discovery)
        return -1;
    for (i = 0; i < discovery->count; ++i) {
        if (discovery->service[i].reference)
            ++registered;
        else
            ++pending;
    }
    if (discovery->browse_type && !discovery->browse_reference)
        ++pending;
    if (!pending ||
        (discovery->last_attempt && now - discovery->last_attempt < 100000))
        return registered;
    discovery->last_attempt = now ? now : 1;

    if (!discovery->library) {
        discovery->library = OpenLibrary((CONST_STRPTR)"bonami.library", 40);
        BonamiBase = discovery->library;
        if (!discovery->library)
            return -1;
    }
    if (!discovery->engine) {
        if (OpenMDNSEngine() != MDNS_OK)
            return registered;      /* mdnsd is not running, yet */
        discovery->engine = 1;
    }
    for (i = 0; i < discovery->count; ++i) {
        MDNSRegisterRef reference = NULL;
        struct TagItem tags[] = {
            { MDNS_ServiceName, (IPTR)discovery->service[i].name },
            { MDNS_ServiceType, (IPTR)discovery->service[i].type },
            { MDNS_Port, (IPTR)discovery->service[i].port },
            { MDNS_RegisterRef, (IPTR)&reference },
            { TAG_IGNORE, 0 },
            { TAG_IGNORE, 0 },
            { TAG_DONE, 0 }
        };
        LONG result;

        if (discovery->service[i].reference)
            continue;
        if (discovery->service[i].text) {
            tags[4].ti_Tag = MDNS_TXTRecord;
            tags[4].ti_Data = (IPTR)discovery->service[i].text;
        }
        if (discovery->service[i].seconds) {
            tags[5].ti_Tag = MDNS_TimeToLive;
            tags[5].ti_Data = (IPTR)discovery->service[i].seconds;
        }
        result = RegisterServiceA(tags);
        if (result == MDNS_OK && reference) {
            discovery->service[i].reference = reference;
            ++registered;
        } else if (result == MDNSERR_NOENGINE) {
            forget(discovery);
            return 0;
        }
    }
    if (discovery->browse_type && !discovery->browse_reference) {
        MDNSBrowseRef reference = NULL;
        struct TagItem tags[] = {
            { MDNS_BrowseRef, (IPTR)&reference },
            { TAG_DONE, 0 }
        };
        struct Hook *hook = discovery->browse_hook;

        if (!hook) {
            hook = AllocMem(sizeof(*hook), MEMF_PUBLIC | MEMF_CLEAR);
            if (!hook)
                return registered;
            hook->h_Entry = (HOOKFUNC)(APTR)AROS_ASMSYMNAME(browse_hook);
            hook->h_Data = discovery;
            discovery->browse_hook = hook;
        }
        if (BrowseServiceTypeA((STRPTR)discovery->browse_type, hook,
                               tags) == MDNS_OK && reference)
            discovery->browse_reference = reference;
    }
    return registered;
}

void netmidi_discovery_close(struct netmidi_discovery *discovery)
{
    int i;

    if (!discovery || !discovery->library)
        return;
    BonamiBase = discovery->library;
    /* the search first: after this the service calls the hook no more */
    if (discovery->browse_reference)
        StopBrowse(discovery->browse_reference);
    discovery->browse_reference = NULL;
    for (i = 0; i < discovery->count; ++i) {
        if (discovery->service[i].reference)
            DeregisterService(discovery->service[i].reference);
        discovery->service[i].reference = NULL;
    }
    if (discovery->engine)
        CloseMDNSEngine();
    discovery->engine = 0;
    CloseLibrary(discovery->library);
    discovery->library = NULL;
    BonamiBase = NULL;
    if (discovery->browse_hook)
        FreeMem(discovery->browse_hook, sizeof(struct Hook));
    discovery->browse_hook = NULL;
}

#else

int netmidi_discovery_poll(struct netmidi_discovery *discovery, uint64_t now)
{
    (void)discovery;
    (void)now;
    return -1;
}

void netmidi_discovery_close(struct netmidi_discovery *discovery)
{
    (void)discovery;
}

#endif
