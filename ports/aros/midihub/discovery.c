#include "discovery.h"

#include <string.h>

#ifdef __AROS__
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>

/* The library's calls take their base from this name. */
struct Library *BonamiBase;
#endif

void netmidi_discovery_init(struct netmidi_discovery *discovery)
{
    memset(discovery, 0, sizeof(*discovery));
}

int netmidi_discovery_add(struct netmidi_discovery *discovery,
                          const char *name, const char *type, uint16_t port)
{
    if (!discovery || !name || !type || !port ||
        discovery->count == NETMIDI_DISCOVERY_SERVICES)
        return -1;
    discovery->service[discovery->count].name = name;
    discovery->service[discovery->count].type = type;
    discovery->service[discovery->count].port = port;
    discovery->service[discovery->count].reference = NULL;
    ++discovery->count;
    return 0;
}

#ifdef __AROS__

/* The service went away: what it held for us went with it. */
static void forget(struct netmidi_discovery *discovery)
{
    int i;
    if (discovery->engine)
        CloseMDNSEngine();
    discovery->engine = 0;
    for (i = 0; i < discovery->count; ++i)
        discovery->service[i].reference = NULL;
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
            { TAG_DONE, 0 }
        };
        LONG result;

        if (discovery->service[i].reference)
            continue;
        result = RegisterServiceA(tags);
        if (result == MDNS_OK && reference) {
            discovery->service[i].reference = reference;
            ++registered;
        } else if (result == MDNSERR_NOENGINE) {
            forget(discovery);
            return 0;
        }
    }
    return registered;
}

void netmidi_discovery_close(struct netmidi_discovery *discovery)
{
    int i;

    if (!discovery || !discovery->library)
        return;
    BonamiBase = discovery->library;
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
