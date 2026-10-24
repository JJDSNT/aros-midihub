/* Packet inputs are encoded independently. Exec/time/network use the shared
 * responder fixture; the real query queues, packet path and IPC dispatch run. */
#define main responder_fixture_main
#include "mdns_responder_rfc.c"
#undef main

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expr); exit(1); } } while (0)

static struct MDNSDaemon daemon;
static UBYTE query_packet[MDNS_MAX_PACKET];

static LONG
call(struct MDNSIPCMsg *msg, UWORD command, APTR ref, ULONG owner_id,
    ULONG daemon_id)
{
    msg->mi_Command = command;
    msg->mi_Handle = ref;
    msg->mi_TaskID = owner_id;
    msg->mi_DaemonID = daemon_id;
    msg->mi_Version = MDNS_IPC_VERSION;
    msg->mi_Msg.mn_Length = sizeof(*msg);
    mdnsClientMessage(&daemon, msg);
    return msg->mi_Result;
}

static APTR
begin(const char *query_name, UWORD type, UWORD timeout)
{
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    strcpy(msg.mi_QueryName, query_name);
    msg.mi_QueryType = type;
    msg.mi_QueryTimeout = timeout;
    CHECK(call(&msg, MDNS_IPC_QUERY, NULL, 1, 0) == MDNS_OK);
    CHECK(msg.mi_DaemonID == 77);
    return msg.mi_Handle;
}

static LONG
read_record(APTR ref, struct MDNSRecordEvent *event)
{
    struct MDNSIPCMsg msg;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    rc = call(&msg, MDNS_IPC_READQUERY, ref, 1, 77);
    if (rc == MDNS_OK) {
        *event = msg.mi_RecordEvent;
    }
    return rc;
}

static void
feed_record(const char *owner_name, UWORD type, UWORD rclass, ULONG ttl,
    const UBYTE *rdata, size_t length, UWORD source_port)
{
    size_t at;

    memset(query_packet, 0, sizeof(query_packet));
    put16(query_packet + 2, DNS_FLAG_QR | DNS_FLAG_AA);
    put16(query_packet + 6, 1);
    at = record(query_packet, 12, owner_name, type, rclass, ttl, rdata,
        length);
    mdnsHandlePacket(&daemon, query_packet, (LONG)at, 0xc0000201,
        source_port);
}

static void
init_query(void)
{
    ticks = 5000;
    live_task_id = 1;
    replies = 0;
    setup(&daemon, 0);
    daemon.md_DaemonID = 77;
}

static void
finish_query(void)
{
    mdnsQueryFree(&daemon);
    mdnsDiscoveryFree(&daemon);
    mdnsRegFree(&daemon);
}

static void
query_a(void)
{
    static const UBYTE address[] = { 203, 0, 113, 9 };
    struct MDNSRecordEvent event;
    APTR ref;

    init_query();
    ref = begin("peer", DNS_TYPE_A, 3);
    mdnsQueryTick(&daemon);
    CHECK(packets == 1);
    feed_record("peer.local", DNS_TYPE_A, DNS_CLASS_IN_FLUSH, 120,
        address, sizeof(address), MDNS_DNS_PORT);
    CHECK(read_record(ref, &event) == MDNS_OK);
    CHECK(event.re_Code == MEVENT_RECORD);
    CHECK(!strcmp(event.re_Name, "peer.local"));
    CHECK(event.re_Type == DNS_TYPE_A && event.re_Class == DNS_CLASS_IN);
    CHECK(event.re_TTL == 120 && event.re_DataLength == 4);
    CHECK(!memcmp(event.re_Data, address, sizeof(address)));
    CHECK(ntohl(event.re_Interface) == daemon.md_Ifs[0].mi_Addr);
    CHECK(read_record(ref, &event) == MDNSERR_NOTREADY);
    finish_query();
}

static void
query_own_host(void)
{
    struct MDNSRecordEvent event;
    static const UBYTE expected[] = { 192, 0, 2, 2 };
    APTR ref;

    init_query();
    ref = begin("AROSBOX", DNS_TYPE_A, 3);
    CHECK(read_record(ref, &event) == MDNS_OK);
    CHECK(!strcmp(event.re_Name, "arosbox.local"));
    CHECK(event.re_DataLength == 4);
    CHECK(!memcmp(event.re_Data, expected, sizeof(expected)));
    finish_query();
}

static void
query_ptr_compressed(void)
{
    struct MDNSRecordEvent event;
    char expanded[256];
    size_t at, owner;
    APTR ref;

    init_query();
    ref = begin("_http._tcp.local", DNS_TYPE_PTR, 0);
    memset(query_packet, 0, sizeof(query_packet));
    put16(query_packet + 2, DNS_FLAG_QR | DNS_FLAG_AA);
    put16(query_packet + 6, 1);
    owner = 12;
    at = name(query_packet, owner, "_http._tcp.local");
    put16(query_packet + at, DNS_TYPE_PTR);
    put16(query_packet + at + 2, DNS_CLASS_IN);
    put32(query_packet + at + 4, 4500);
    put16(query_packet + at + 8, 10);
    at += 10;
    query_packet[at++] = 7;
    memcpy(query_packet + at, "Printer", 7);
    at += 7;
    query_packet[at++] = 0xc0;
    query_packet[at++] = (UBYTE)owner;
    mdnsHandlePacket(&daemon, query_packet, (LONG)at, 0xc0000201,
        MDNS_DNS_PORT);
    CHECK(read_record(ref, &event) == MDNS_OK);
    CHECK(mdnsWireToName(event.re_Data, event.re_DataLength, 0,
        event.re_Data, event.re_DataLength, expanded, sizeof(expanded)) >= 0);
    CHECK(!strcmp(expanded, "Printer._http._tcp.local"));
    finish_query();
}

static void
query_coalesce(void)
{
    struct MDNSRecordEvent event;
    static const UBYTE first[] = { 1, 'a' };
    APTR ref;

    init_query();
    ref = begin("peer.local", DNS_TYPE_TXT, 0);
    feed_record("peer.local", DNS_TYPE_TXT, DNS_CLASS_IN_FLUSH, 10,
        first, sizeof(first), MDNS_DNS_PORT);
    feed_record("PEER.local", DNS_TYPE_TXT, DNS_CLASS_IN_FLUSH, 20,
        first, sizeof(first), MDNS_DNS_PORT);
    CHECK(read_record(ref, &event) == MDNS_OK);
    CHECK(event.re_TTL == 20);
    CHECK(read_record(ref, &event) == MDNSERR_NOTREADY);
    finish_query();
}

static void
query_overflow(void)
{
    struct MDNSRecordEvent event;
    UBYTE txt[] = { 1, 0 };
    APTR ref;
    unsigned i;

    init_query();
    ref = begin("peer.local", DNS_TYPE_TXT, 0);
    for (i = 0; i < 9; i++) {
        txt[1] = (UBYTE)i;
        feed_record("peer.local", DNS_TYPE_TXT, DNS_CLASS_IN, 120,
            txt, sizeof(txt), MDNS_DNS_PORT);
    }
    CHECK(read_record(ref, &event) == MDNSERR_BUSY);
    finish_query();
}

static void
query_timeout(void)
{
    struct MDNSIPCMsg msg;
    struct MDNSEventRequest request;
    struct MDNSRecordEvent event;
    APTR ref;

    init_query();
    ref = begin("absent.local", DNS_TYPE_A, 1);
    memset(&request, 0, sizeof(request));
    memset(&msg, 0, sizeof(msg));
    msg.mi_Request = &request;
    CHECK(call(&msg, MDNS_IPC_NOTIFYQUERY, ref, 1, 77) == MDNS_OK);
    ticks += 50;
    mdnsQueryTick(&daemon);
    CHECK(replies == 1 && request.er_Result == MDNSERR_TIMEOUT);
    CHECK(read_record(ref, &event) == MDNSERR_TIMEOUT);
    finish_query();
}

static void
query_notify_stop(void)
{
    struct MDNSIPCMsg msg;
    struct MDNSEventRequest request;
    APTR ref;

    init_query();
    ref = begin("peer.local", DNS_TYPE_A, 0);
    memset(&request, 0, sizeof(request));
    memset(&msg, 0, sizeof(msg));
    msg.mi_Request = &request;
    CHECK(call(&msg, MDNS_IPC_NOTIFYQUERY, ref, 1, 77) == MDNS_OK);
    memset(&msg, 0, sizeof(msg));
    CHECK(call(&msg, MDNS_IPC_STOPQUERY, ref, 1, 77) == MDNS_OK);
    CHECK(replies == 1 && request.er_Result == MDNSERR_CANCELLED);
    finish_query();
}

static void
query_owner_restart(void)
{
    struct MDNSIPCMsg msg;
    struct MDNSRecordEvent event;
    APTR ref;

    init_query();
    ref = begin("peer.local", DNS_TYPE_A, 0);
    memset(&msg, 0, sizeof(msg));
    CHECK(call(&msg, MDNS_IPC_READQUERY, ref, 2, 77) == MDNSERR_BADPARAM);
    CHECK(call(&msg, MDNS_IPC_READQUERY, ref, 1, 78) == MDNSERR_NOENGINE);
    live_task_id = 2;
    mdnsQueryReap(&daemon);
    live_task_id = 1;
    CHECK(read_record(ref, &event) == MDNSERR_BADPARAM);
    finish_query();
}

static void
query_network_reset(void)
{
    APTR ref;

    init_query();
    ref = begin("peer.local", DNS_TYPE_A, 0);
    (void)ref;
    mdnsQueryTick(&daemon);
    CHECK(packets == 1);
    ticks++;
    mdnsQueryReset(&daemon);
    mdnsQueryTick(&daemon);
    CHECK(packets == 2);
    finish_query();
}

static void
query_filtered(UWORD rclass, UWORD port)
{
    static const UBYTE address[] = { 203, 0, 113, 9 };
    struct MDNSRecordEvent event;
    APTR ref;

    init_query();
    ref = begin("peer.local", DNS_TYPE_A, 0);
    feed_record("peer.local", DNS_TYPE_A, rclass, 120, address,
        sizeof(address), port);
    CHECK(read_record(ref, &event) == MDNSERR_NOTREADY);
    finish_query();
}

static void
query_invalid(void)
{
    struct MDNSIPCMsg msg;

    init_query();
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.mi_QueryName, "bad..name");
    msg.mi_QueryType = DNS_TYPE_A;
    CHECK(call(&msg, MDNS_IPC_QUERY, NULL, 1, 0) == MDNSERR_BADPARAM);
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.mi_QueryName, "peer.local");
    CHECK(call(&msg, MDNS_IPC_QUERY, NULL, 1, 0) == MDNSERR_BADPARAM);
    msg.mi_QueryType = DNS_TYPE_A;
    msg.mi_QueryTimeout = 61;
    CHECK(call(&msg, MDNS_IPC_QUERY, NULL, 1, 0) == MDNSERR_BADPARAM);
    finish_query();
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    if (!strcmp(argv[1], "query-a")) query_a();
    else if (!strcmp(argv[1], "query-own-host")) query_own_host();
    else if (!strcmp(argv[1], "query-ptr-compressed")) query_ptr_compressed();
    else if (!strcmp(argv[1], "query-coalesce")) query_coalesce();
    else if (!strcmp(argv[1], "query-overflow")) query_overflow();
    else if (!strcmp(argv[1], "query-timeout")) query_timeout();
    else if (!strcmp(argv[1], "query-notify-stop")) query_notify_stop();
    else if (!strcmp(argv[1], "query-owner-restart")) query_owner_restart();
    else if (!strcmp(argv[1], "query-network-reset")) query_network_reset();
    else if (!strcmp(argv[1], "query-wrong-class")) query_filtered(3, MDNS_DNS_PORT);
    else if (!strcmp(argv[1], "query-wrong-port")) query_filtered(DNS_CLASS_IN, 1234);
    else if (!strcmp(argv[1], "query-invalid")) query_invalid();
    else return 2;
    printf("review fixture %s: PASS\n", argv[1]);
    return 0;
}
