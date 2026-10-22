/* Packet inputs are encoded independently. Exec/time/network use the shared
 * responder fixture; the real daemon cache, packet path and IPC dispatch run. */
#define main responder_fixture_main
#include "mdns_responder_rfc.c"
#undef main

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expr); exit(1); } } while (0)
static struct MDNSDaemon md;
static UBYTE packet[MDNS_MAX_PACKET], data[512];
/* the service type records are fed for */
static const char *feed_type = "_midi2._udp.local";
static const UBYTE txt[] = { 5, 'a','=','b',0,'c', 4,'X','=',' ','\\' };

static size_t key(UBYTE *out, size_t at, const char *instance, int host)
{
    if (instance) {
        size_t n = strlen(instance);
        CHECK(n <= 63);
        out[at++] = n;
        memcpy(out + at, instance, n);
        at += n;
    }
    return name(out, at, host ? "peer.local" : feed_type);
}
static size_t rr(size_t at, const char *instance, UWORD type, UWORD cls,
                 ULONG ttl, const UBYTE *rdata, size_t len)
{
    at = key(packet, at, instance, type == DNS_TYPE_A);
    put16(packet + at, type); put16(packet + at + 2, cls);
    put32(packet + at + 4, ttl); put16(packet + at + 8, len);
    memcpy(packet + at + 10, rdata, len);
    return at + 10 + len;
}
static void feed_records(const char *instance, unsigned mask, ULONG ttl,
                         UBYTE ip, int tick)
{
    size_t at = 12, len;
    unsigned count = 0;
    memset(packet, 0, sizeof(packet));
    put16(packet + 2, 0x8400);
    if (mask & 1) {
        len = key(data, 0, instance, 0);
        at = rr(at, NULL, DNS_TYPE_PTR, 1, ttl, data, len); ++count;
    }
    if (mask & 2) {
        memset(data, 0, 6); put16(data + 4, 5504);
        len = key(data, 6, NULL, 1);
        at = rr(at, instance, DNS_TYPE_SRV, 0x8001, ttl, data, len); ++count;
    }
    if (mask & 4) {
        at = rr(at, instance, DNS_TYPE_TXT, 0x8001, ttl, txt, sizeof(txt)); ++count;
    }
    if (mask & 8) {
        const UBYTE addr[] = {203,0,113,ip};
        at = rr(at, NULL, DNS_TYPE_A, 0x8001, ttl, addr, sizeof(addr)); ++count;
    }
    put16(packet + 6, count);
    mdnsHandlePacket(&md, packet, at, 0xcb007101, 5353);
    if (tick) mdnsDiscoveryTick(&md);
}
static void feed(const char *instance, unsigned mask, ULONG ttl, UBYTE ip)
{
    feed_records(instance, mask, ttl, ip, 1);
}
static LONG call(struct MDNSIPCMsg *msg, UWORD command, APTR ref, ULONG ownerID, ULONG epoch)
{
    msg->mi_Command = command; msg->mi_Handle = ref;
    msg->mi_TaskID = ownerID; msg->mi_DaemonID = epoch;
    msg->mi_Version = MDNS_IPC_VERSION; msg->mi_Msg.mn_Length = sizeof(*msg);
    mdnsClientMessage(&md, msg);
    return msg->mi_Result;
}
static APTR begin_type(int resolve, const char *instance, const char *type)
{
    struct MDNSIPCMsg msg = {0};
    strcpy(msg.mi_ServiceType, type);
    if (instance) strcpy(msg.mi_Name, instance);
    CHECK(call(&msg, resolve ? MDNS_IPC_RESOLVE : MDNS_IPC_BROWSE, NULL, 1, 0) == MDNS_OK);
    CHECK(msg.mi_DaemonID == 77);
    return msg.mi_Handle;
}
static APTR begin(int resolve, const char *instance)
{
    return begin_type(resolve, instance, "_midi2._udp");
}
static LONG read_event(APTR ref, struct MDNSDiscoveryEvent *out)
{
    struct MDNSIPCMsg msg = {0};
    LONG rc = call(&msg, MDNS_IPC_READ, ref, 1, 77);
    if (rc == MDNS_OK) *out = msg.mi_Event;
    return rc;
}
static void init(void)
{
    ticks = 5000; live_task_id = 1; packets = 0; feed_type = "_midi2._udp.local";
    setup(&md, 0); md.md_DaemonID = 77;
}
static void finish(void)
{
    mdnsDiscoveryFree(&md); mdnsRegFree(&md);
}
static void expect_add(APTR ref, const char *instance, UBYTE address)
{
    struct MDNSDiscoveryEvent e;
    CHECK(read_event(ref, &e) == MDNS_OK);
    CHECK(e.de_Code == MEVENT_ADD || e.de_Code == MEVENT_RESOLVED);
    CHECK(!strcmp(e.de_Name, instance)); CHECK(!strcmp(e.de_Type, "_midi2._udp"));
    CHECK(ntohl(e.de_Address) == (0xcb007100u | address));
    CHECK(e.de_Port == 5504); CHECK(e.de_TXTLength == sizeof(txt));
    CHECK(!memcmp(e.de_TXT, txt, sizeof(txt)));
}
static void labels(void)
{
    char inst[64]; APTR ref;
    memset(inst, 'A', 63); inst[7] = '.'; inst[15] = '\\'; inst[63] = 0;
    init(); ref = begin(0, NULL);
    feed(inst, 8, 120, 9); feed(inst, 4, 120, 9); feed(inst, 1, 120, 9);
    { struct MDNSDiscoveryEvent e; CHECK(read_event(ref, &e) == MDNSERR_NOTREADY); }
    feed(inst, 2, 120, 9); expect_add(ref, inst, 9);
    finish();
}
static void expiry(unsigned bit)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL);
    feed("Session", 15 ^ bit, 120, 9); feed("Session", bit, 2, 9);
    expect_add(ref, "Session", 9);
    ticks += 99; mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNSERR_NOTREADY);
    ticks++; mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNS_OK);
    CHECK(e.de_Code == MEVENT_REMOVE); finish();
}
static void goodbye(int rescue)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL); feed("Session", 15, 120, 9); expect_add(ref, "Session", 9);
    feed("Session", 1, 0, 9); CHECK(read_event(ref, &e) == MDNSERR_NOTREADY);
    ticks += 49;
    if (rescue) feed("Session", 1, 120, 9);
    ticks++; mdnsDiscoveryTick(&md);
    CHECK(read_event(ref, &e) == (rescue ? MDNSERR_NOTREADY : MDNS_OK));
    if (!rescue) CHECK(e.de_Code == MEVENT_REMOVE);
    finish();
}
static void updates(void)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL); feed("Session", 15, 120, 9); expect_add(ref, "Session", 9);
    ticks += 100; feed("Session", 8, 120, 10); expect_add(ref, "Session", 10);
    ticks += 50; mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNSERR_NOTREADY);
    finish();
}
static void lifecycle(void)
{
    APTR ref; struct MDNSIPCMsg msg = {0}; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL); feed("Session", 15, 120, 9);
    CHECK(call(&msg, MDNS_IPC_STOPBROWSE, ref, 2, 77) == MDNSERR_BADPARAM);
    CHECK(call(&msg, MDNS_IPC_READ, ref, 1, 78) == MDNSERR_NOENGINE);
    expect_add(ref, "Session", 9);
    CHECK(call(&msg, MDNS_IPC_STOPBROWSE, ref, 1, 77) == MDNS_OK);
    CHECK(read_event(ref, &e) == MDNSERR_BADPARAM);
    ref = begin(0, NULL); live_task_id = 2; mdnsDiscoveryTick(&md);
#ifdef MDNS_DISCOVERY_REAP
    /* ended owners go on the reap timer, not on every turn */
    live_task_id = 1; CHECK(read_event(ref, &e) != MDNSERR_BADPARAM);
    live_task_id = 2; mdnsDiscoveryReap(&md); live_task_id = 1;
#endif
    CHECK(read_event(ref, &e) == MDNSERR_BADPARAM);
    finish();
}
static void overflow(void)
{
    APTR ref; struct MDNSDiscoveryEvent e; unsigned i;
    init(); ref = begin(0, NULL);
    for (i = 0; i < 9; i++) { char inst[16]; snprintf(inst, sizeof(inst), "Session%u", i); feed(inst, 15, 120, 9); }
    CHECK(read_event(ref, &e) == MDNSERR_BUSY); finish();
}
static void resolving(int timeout)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ref = begin(1, "Session");
    if (!timeout) { feed("Session", 14, 120, 9); expect_add(ref, "Session", 9); }
    else { ticks += 250; mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNS_OK); CHECK(e.de_Code == MEVENT_RESOLVE_FAILED); }
    finish();
}
static void restart_network(void)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL); feed("Session", 15, 120, 9); expect_add(ref, "Session", 9);
    mdnsDiscoveryReset(&md); CHECK(read_event(ref, &e) == MDNS_OK); CHECK(e.de_Code == MEVENT_REMOVE);
    feed("Session", 15, 120, 10); expect_add(ref, "Session", 10); finish();
}
static void refresh(void)
{
    APTR ref; unsigned before;
    init(); ref = begin(0, NULL); feed("Session", 15, 60, 9); expect_add(ref, "Session", 9);
    before = packets; ticks += 2400; mdnsDiscoveryTick(&md); CHECK(packets >= before + 4); finish();
}

static void compressed(void)
{
    APTR ref; size_t at = 12, len, owner;
    init(); ref = begin(0, NULL); feed("Session", 14, 120, 9);
    memset(packet, 0, sizeof(packet)); put16(packet + 2, 0x8400); put16(packet + 6, 1);
    owner = at; at = key(packet, at, NULL, 0);
    put16(packet + at, DNS_TYPE_PTR); put16(packet + at + 2, 1); put32(packet + at + 4, 120);
    len = strlen("Session") + 3; put16(packet + at + 8, len); at += 10;
    packet[at++] = 7; memcpy(packet + at, "Session", 7); at += 7;
    packet[at++] = 0xc0; packet[at++] = owner;
    mdnsHandlePacket(&md, packet, at, 0xcb007101, 5353); mdnsDiscoveryTick(&md);
    expect_add(ref, "Session", 9); finish();
}
static void invalid_record(int kind)
{
    APTR ref; struct MDNSDiscoveryEvent e; size_t at = 12, len;
    init(); ref = begin(0, NULL); feed("Session", 14, 120, 9);
    memset(packet, 0, sizeof(packet)); put16(packet + 2, 0x8400); put16(packet + 6, 1);
    len = key(data, 0, "Session", 0);
    at = rr(at, NULL, DNS_TYPE_PTR, kind == 1 ? 3 : 1, 120, data, len);
    if (kind == 2) { /* compressed target cycle */
        size_t rdo = 12 + 19 + 10;
        packet[rdo] = 0xc0; packet[rdo + 1] = rdo;
    }
    if (kind == 3) { /* name consumes bytes beyond its own RDATA */
        put16(packet + 12 + 19 + 8, len - 1);
    }
    mdnsHandlePacket(&md, packet, at, 0xcb007101, kind == 4 ? 1234 : 5353);
    mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNSERR_NOTREADY); finish();
}
static void multiple_links(void)
{
    APTR ref;
    init(); md.md_IfCount = 2; md.md_Ifs[1].mi_Addr = 0xc0000302;
    ref = begin(0, NULL); feed("Session", 15, 120, 9); expect_add(ref, "Session", 9);
    md.md_SendIf = 1; feed("Session", 15, 120, 10); expect_add(ref, "Session", 10);
    md.md_SendIf = 0; feed("Session", 8, 120, 11); expect_add(ref, "Session", 11);
    finish();
}
static void wrap(void)
{
    APTR ref; struct MDNSDiscoveryEvent e;
    init(); ticks = 0xffffffd0u; ref = begin(0, NULL); feed("Session", 15, 2, 9); expect_add(ref, "Session", 9);
    ticks += 100; mdnsDiscoveryTick(&md); CHECK(read_event(ref, &e) == MDNS_OK); CHECK(e.de_Code == MEVENT_REMOVE); finish();
}
static void release_no_queries(void)
{
    APTR ref; struct MDNSIPCMsg msg = {0}; unsigned before;
    init(); ref = begin(0, NULL); feed("Session", 15, 60, 9); expect_add(ref, "Session", 9);
    CHECK(call(&msg, MDNS_IPC_RELEASE, NULL, 1, 0) == MDNS_OK);
    before = packets; ticks += 2500; mdnsDiscoveryTick(&md); CHECK(packets == before); finish();
}
static void missing_backoff(void)
{
    APTR ref; unsigned before;
    init(); ref = begin(0, NULL); (void)ref; feed("Session", 1, 120, 9);
    ticks += 50; mdnsDiscoveryTick(&md);
    before = packets; ticks += 50; mdnsDiscoveryTick(&md);
    CHECK(packets == before + 1); /* browse retry, no missing SRV/TXT flood */
    finish();
}
#ifdef MDNS_DISCOVERY_REAP
/* Review of #1695: what nobody browses is not cached, a full cache gives
 * way, and running out of views fails only that type's browses. */
static void unrelated_not_cached(void)
{
    APTR ref; unsigned i; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL);
    feed_type = "_printer._tcp.local";
    for (i = 0; i < 200; i++) { char inst[16]; snprintf(inst, sizeof(inst), "Printer%u", i); feed(inst, 15, 4500, 9); }
    CHECK(read_event(ref, &e) == MDNSERR_NOTREADY);
    feed_type = "_midi2._udp.local"; feed("Session", 15, 120, 9); expect_add(ref, "Session", 9);
    finish();
}
static void evict_unwanted(void)
{
    APTR printers, sessions; unsigned i; struct MDNSIPCMsg msg = {0};
    init(); printers = begin_type(0, NULL, "_printer._tcp");
    feed_type = "_printer._tcp.local";
    for (i = 0; i < 30; i++) {
        char inst[16]; snprintf(inst, sizeof(inst), "Printer%u", i);
        feed(inst, 15, 4500, 9);
        { struct MDNSDiscoveryEvent e; CHECK(read_event(printers, &e) == MDNS_OK); }
    }
    CHECK(call(&msg, MDNS_IPC_STOPBROWSE, printers, 1, 77) == MDNS_OK);
    sessions = begin(0, NULL); feed_type = "_midi2._udp.local";
    for (i = 0; i < 15; i++) {   /* more than the room the printers left */
        char inst[16]; snprintf(inst, sizeof(inst), "Session%u", i);
        feed(inst, 15, 120, 9); expect_add(sessions, inst, 9);
    }
    finish();
}
static void views_scoped(void)
{
    APTR sessions, printers; unsigned i; struct MDNSDiscoveryEvent e;
    init(); sessions = begin(0, NULL); printers = begin_type(0, NULL, "_printer._tcp");
    for (i = 0; i < 32; i++) {
        char inst[16]; snprintf(inst, sizeof(inst), "Session%u", i);
        feed(inst, 1, 120, 9);
    }
    /* Both PTRs are present before one rebuild. The overflowing type comes
     * first in the cache; its 32 views must be returned during this pass so
     * the later, small type can acquire one. */
    feed_records("Session32", 1, 120, 9, 0);
    feed_type = "_printer._tcp.local";
    feed_records("Printer", 1, 120, 9, 0);
    mdnsDiscoveryTick(&md);
    CHECK(read_event(sessions, &e) == MDNSERR_BUSY);
    feed("Printer", 14, 120, 9);
    CHECK(read_event(printers, &e) == MDNS_OK); CHECK(e.de_Code == MEVENT_ADD);
    CHECK(!strcmp(e.de_Name, "Printer"));
    finish();
}
/* An A that came within a second before the SRV naming its host is kept
 * for it; an older one is asked for again. */
static void a_before_srv(void)
{
    APTR ref; unsigned before; struct MDNSDiscoveryEvent e;
    init(); ref = begin(0, NULL);
    feed("Session", 8, 120, 9); ticks += 49; feed("Session", 7, 120, 9);
    expect_add(ref, "Session", 9); finish();
    init(); ref = begin(0, NULL);
    feed("Session", 8, 120, 9); ticks += 51; feed("Session", 7, 120, 9);
    CHECK(read_event(ref, &e) == MDNSERR_NOTREADY);
    before = packets; ticks += 50; mdnsDiscoveryTick(&md);
    CHECK(packets > before);     /* the A is asked for */
    feed("Session", 8, 120, 9); expect_add(ref, "Session", 9);
    finish();
}
#endif
#ifdef MDNS_IPC_NOTIFY
/* Lent requests: the device I/O contract of RequestMDNSEvent(). */
static struct MDNSEventRequest lent;
static LONG lend(APTR ref, ULONG ownerID, ULONG epoch, struct MDNSEventRequest *request)
{
    struct MDNSIPCMsg msg = {0};
    msg.mi_Request = request;
    return call(&msg, MDNS_IPC_NOTIFY, ref, ownerID, epoch);
}
static void notify_init(void)
{
    init(); replies = 0; memset(replied, 0, sizeof(replied));
    memset(&lent, 0, sizeof(lent)); lent.er_Result = 12345;
}
static void returned(unsigned count, LONG result)
{
    CHECK(replies == count);
    if (count) { CHECK(replied[count - 1] == &lent.er_Msg); CHECK(lent.er_Result == result); }
    else CHECK(lent.er_Result == 12345);
}
static void drain(APTR ref)
{
    struct MDNSDiscoveryEvent e; unsigned n = 0;
    while (read_event(ref, &e) == MDNS_OK) n++;
    CHECK(n > 0);
}
static void notify_pending(void)
{
    APTR ref;
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK); returned(0, 0);
    ticks += 50; mdnsDiscoveryTick(&md); returned(0, 0);
    feed("Session", 15, 120, 9); returned(1, MDNS_OK);
    feed("Other", 15, 120, 10); returned(1, MDNS_OK); /* once per lend */
    drain(ref);
    feed("Third", 15, 120, 11); returned(1, MDNS_OK); /* nothing lent */
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK); returned(2, MDNS_OK); /* already waiting */
    finish();
}
static void notify_stop(int eventFirst)
{
    APTR ref; struct MDNSIPCMsg msg = {0};
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    if (eventFirst) { feed("Session", 15, 120, 9); returned(1, MDNS_OK); }
    CHECK(call(&msg, MDNS_IPC_STOPBROWSE, ref, 1, 77) == MDNS_OK);
    returned(1, eventFirst ? MDNS_OK : MDNSERR_CANCELLED);
    feed("Other", 15, 120, 10); mdnsDiscoveryCancel(&md); returned(1, eventFirst ? MDNS_OK : MDNSERR_CANCELLED);
    finish();
}
static void notify_refused(void)
{
    APTR ref; struct MDNSEventRequest second; struct MDNSIPCMsg msg = {0};
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, NULL) == MDNSERR_BADPARAM);
    CHECK(lend(ref, 2, 77, &lent) == MDNSERR_BADPARAM);           /* not the owner */
    CHECK(lend(ref, 1, 78, &lent) == MDNSERR_NOENGINE);           /* old daemon */
    CHECK(lend((APTR)999, 1, 77, &lent) == MDNSERR_BADPARAM);     /* no such operation */
    returned(0, 0);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    memset(&second, 0, sizeof(second));
    CHECK(lend(ref, 1, 77, &second) == MDNSERR_BADPARAM);         /* one per operation */
    CHECK(call(&msg, MDNS_IPC_STOPBROWSE, ref, 1, 77) == MDNS_OK);
    returned(1, MDNSERR_CANCELLED); finish();
}
static void notify_overflow(void)
{
    APTR ref; unsigned i;
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    feed("Session0", 15, 120, 9); returned(1, MDNS_OK);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK); returned(2, MDNS_OK);
    for (i = 1; i < 9; i++) { char inst[16]; snprintf(inst, sizeof(inst), "Session%u", i); feed(inst, 15, 120, 9); }
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK); returned(3, MDNSERR_BUSY);
    finish();
}
static void notify_shutdown(void)
{
    APTR first, second; struct MDNSEventRequest other;
    notify_init(); first = begin(0, NULL); second = begin(1, "Session");
    memset(&other, 0, sizeof(other));
    CHECK(lend(first, 1, 77, &lent) == MDNS_OK); CHECK(lend(second, 1, 77, &other) == MDNS_OK);
    mdnsDiscoveryCancel(&md);
    CHECK(replies == 2); CHECK(lent.er_Result == MDNSERR_NOENGINE); CHECK(other.er_Result == MDNSERR_NOENGINE);
    mdnsDiscoveryCancel(&md); CHECK(replies == 2); finish();
}
static void notify_release(void)
{
    APTR ref; struct MDNSIPCMsg msg = {0};
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    CHECK(call(&msg, MDNS_IPC_RELEASE, NULL, 1, 0) == MDNS_OK);
    feed("Session", 15, 120, 9); mdnsDiscoveryCancel(&md);
    returned(0, 0); finish();
}
static void notify_vanished(void)
{
    APTR ref;
    notify_init(); ref = begin(0, NULL);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    live_task_id = 2;
    feed("Session", 15, 120, 9); mdnsDiscoveryCancel(&md);
    returned(0, 0); finish();
}
static void notify_resolve(void)
{
    APTR ref;
    notify_init(); ref = begin(1, "Session");
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK);
    ticks += 250; mdnsDiscoveryTick(&md); returned(1, MDNS_OK);
    { struct MDNSDiscoveryEvent e; CHECK(read_event(ref, &e) == MDNS_OK); CHECK(e.de_Code == MEVENT_RESOLVE_FAILED); }
    finish();
}
static void notify_reset(void)
{
    APTR ref;
    notify_init(); ref = begin(0, NULL); feed("Session", 15, 120, 9); drain(ref);
    CHECK(lend(ref, 1, 77, &lent) == MDNS_OK); returned(0, 0);
    mdnsDiscoveryReset(&md); returned(1, MDNS_OK); finish();
}
#endif
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (!strcmp(argv[1], "labels-order-txt")) labels();
    else if (!strcmp(argv[1], "expire-ptr")) expiry(1);
    else if (!strcmp(argv[1], "expire-srv")) expiry(2);
    else if (!strcmp(argv[1], "expire-txt")) expiry(4);
    else if (!strcmp(argv[1], "expire-a")) expiry(8);
    else if (!strcmp(argv[1], "goodbye")) goodbye(0);
    else if (!strcmp(argv[1], "rescue")) goodbye(1);
    else if (!strcmp(argv[1], "update")) updates();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "overflow")) overflow();
    else if (!strcmp(argv[1], "resolve")) resolving(0);
    else if (!strcmp(argv[1], "timeout")) resolving(1);
    else if (!strcmp(argv[1], "network-reset")) restart_network();
    else if (!strcmp(argv[1], "refresh")) refresh();
    else if (!strcmp(argv[1], "compressed")) compressed();
    else if (!strcmp(argv[1], "wrong-class")) invalid_record(1);
    else if (!strcmp(argv[1], "pointer-cycle")) invalid_record(2);
    else if (!strcmp(argv[1], "short-rdata")) invalid_record(3);
    else if (!strcmp(argv[1], "wrong-port")) invalid_record(4);
    else if (!strcmp(argv[1], "multiple-links")) multiple_links();
    else if (!strcmp(argv[1], "clock-wrap")) wrap();
    else if (!strcmp(argv[1], "release-quiet")) release_no_queries();
    else if (!strcmp(argv[1], "missing-backoff")) missing_backoff();
#ifdef MDNS_DISCOVERY_REAP
    else if (!strcmp(argv[1], "unrelated-not-cached")) unrelated_not_cached();
    else if (!strcmp(argv[1], "evict-unwanted")) evict_unwanted();
    else if (!strcmp(argv[1], "views-scoped")) views_scoped();
    else if (!strcmp(argv[1], "a-before-srv")) a_before_srv();
#endif
#ifdef MDNS_IPC_NOTIFY
    else if (!strcmp(argv[1], "notify-pending")) notify_pending();
    else if (!strcmp(argv[1], "notify-stop")) notify_stop(0);
    else if (!strcmp(argv[1], "notify-event-then-stop")) notify_stop(1);
    else if (!strcmp(argv[1], "notify-refused")) notify_refused();
    else if (!strcmp(argv[1], "notify-overflow")) notify_overflow();
    else if (!strcmp(argv[1], "notify-shutdown")) notify_shutdown();
    else if (!strcmp(argv[1], "notify-release")) notify_release();
    else if (!strcmp(argv[1], "notify-vanished")) notify_vanished();
    else if (!strcmp(argv[1], "notify-resolve")) notify_resolve();
    else if (!strcmp(argv[1], "notify-reset")) notify_reset();
#endif
    else return 2;
    printf("discovery %s: PASS\n", argv[1]); return 0;
}
