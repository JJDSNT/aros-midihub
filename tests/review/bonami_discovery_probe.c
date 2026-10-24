/* Native ABI/lifetime smoke for the proposed queued DNS-SD API, read both
 * by polling and after a lent request comes back. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>
#include <string.h>
#include <stdio.h>

struct Library *BonamiBase;
static APTR restart_quiet;
static int failures;
#define CHECK(e) do { if (!(e)) { printf("FAIL line %d: %s\n", __LINE__, #e); failures++; } } while (0)

static LONG browse(APTR *ref, struct Hook *hook)
{
    struct TagItem tags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE}, {MDNS_BrowseRef, (IPTR)ref}, {TAG_DONE, 0}
    };
    return BrowseServiceTypeA((STRPTR)"_midi2._udp", hook, tags);
}
static LONG wait_event(APTR ref, struct MDNSDiscoveryEvent *event)
{
    LONG rc = MDNSERR_NOTREADY;
    int i;
    for (i = 0; i < 200 && rc == MDNSERR_NOTREADY; i++) {
        rc = ReadMDNSEvent(ref, event);
        if (rc == MDNSERR_NOTREADY) Delay(5);
    }
    return rc;
}
static void check_event(const struct MDNSDiscoveryEvent *event, ULONG code)
{
    const UBYTE *ip = (const UBYTE *)&event->de_Address;
    const UBYTE *txt = event->de_TXT;
    unsigned i;
    CHECK(event->de_Code == code);
    CHECK(!strcmp(event->de_Name, "Peer.With\\Slashes"));
    CHECK(!strcmp(event->de_Type, "_midi2._udp"));
    CHECK(event->de_Port == 5504);
    CHECK(ip[0] == 192 && ip[1] == 168 && ip[2] == 166 && ip[3] == 1);
    CHECK(event->de_TXTLength == 176);
    CHECK(txt[0] == 114 && !memcmp(txt + 1, "umpendpointname=", 16));
    for (i = 17; i < 115; i++) CHECK(txt[i] == '\\');
    CHECK(txt[115] == 60 && !memcmp(txt + 116, "ProductInstanceId=", 18));
}
/* Lent requests: the reply is the wakeup; reading happens afterwards. */
static struct MsgPort *port;
static struct MDNSEventRequest request;
static LONG lend(APTR ref)
{
    request.er_Msg.mn_ReplyPort = port;
    request.er_Msg.mn_Length = sizeof(request);
    return RequestMDNSEvent(ref, &request);
}
static LONG woken(void)
{
    struct Message *m;
    while ((m = GetMsg(port)) == NULL) Wait(1UL << port->mp_SigBit);
    CHECK(m == &request.er_Msg);
    CHECK(GetMsg(port) == NULL);          /* exactly one reply */
    return request.er_Result;
}
static void woken_event(APTR ref, ULONG code)
{
    struct MDNSDiscoveryEvent event;
    unsigned n = 0;
    CHECK(woken() == MDNS_OK);
    while (ReadMDNSEvent(ref, &event) == MDNS_OK) {
        if (n++ == 0) check_event(&event, code);
    }
    CHECK(n >= 1);
}
static void lent_requests(void)
{
    APTR ref = NULL, resolved = NULL;
    struct MDNSEventRequest second;
    struct TagItem tags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE}, {MDNS_ResolveRef, (IPTR)&resolved}, {TAG_DONE, 0}
    };
    CHECK(browse(&ref, NULL) == MDNS_OK);
    CHECK(lend(ref) == MDNS_OK);
    woken_event(ref, MEVENT_ADD);
    /* Nothing new is coming: stop must hand the request back itself. */
    CHECK(lend(ref) == MDNS_OK);
    memset(&second, 0, sizeof(second));
    second.er_Msg.mn_ReplyPort = port; second.er_Msg.mn_Length = sizeof(second);
    CHECK(RequestMDNSEvent(ref, &second) == MDNSERR_BADPARAM);
    StopBrowse(ref);
    CHECK(GetMsg(port) == &request.er_Msg);   /* back before StopBrowse returned */
    CHECK(request.er_Result == MDNSERR_CANCELLED);
    CHECK(GetMsg(port) == NULL);
    CHECK(lend(ref) == MDNSERR_BADPARAM);      /* stopped */
    CHECK(ResolveServiceA((STRPTR)"Peer.With\\Slashes", (STRPTR)"_midi2._udp", NULL, tags) == MDNS_OK);
    CHECK(lend(resolved) == MDNS_OK);
    woken_event(resolved, MEVENT_RESOLVED);
    StopResolve(resolved);
    CHECK(GetMsg(port) == NULL);               /* nothing lent, nothing back */
}

int main(void)
{
    APTR ref = NULL, resolved = NULL, sentinel = (APTR)123;
    struct MDNSDiscoveryEvent event, unchanged;
    struct TagItem tags[] = {
        {MDNS_EventMode, MDNS_EVENT_QUEUE}, {MDNS_ResolveRef, (IPTR)&resolved}, {TAG_DONE, 0}
    };
    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (!BonamiBase) return 20;
    CHECK(BonamiBase->lib_Revision >= 2);
    port = CreateMsgPort();
    if (!port) return 20;
    lent_requests();
    CHECK(browse(&sentinel, (struct Hook *)1) == MDNSERR_NOTREADY);
    CHECK(sentinel == (APTR)123);
    CHECK(browse(&ref, NULL) == MDNS_OK && ref != NULL);
    CHECK(wait_event(ref, &event) == MDNS_OK); check_event(&event, MEVENT_ADD);
    CHECK(ResolveServiceA((STRPTR)"Peer.With\\Slashes", (STRPTR)"_midi2._udp", NULL, tags) == MDNS_OK);
    CHECK(wait_event(resolved, &event) == MDNS_OK); check_event(&event, MEVENT_RESOLVED);
    StopResolve(resolved);
    memset(&unchanged, 0xa5, sizeof(unchanged)); event = unchanged;
    CHECK(ReadMDNSEvent(resolved, &event) == MDNSERR_BADPARAM);
    CHECK(!memcmp(&event, &unchanged, sizeof(event)));
    CHECK(ResolveServiceA((STRPTR)"Does Not Exist", (STRPTR)"_midi2._udp", NULL, tags) == MDNS_OK);
    CHECK(wait_event(resolved, &event) == MDNS_OK && event.de_Code == MEVENT_RESOLVE_FAILED);
    StopResolve(resolved);
    /* A quiet browse with a lent request: mdnsd's exit must hand it back. */
    {
        APTR quiet = NULL;
        CHECK(browse(&quiet, NULL) == MDNS_OK);
        CHECK(lend(quiet) == MDNS_OK);
        woken_event(quiet, MEVENT_ADD);
        CHECK(lend(quiet) == MDNS_OK);
        restart_quiet = quiet;
    }
    /* Restart only mdnsd, keeping this client/library and the stack alive. */
    {
        struct MsgPort *engine;
        int attempts;
        Forbid();
        engine = FindPort((CONST_STRPTR)"BonAmi");
        if (engine) Signal(engine->mp_SigTask, SIGBREAKF_CTRL_C);
        Permit();
        for (attempts = 0; attempts < 100 && OpenMDNSEngine() == MDNS_OK; attempts++) Delay(5);
        CHECK(OpenMDNSEngine() == MDNSERR_NOENGINE);
        CHECK(GetMsg(port) == &request.er_Msg);   /* replied before the port went */
        CHECK(request.er_Result == MDNSERR_NOENGINE);
        StopBrowse(restart_quiet);
        CHECK(GetMsg(port) == NULL);
    }
    CHECK(SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/Network/Services/mdnsd", TAG_DONE) == 0);
    Delay(100);
    CHECK(ReadMDNSEvent(ref, &event) == MDNSERR_NOENGINE);
    StopBrowse(ref); /* old reference must not stop anything in the new daemon */
    ref = NULL;
    CHECK(browse(&ref, NULL) == MDNS_OK);
    CHECK(wait_event(ref, &event) == MDNS_OK); check_event(&event, MEVENT_ADD);
    StopBrowse(ref);
    CHECK(ReadMDNSEvent(ref, &event) == MDNSERR_BADPARAM);
    CloseMDNSEngine(); CloseLibrary(BonamiBase);
    DeleteMsgPort(port);
    printf("BONAMI-DISCOVERY: %s (%d failures; browse, resolve, TXT, cancel, restart, lent requests)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
