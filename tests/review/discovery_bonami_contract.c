/* Compile the actual integration source with mocked library calls.
 * Exit 1 means a protocol/lifecycle expectation failed, 2 is bad test setup.
 */
#include "aros_host.h"
#include <bonami/mdns.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Library fake_library;
static unsigned registrations, searches, calls;
static IPTR generation = 1;

static struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{ (void)name; (void)version; return &fake_library; }
static void CloseLibrary(struct Library *library) { (void)library; }
static void Forbid(void) {}
static void Permit(void) {}
static APTR AllocMem(size_t size, ULONG flags) { (void)flags; return calloc(1, size); }
static void FreeMem(APTR memory, size_t size) { (void)size; free(memory); }
static LONG OpenMDNSEngine(void) { ++calls; return MDNS_OK; }
static void CloseMDNSEngine(void) { ++calls; registrations = searches = 0; }
static LONG RegisterServiceA(struct TagItem *tags)
{
    ++calls;
    ++registrations;
    for (; tags->ti_Tag != TAG_DONE; ++tags)
        if (tags->ti_Tag == MDNS_RegisterRef)
            *(APTR *)tags->ti_Data = (APTR)generation;
    return MDNS_OK;
}
static void DeregisterService(APTR reference) { (void)reference; ++calls; }
static LONG BrowseServiceTypeA(STRPTR type, struct Hook *hook, struct TagItem *tags)
{
    (void)type;
    (void)hook;
    ++calls;
    ++searches;
    for (; tags->ti_Tag != TAG_DONE; ++tags)
        if (tags->ti_Tag == MDNS_BrowseRef)
            *(APTR *)tags->ti_Data = (APTR)generation;
    return MDNS_OK;
}
static void StopBrowse(APTR reference) { (void)reference; ++calls; }

#include "../../ports/aros/midihub/discovery.c"

static int receive_text(const char *text, const char *expected)
{
    struct netmidi_discovery discovery;
    struct netmidi_discovered event;
    struct MDNSBrowseMsg message;
    struct Hook hook;
    netmidi_discovery_init(&discovery);
    memset(&message, 0, sizeof(message));
    memset(&hook, 0, sizeof(hook));
    hook.h_Data = &discovery;
    message.bm_Code = MEVENT_ADD;
    strcpy(message.bm_Instance.si_Name, "test-host");
    message.bm_Instance.si_HostAddr = 1;
    message.bm_Instance.si_Port = 5673;
    message.bm_Instance.si_TXTLength = (UWORD)strlen(text);
    if (strlen(text) > sizeof(message.bm_Instance.si_TXT))
        return 2;
    memcpy(message.bm_Instance.si_TXT, text, strlen(text));
    browse_hook(&hook, NULL, &message);
    if (!netmidi_discovery_next(&discovery, &event))
        return 2;
    printf("expected length=%zu, received length=%zu, endpoint='%s'\n",
           strlen(expected), strlen(event.endpoint), event.endpoint);
    return strcmp(event.endpoint, expected) != 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "spaces"))
        return receive_text("UMPEndpointName=Studio\\ Piano ProductInstanceId=unit1", "Studio Piano");
    if (!strcmp(argv[1], "key-case"))
        return receive_text("umpendpointname=Piano ProductInstanceId=unit1", "Piano");
    if (!strcmp(argv[1], "endpoint-98")) {
        char name[99], text[160];
        memset(name, 'A', sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        snprintf(text, sizeof(text), "UMPEndpointName=%s", name);
        return receive_text(text, name);
    }
    if (!strcmp(argv[1], "restart")) {
        struct netmidi_discovery discovery;
        unsigned before;
        int reported, failed;
        netmidi_discovery_init(&discovery);
        netmidi_discovery_add(&discovery, "Session", "_apple-midi._udp", 5004);
        netmidi_discovery_browse(&discovery, "_apple-midi._udp");
        if (netmidi_discovery_poll(&discovery, 1) != 1 ||
            registrations != 1 || searches != 1)
            return 2;
        /* A daemon restart loses its registry and searches. The library
         * remains loaded; a new daemon answers OpenMDNSEngine normally.
         */
        registrations = searches = 0;
        ++generation;
        before = calls;
        reported = netmidi_discovery_poll(&discovery, 200001);
        printf("after restart: reported=%d, registrations=%u, searches=%u, library calls=%u\n",
               reported, registrations, searches, calls - before);
        failed = registrations != 1 || searches != 1;
        netmidi_discovery_close(&discovery);
        return failed;
    }
    return 2;
}
