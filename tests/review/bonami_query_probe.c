/* Native ABI/lifetime smoke for queued record queries and synchronous host
 * lookup. A Linux peer supplies independently encoded PTR and A answers. */
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
static int failures;
#define CHECK(e) do { if (!(e)) { printf("FAIL line %d: %s\n", __LINE__, #e); failures++; } } while (0)

static LONG
decode_name(const UBYTE *wire, UWORD length, char *out, UWORD out_size)
{
    UWORD at = 0, used = 0;

    while (at < length) {
        UBYTE label = wire[at++];

        if (label == 0) {
            if (used != 0) out[used - 1] = '\0';
            else out[0] = '\0';
            return MDNS_OK;
        }
        if (label > 63 || at + label > length || used + label + 1 >= out_size) {
            return MDNSERR_BADPARAM;
        }
        memcpy(out + used, wire + at, label);
        used += label;
        out[used++] = '.';
        at += label;
    }
    return MDNSERR_BADPARAM;
}

static LONG
start_query(MDNSQueryRef *ref)
{
    struct TagItem tags[] = {
        { MDNS_EventMode, MDNS_EVENT_QUEUE },
        { MDNS_QueryRef, (IPTR)ref },
        { TAG_DONE, 0 }
    };

    return QueryRecordA((STRPTR)"_querytest._udp.local", 12, tags);
}

int
main(void)
{
    struct MDNSRecordEvent event;
    struct MDNSEventRequest request;
    struct MsgPort *port;
    struct Message *message;
    MDNSQueryRef ref = NULL;
    ULONG address = 0;
    UBYTE *bytes = (UBYTE *)&address;
    char target[256];
    struct TagItem timeout[] = {
        { MDNS_Timeout, 3 }, { TAG_DONE, 0 }
    };

    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (BonamiBase == NULL) return RETURN_FAIL;
    CHECK(BonamiBase->lib_Revision == 3);
    CHECK(OpenMDNSEngine() == MDNS_OK);
    port = CreateMsgPort();
    if (port == NULL) return RETURN_FAIL;

    CHECK(start_query(&ref) == MDNS_OK && ref != NULL);
    memset(&request, 0, sizeof(request));
    request.er_Msg.mn_ReplyPort = port;
    request.er_Msg.mn_Length = sizeof(request);
    CHECK(RequestMDNSEvent(ref, &request) == MDNS_OK);
    while ((message = GetMsg(port)) == NULL) {
        Wait(1UL << port->mp_SigBit);
    }
    CHECK(message == &request.er_Msg);
    CHECK(request.er_Result == MDNS_OK);
    CHECK(ReadMDNSRecordEvent(ref, &event) == MDNS_OK);
    CHECK(event.re_Code == MEVENT_RECORD);
    CHECK(!strcmp(event.re_Name, "_querytest._udp.local"));
    CHECK(event.re_Type == 12 && event.re_Class == 1 && event.re_TTL == 120);
    CHECK(decode_name(event.re_Data, event.re_DataLength, target,
        sizeof(target)) == MDNS_OK);
    CHECK(!strcmp(target, "Query Peer._querytest._udp.local"));
    CHECK(ReadMDNSRecordEvent(ref, &event) == MDNSERR_NOTREADY);

    memset(&request, 0, sizeof(request));
    request.er_Msg.mn_ReplyPort = port;
    request.er_Msg.mn_Length = sizeof(request);
    CHECK(RequestMDNSEvent(ref, &request) == MDNS_OK);
    StopQuery(ref);
    CHECK(GetMsg(port) == &request.er_Msg);
    CHECK(request.er_Result == MDNSERR_CANCELLED);
    CHECK(ReadMDNSRecordEvent(ref, &event) == MDNSERR_BADPARAM);

    CHECK(LookupHostA((STRPTR)"querypeer", &address, timeout) == MDNS_OK);
    CHECK(bytes[0] == 192 && bytes[1] == 168 && bytes[2] == 166 &&
        bytes[3] == 1);
    address = 0xa5a5a5a5;
    timeout[0].ti_Data = 1;
    CHECK(LookupHostA((STRPTR)"absent", &address, timeout) == MDNSERR_TIMEOUT);
    CHECK(address == 0xa5a5a5a5);

    ref = (APTR)123;
    {
        struct TagItem tags[] = {
            { MDNS_QueryRef, (IPTR)&ref },
            { TAG_DONE, 0 }
        };
        CHECK(QueryRecordA((STRPTR)"peer.local", 1, tags) ==
            MDNSERR_NOTREADY);
        CHECK(ref == (APTR)123);
    }

    DeleteMsgPort(port);
    CloseMDNSEngine();
    CloseLibrary(BonamiBase);
    printf("BONAMI-QUERY: %s (%d failures; record queue, cancel, lookup, timeout)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? RETURN_FAIL : RETURN_OK;
}
