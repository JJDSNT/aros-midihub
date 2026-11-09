/* Exercise actual #1591 packet/registration/host/DNS code on the host.
 * Fixtures encode wire input independently; networking/time/Exec are mocked.
 * Exit 1 means a contract failed, 2 means invalid setup.
 */
#include "mdns_exec_mock.h"
#include "mdnsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>

static ULONG ticks = 5000;
static unsigned packets, answer_records, announcements, zero_host_addresses;
#ifdef MDNS_IPV6_CORE
static unsigned host_aaaa_records;
static unsigned ipv4_packets, ipv6_packets, dual_packets;
#endif
#ifdef MDNS_SUBTYPE_CORE
static unsigned subtype_ptr_records, subtype_goodbyes;
#endif
static unsigned a_questions, aaaa_questions, srv_questions, txt_questions;
static struct Task owner;
static ULONG live_task_id = 1;

struct Task *FindTaskByPID(ULONG id) { return id == live_task_id ? &owner : NULL; }
/* Lent requests come back here; the fixtures check order and count. */
static struct Message *replied[16];
static unsigned replies;
void ReplyMsg(struct Message *message)
{
    if (replies < 16) replied[replies] = message;
    replies++;
}

static void own_message(struct MDNSIPCMsg *message)
{
#if MDNS_IPC_VERSION >= 2
    message->mi_TaskID = 1;
#else
    message->mi_Task = &owner;
#endif
}
static UBYTE rx[MDNS_MAX_PACKET], tx[MDNS_MAX_PACKET];

void NewList(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}
void AddTail(struct List *list, struct Node *node)
{
    node->ln_Succ = (struct Node *)&list->lh_Tail;
    node->ln_Pred = list->lh_TailPred;
    list->lh_TailPred->ln_Succ = node;
    list->lh_TailPred = node;
}
void Remove(struct Node *node)
{
    node->ln_Pred->ln_Succ = node->ln_Succ;
    node->ln_Succ->ln_Pred = node->ln_Pred;
}
struct Node *RemHead(struct List *list)
{
    struct Node *node = list->lh_Head;
    if (!node->ln_Succ)
        return NULL;
    Remove(node);
    return node;
}
APTR AllocMem(size_t size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR memory, size_t size) { (void)size; free(memory); }
LONG Stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
LONG Strnicmp(const char *a, const char *b, LONG n) { return strncasecmp(a, b, (size_t)n); }
ULONG mdnsTicks(void) { return ticks; }
ULONG mdnsNow(void) { return ticks / 50; }

static void configure_links(struct MDNSDaemon *md, BOOL second, BOOL ipv4,
                            BOOL ipv6)
{
    struct dnssd_link_spec links[2];
    struct dnssd_address_spec addresses[4];
    size_t link_count = second ? 2u : 1u;
    size_t address_count = 0u;

    memset(links, 0, sizeof(links));
    memset(addresses, 0, sizeof(addresses));
    links[0].interface_index = 1;
    links[0].flags = DNSSD_LINK_UP | DNSSD_LINK_MULTICAST;
    links[0].name_length = 4;
    memcpy(links[0].name, "eth0", 4);
    if (ipv4) {
        addresses[address_count].interface_index = 1;
        addresses[address_count].family = DNSSD_ADDRESS_IPV4;
        addresses[address_count].length = 4;
        addresses[address_count].bytes[0] = 192;
        addresses[address_count].bytes[1] = 0;
        addresses[address_count].bytes[2] = 2;
        addresses[address_count].bytes[3] = 2;
        ++address_count;
    }
    if (second) {
        links[1].interface_index = 2;
        links[1].flags = DNSSD_LINK_UP | DNSSD_LINK_MULTICAST;
        links[1].name_length = 4;
        memcpy(links[1].name, "eth1", 4);
        addresses[address_count].interface_index = 2;
        addresses[address_count].family = DNSSD_ADDRESS_IPV4;
        addresses[address_count].length = 4;
        addresses[address_count].bytes[0] = 198;
        addresses[address_count].bytes[1] = 51;
        addresses[address_count].bytes[2] = 100;
        addresses[address_count].bytes[3] = 2;
        ++address_count;
    }
    if (ipv6) {
        addresses[address_count].interface_index = 1;
        addresses[address_count].scope_id = 1;
        addresses[address_count].family = DNSSD_ADDRESS_IPV6;
        addresses[address_count].length = 16;
        addresses[address_count].bytes[0] = 0xfe;
        addresses[address_count].bytes[1] = 0x80;
        addresses[address_count].bytes[15] = 2;
        ++address_count;
    }
    if (dnssd_link_inventory_reconcile(&md->md_LinkInventory,
            links, link_count, addresses, address_count) != DNSSD_LINK_OK)
        abort();
    md->md_IfCount = (LONG)link_count;
    md->md_Ifs[0].mi_Index = 1;
    if (second)
        md->md_Ifs[1].mi_Index = 2;
}

ULONG mdnsNetAddress4(const struct MDNSDaemon *md, LONG slot)
{
    uint32_t index;
    size_t i;

    if (slot < 0 || slot >= md->md_IfCount)
        return 0;
    index = md->md_Ifs[slot].mi_Index;
    for (i = 0; i < md->md_LinkInventory.address_count; ++i) {
        const struct dnssd_address_entry *entry =
            &md->md_LinkInventory.addresses[i];

        if (entry->value.interface_index == index &&
            entry->value.family == DNSSD_ADDRESS_IPV4)
            return ((ULONG)entry->value.bytes[0] << 24) |
                   ((ULONG)entry->value.bytes[1] << 16) |
                   ((ULONG)entry->value.bytes[2] << 8) |
                   (ULONG)entry->value.bytes[3];
    }
    return 0;
}

BOOL mdnsNetAddress6(const struct MDNSDaemon *md, LONG slot,
                     struct in6_addr *address)
{
    uint32_t index;
    size_t i;

    memset(address, 0, sizeof(*address));
    if (slot < 0 || slot >= md->md_IfCount)
        return FALSE;
    index = md->md_Ifs[slot].mi_Index;
    for (i = 0; i < md->md_LinkInventory.address_count; ++i) {
        const struct dnssd_address_entry *entry =
            &md->md_LinkInventory.addresses[i];

        if (entry->value.interface_index == index &&
            entry->value.family == DNSSD_ADDRESS_IPV6) {
            memcpy(address, entry->value.bytes, sizeof(*address));
            return TRUE;
        }
    }
    return FALSE;
}

BOOL mdnsNetIsOwn(struct MDNSDaemon *md, ULONG address)
{
    LONG i;
    for (i = 0; i < md->md_IfCount; ++i)
        if (mdnsNetAddress4(md, i) == address)
            return TRUE;
    return FALSE;
}
#ifdef MDNS_IPV6_CORE
BOOL mdnsNetIsOwn6(struct MDNSDaemon *md, const struct in6_addr *address)
{
    LONG i;
    for (i = 0; i < md->md_IfCount; ++i) {
        struct in6_addr own;

        if (mdnsNetAddress6(md, i, &own) &&
            !memcmp(&own, address, sizeof(*address)))
            return TRUE;
    }
    return FALSE;
}
#endif
void mdnsNetNameHost(struct MDNSDaemon *md)
{ snprintf(md->md_HostName, sizeof(md->md_HostName), "arosbox-%u", md->md_HostSuffix); }
BOOL mdnsNetSendOn(struct MDNSDaemon *md, LONG index, const UBYTE *packet, LONG length)
{
    const struct MDNSHeader *header = (const struct MDNSHeader *)packet;
    LONG offset = sizeof(*header), consumed;
    unsigned i, answers = ntohs(header->ancount);
    (void)md;
    (void)index;
    ++packets;
#ifdef MDNS_IPV6_CORE
    if (md->md_SendFamily == AF_INET)
        ++ipv4_packets;
    else if (md->md_SendFamily == AF_INET6)
        ++ipv6_packets;
    else
        ++dual_packets;
#endif
    answer_records += answers;
    if (ntohs(header->flags) & DNS_FLAG_QR)
        ++announcements;
    for (i = 0; i < ntohs(header->qdcount); ++i) {
        struct MDNSQuestion question;
        if (mdnsParseQuestion(packet, length, offset, &question, &consumed))
            abort();
        if (question.qtype == DNS_TYPE_SRV)
            ++srv_questions;
        else if (question.qtype == DNS_TYPE_TXT)
            ++txt_questions;
        else if (question.qtype == DNS_TYPE_A)
            ++a_questions;
#ifdef MDNS_IPV6_CORE
        else if (question.qtype == DNS_TYPE_AAAA)
            ++aaaa_questions;
#endif
        offset += consumed;
    }
    for (i = 0; i < answers; ++i) {
        struct MDNSRecord record;
        if (mdnsParseRecord(packet, length, offset, &record, &consumed))
            abort();
        if (record.type == DNS_TYPE_A && !strcmp(record.name, "arosbox.local") && !record.ttl)
            ++zero_host_addresses;
#ifdef MDNS_IPV6_CORE
        if (record.type == DNS_TYPE_AAAA &&
            !strcmp(record.name, "arosbox.local"))
            ++host_aaaa_records;
#endif
#ifdef MDNS_SUBTYPE_CORE
        if (record.type == DNS_TYPE_PTR &&
            (!strcmp(record.name, "_L3840._sub._midi2._udp.local") ||
             !strcmp(record.name, "_I001122._sub._midi2._udp.local"))) {
            ++subtype_ptr_records;
            if (record.ttl == 0)
                ++subtype_goodbyes;
        }
#endif
        offset += consumed;
    }
    return TRUE;
}
void mdnsNetSendAll(struct MDNSDaemon *md, const UBYTE *packet, LONG length)
{
    LONG i;
    for (i = 0; i < md->md_IfCount; ++i)
        if (md->md_SendIf < 0 || md->md_SendIf == i)
            mdnsNetSendOn(md, i, packet, length);
}

static void put16(UBYTE *p, UWORD n) { p[0] = n >> 8; p[1] = n; }
static void put32(UBYTE *p, ULONG n)
{ p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n; }
static size_t name(UBYTE *packet, size_t at, const char *text)
{
    const char *dot;
    do {
        size_t length;
        dot = strchr(text, '.');
        length = dot ? (size_t)(dot - text) : strlen(text);
        if (!length || length > 63 || at + 1 + length >= MDNS_MAX_PACKET)
            abort();
        packet[at++] = (UBYTE)length;
        memcpy(packet + at, text, length);
        at += length;
        if (dot)
            text = dot + 1;
    } while (dot);
    packet[at++] = 0;
    return at;
}
static size_t question_name(UBYTE *packet, const char *owner, UWORD type,
                            UWORD cls, UWORD flags)
{
    size_t at;
    memset(packet, 0, MDNS_MAX_PACKET);
    put16(packet + 2, flags);
    put16(packet + 4, 1);
    at = name(packet, 12, owner);
    put16(packet + at, type);
    put16(packet + at + 2, cls);
    return at + 4;
}
static size_t question(UBYTE *packet, UWORD type, UWORD cls, UWORD flags)
{
    return question_name(packet, type == DNS_TYPE_PTR ?
        "_midi2._udp.local" : "Session._midi2._udp.local", type, cls,
        flags);
}
static size_t record(UBYTE *packet, size_t at, const char *owner_name,
                     UWORD type, UWORD cls, ULONG ttl, const UBYTE *data, size_t length)
{
    at = name(packet, at, owner_name);
    put16(packet + at, type);
    put16(packet + at + 2, cls);
    put32(packet + at + 4, ttl);
    put16(packet + at + 8, (UWORD)length);
    memcpy(packet + at + 10, data, length);
    return at + 10 + length;
}
static size_t srv(UBYTE *data, UWORD port)
{
    memset(data, 0, 6);
    put16(data + 4, port);
    return name(data, 6, "arosbox.local");
}
static struct MDNSRegisteredService *setup(struct MDNSDaemon *md, ULONG ttl)
{
    struct MDNSIPCMsg message;
    struct MDNSRegisteredService *service;
    memset(md, 0, sizeof(*md));
    NEWLIST(&md->md_Services);
    if (dnssd_link_inventory_init(&md->md_LinkInventory,
            md->md_Links, md->md_LinkScratch, MDNS_IFS_MAX,
            md->md_Addresses, md->md_AddressScratch,
            MDNS_ADDRESS_MAX) != DNSSD_LINK_OK)
        abort();
    configure_links(md, FALSE, TRUE, FALSE);
    md->md_HostAddr = 0xc0000202;
    md->md_Socket = 0;
    md->md_SendIf = 0;
    md->md_HostState = MDNS_NAME_ANNOUNCED;
    strcpy(md->md_HostName, "arosbox");
    md->md_TxBuf = tx;
    md->md_RxBuf = rx;
    md->md_NextHandle = 1;
    memset(&message, 0, sizeof(message));
    strcpy(message.mi_Name, "Session");
    strcpy(message.mi_ServiceType, "_midi2._udp");
    strcpy(message.mi_Domain, "local");
    strcpy(message.mi_TXT, "v=a");
    message.mi_Port = 5673;
    message.mi_TTL = ttl;
    own_message(&message);
    if (mdnsRegAdd(md, &message))
        abort();
    service = (struct MDNSRegisteredService *)md->md_Services.lh_Head;
    service->rs_State = MDNS_NAME_ANNOUNCED;
    service->rs_Step = MDNS_ANNOUNCE_COUNT;
    packets = answer_records = announcements = zero_host_addresses = 0;
#ifdef MDNS_IPV6_CORE
    host_aaaa_records = 0;
    ipv4_packets = ipv6_packets = dual_packets = 0;
#endif
#ifdef MDNS_SUBTYPE_CORE
    subtype_ptr_records = subtype_goodbyes = 0;
#endif
    a_questions = aaaa_questions = srv_questions = txt_questions = 0;
    return service;
}
static void receive(struct MDNSDaemon *md, const UBYTE *packet, size_t length)
{ mdnsHandlePacket(md, packet, (LONG)length, 0xc0000201, MDNS_DNS_PORT); }
#ifdef MDNS_IPV6_CORE
static void receive6(struct MDNSDaemon *md, const UBYTE *packet, size_t length)
{
    struct in6_addr source;

    memset(&source, 0, sizeof(source));
    source.s6_addr[0] = 0xfe;
    source.s6_addr[1] = 0x80;
    source.s6_addr[15] = 1;
    md->md_SendFamily = AF_INET6;
    mdnsHandlePacket6(md, packet, (LONG)length, &source, MDNS_DNS_PORT);
    md->md_SendFamily = 0;
}
#endif
static void receive_from_port(struct MDNSDaemon *md, const UBYTE *packet,
                              size_t length, UWORD port)
{ mdnsHandlePacket(md, packet, (LONG)length, 0xc0000201, port); }
static void flush(struct MDNSDaemon *md)
{ ticks += 10; mdnsRegTick(md); }

#ifdef MDNS_IPV6_CORE
static void enable_ipv6(struct MDNSDaemon *md)
{
    configure_links(md, FALSE, TRUE, TRUE);
}
#endif

#ifdef MDNS_SUBTYPE_CORE
static void enable_subtypes(struct MDNSRegisteredService *service)
{
    service->rs_SubtypeCount = 2;
    strcpy(service->rs_Subtypes[0], "_L3840");
    strcpy(service->rs_Subtypes[1], "_I001122");
}
#endif

int main(int argc, char **argv)
{
    struct MDNSDaemon md;
    struct MDNSRegisteredService *service;
    UBYTE packet[MDNS_MAX_PACKET], data[512];
    size_t at, length;
    int failure = 2;
    if (argc != 2)
        return 2;
    service = setup(&md, 60);
#ifdef MDNS_IPV6_CORE
    if (!strcmp(argv[1], "host-aaaa")) {
        enable_ipv6(&md);
        at = question_name(packet, "arosbox.local", DNS_TYPE_AAAA,
            DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        printf("AAAA host query: packets=%u, AAAA records=%u\n",
            packets, host_aaaa_records);
        failure = packets != 1 || host_aaaa_records != 1;
    } else if (!strcmp(argv[1], "host-conflict-aaaa")) {
        static const UBYTE peer[16] = {
            0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 99
        };

        enable_ipv6(&md);
        memset(packet, 0, 12);
        put16(packet + 2, DNS_FLAG_QR | DNS_FLAG_AA);
        put16(packet + 6, 1);
        at = record(packet, 12, "arosbox.local", DNS_TYPE_AAAA,
            DNS_CLASS_IN_FLUSH, 120, peer, sizeof(peer));
        receive(&md, packet, at);
        printf("AAAA conflict: hostname=%s, state=%u\n", md.md_HostName,
            md.md_HostState);
        failure = strcmp(md.md_HostName, "arosbox-2") != 0 ||
            md.md_HostState != MDNS_NAME_PROBING;
    } else if (!strcmp(argv[1], "service-aaaa")) {
        enable_ipv6(&md);
        mdnsRegAnnounceAll(&md);
        printf("service announcement: packets=%u, AAAA records=%u\n",
            packets, host_aaaa_records);
        failure = packets != 1 || host_aaaa_records != 1;
    } else if (!strcmp(argv[1], "shared-ipv6-family")) {
        enable_ipv6(&md);
        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive6(&md, packet, at);
        flush(&md);
        printf("delayed IPv6 browse: v4=%u, v6=%u, dual=%u\n",
            ipv4_packets, ipv6_packets, dual_packets);
        failure = packets != 1 || ipv4_packets != 0 || ipv6_packets != 1 ||
            dual_packets != 0;
    } else
#endif
#ifdef MDNS_SUBTYPE_CORE
    if (!strcmp(argv[1], "subtype-announce")) {
        enable_subtypes(service);
        mdnsRegAnnounceAll(&md);
        printf("subtype announcement: packets=%u, subtype PTRs=%u\n",
            packets, subtype_ptr_records);
        failure = packets != 2 || subtype_ptr_records != 2;
    } else if (!strcmp(argv[1], "subtype-query")) {
        enable_subtypes(service);
        at = question_name(packet, "_L3840._sub._midi2._udp.local",
            DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        printf("subtype query: packets=%u, subtype PTRs=%u\n",
            packets, subtype_ptr_records);
        failure = packets != 2 || subtype_ptr_records != 1;
    } else if (!strcmp(argv[1], "subtype-goodbye")) {
        struct MDNSIPCMsg message = {0};

        enable_subtypes(service);
        message.mi_Handle = service->rs_ClientRef;
        own_message(&message);
        mdnsRegRemove(&md, &message);
        printf("subtype goodbye: PTRs=%u, zero TTL=%u\n",
            subtype_ptr_records, subtype_goodbyes);
        failure = subtype_ptr_records != 2 || subtype_goodbyes != 2;
        service = NULL;
    } else if (!strcmp(argv[1], "subtype-validation")) {
        struct MDNSIPCMsg message = {0};
        LONG invalid, duplicate;

        strcpy(message.mi_Name, "Other");
        strcpy(message.mi_ServiceType, "_midi2._udp");
        message.mi_Port = 6000;
        message.mi_SubtypeCount = 1;
        strcpy(message.mi_Subtypes[0], "bad.label");
        own_message(&message);
        invalid = mdnsRegAdd(&md, &message);
        strcpy(message.mi_Subtypes[0], "_L3840");
        message.mi_SubtypeCount = 2;
        strcpy(message.mi_Subtypes[1], "_l3840");
        duplicate = mdnsRegAdd(&md, &message);
        printf("subtype validation: invalid=%ld duplicate=%ld\n",
            (long)invalid, (long)duplicate);
        failure = invalid != MDNSERR_BADPARAM ||
            duplicate != MDNSERR_BADPARAM;
    } else
#endif
    if (!strcmp(argv[1], "known-short") || !strcmp(argv[1], "known-expiring") ||
        !strcmp(argv[1], "known-default") || !strcmp(argv[1], "known-wrong-owner")) {
        const int default_ttl = !strcmp(argv[1], "known-default");
        const int expiring = !strcmp(argv[1], "known-expiring");
        const int wrong_owner = !strcmp(argv[1], "known-wrong-owner");
        service->rs_TTL = default_ttl || wrong_owner ? 0 : 60;
        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        length = name(data, 0, "Session._midi2._udp.local");
        at = record(packet, at, wrong_owner ? "_unrelated._udp.local" : "_midi2._udp.local",
                    DNS_TYPE_PTR, DNS_CLASS_IN, default_ttl || wrong_owner ? 3000 :
                    expiring ? 20 : 40, data, length);
        put16(packet + 6, 1);
        receive(&md, packet, at);
        flush(&md);
        printf("query with known PTR: responses=%u, records=%u\n", packets, answer_records);
        failure = expiring || wrong_owner ? packets == 0 : packets != 0;
    } else if (!strcmp(argv[1], "identical-srv") || !strcmp(argv[1], "different-srv")) {
        const int identical = !strcmp(argv[1], "identical-srv");
        memset(packet, 0, sizeof(packet));
        put16(packet + 2, DNS_FLAG_QR | DNS_FLAG_AA);
        put16(packet + 6, 1);
        length = srv(data, identical ? 5673 : 6000);
        at = record(packet, 12, "Session._midi2._udp.local", DNS_TYPE_SRV,
                    DNS_CLASS_IN_FLUSH, 60, data, length);
        receive(&md, packet, at);
        printf("after SRV: instance='%s', state=%u\n", service->rs_Instance, service->rs_State);
        failure = identical ? strcmp(service->rs_Instance, "Session") != 0 ||
            service->rs_State != MDNS_NAME_ANNOUNCED : service->rs_State != MDNS_NAME_PROBING;
    } else if (!strcmp(argv[1], "class-in") || !strcmp(argv[1], "class-any") ||
               !strcmp(argv[1], "class-ch") || !strcmp(argv[1], "opcode") ||
               !strcmp(argv[1], "rcode")) {
        const int invalid_class = !strcmp(argv[1], "class-ch");
        const int bad_opcode = !strcmp(argv[1], "opcode");
        const int bad_rcode = !strcmp(argv[1], "rcode");
        UWORD cls = invalid_class ? 3 : !strcmp(argv[1], "class-any") ? 255 : DNS_CLASS_IN;
        at = question(packet, DNS_TYPE_SRV, cls, bad_opcode ? 0x0800 : bad_rcode ? 1 : 0);
        receive(&md, packet, at);
        flush(&md);
        printf("query: qclass=%u, flags=%04x, responses=%u\n", cls,
               bad_opcode ? 0x0800 : bad_rcode ? 1 : 0, packets);
        failure = invalid_class || bad_opcode || bad_rcode ? packets != 0 : packets == 0;
    } else if (!strcmp(argv[1], "withdraw-service")) {
        struct MDNSIPCMsg message;
        memset(&message, 0, sizeof(message));
        message.mi_Handle = service->rs_ClientRef;
        own_message(&message);
        mdnsRegRemove(&md, &message);
        printf("service removed, host still announced=%d, host A goodbyes=%u\n",
               md.md_HostState == MDNS_NAME_ANNOUNCED, zero_host_addresses);
        failure = packets == 0 || zero_host_addresses != 0;
    } else if (!strcmp(argv[1], "announce-initial")) {
        unsigned i;
        service->rs_State = MDNS_NAME_PROBING;
        service->rs_Step = 0;
        for (i = 0; i < 4; ++i) {
            mdnsRegTick(&md);
            ticks += MDNS_PROBE_TICKS;
        }
        ticks += 50;
        mdnsRegTick(&md);
        printf("initial registration: announcements=%u\n", announcements);
        failure = announcements != 2;
    } else if (!strcmp(argv[1], "update-port")) {
        struct MDNSIPCMsg message;
        memset(&message, 0, sizeof(message));
        message.mi_Handle = service->rs_ClientRef;
        own_message(&message);
        message.mi_Port = 6000;
        if (mdnsRegUpdate(&md, &message))
            abort();
        ticks += 50;
        mdnsRegTick(&md);
        ticks += 50;
        mdnsRegTick(&md);
        printf("after port update: port=%u, announcements=%u\n", service->rs_Port, announcements);
        failure = service->rs_Port != 6000 || announcements < 2;
    } else if (!strcmp(argv[1], "update-unchanged")) {
        /* An update that changes nothing, the way a client asks whether
           its handle is still known, sends nothing; one that changes the
           port is announced (RFC 6762 sections 8.3 and 8.4). */
        struct MDNSIPCMsg message;
        unsigned quiet;
        memset(&message, 0, sizeof(message));
        message.mi_Handle = service->rs_ClientRef;
        own_message(&message);
        if (mdnsRegUpdate(&md, &message))
            abort();
        message.mi_Port = service->rs_Port;
        if (mdnsRegUpdate(&md, &message))
            abort();
        ticks += 50;
        mdnsRegTick(&md);
        ticks += 50;
        mdnsRegTick(&md);
        quiet = announcements;
        message.mi_Port = (UWORD)(service->rs_Port + 1);
        if (mdnsRegUpdate(&md, &message))
            abort();
        ticks += 50;
        mdnsRegTick(&md);
        printf("unchanged update: announcements=%u; changed port: %u\n",
               quiet, announcements);
        failure = quiet != 0 || announcements == 0;
    } else if (!strcmp(argv[1], "probe-txt")) {
        static const UBYTE peer_txt[] = {3, 'v', '=', 'z'};
        service->rs_State = MDNS_NAME_PROBING;
        service->rs_Step = 1;
        service->rs_NextTick = ticks + MDNS_PROBE_TICKS;
        at = question(packet, DNS_TYPE_ANY, DNS_CLASS_IN, 0);
        at = record(packet, at, "Session._midi2._udp.local", DNS_TYPE_TXT,
                    DNS_CLASS_IN, 60, peer_txt, sizeof(peer_txt));
        length = srv(data, 5673);
        at = record(packet, at, "Session._midi2._udp.local", DNS_TYPE_SRV,
                    DNS_CLASS_IN, 60, data, length);
        put16(packet + 8, 2);
        receive(&md, packet, at);
        printf("probe with later TXT: step=%u, retry in %u ticks\n", service->rs_Step,
               service->rs_NextTick - ticks);
        failure = service->rs_Step != 0 || service->rs_NextTick < ticks + 50;
    } else if (!strcmp(argv[1], "legacy-multiple")) {
        struct MDNSIPCMsg message;
        struct MDNSRegisteredService *second;

        memset(&message, 0, sizeof(message));
        strcpy(message.mi_Name, "Second");
        strcpy(message.mi_ServiceType, "_midi2._udp");
        strcpy(message.mi_Domain, "local");
        message.mi_Port = 5674;
        message.mi_TTL = 60;
        own_message(&message);
        if (mdnsRegAdd(&md, &message))
            abort();
        second = (struct MDNSRegisteredService *)md.md_Services.lh_TailPred;
        second->rs_State = MDNS_NAME_ANNOUNCED;
        second->rs_Step = MDNS_ANNOUNCE_COUNT;

        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        put16(packet, 0x1234);
        receive_from_port(&md, packet, at, 49152);
        printf("legacy browse with two services: packets=%u, records=%u\n",
               packets, answer_records);
        failure = packets != 1 || answer_records != 7;
    } else if (!strcmp(argv[1], "shared-delay")) {
        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        failure = packets != 0;
        flush(&md);
        printf("shared browse: immediate=%u, after delay=%u\n",
               failure != 0, packets);
        failure |= packets != 1;
    } else if (!strcmp(argv[1], "shared-repeat")) {
        mdnsRegAnnounceAll(&md);
        packets = answer_records = announcements = 0;
        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        printf("shared browse inside one second: responses=%u\n", packets);
        failure = packets != 0;
        ticks += 50;
        receive(&md, packet, at);
        flush(&md);
        printf("shared browse after one second: responses=%u\n", packets);
        failure |= packets != 1;
    } else if (!strcmp(argv[1], "shared-interface")) {
        configure_links(&md, TRUE, TRUE, FALSE);
        md.md_SendIf = 0;
        mdnsRegAnnounceAll(&md);
        packets = answer_records = announcements = 0;
        md.md_SendIf = 1;
        at = question(packet, DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        printf("shared browse on another interface: responses=%u\n", packets);
        failure = packets != 1;
    } else if (!strcmp(argv[1], "types-delay")) {
        at = question_name(packet, "_services._dns-sd._udp.local",
            DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        failure = packets != 0;
        flush(&md);
        printf("service-type browse: immediate=%u, after delay=%u\n",
               failure != 0, packets);
        failure |= packets != 1;
    } else if (!strcmp(argv[1], "types-legacy")) {
        at = question_name(packet, "_services._dns-sd._udp.local",
            DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        put16(packet, 0x1234);
        receive_from_port(&md, packet, at, 49152);
        failure = packets != 1;
        flush(&md);
        printf("legacy service-type browse: responses=%u\n", packets);
        failure |= packets != 1;
    } else if (!strcmp(argv[1], "types-repeat")) {
        at = question_name(packet, "_services._dns-sd._udp.local",
            DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        failure = packets != 1;
        packets = answer_records = announcements = 0;
        receive(&md, packet, at);
        flush(&md);
        printf("service-type browse inside one second: responses=%u\n",
               packets);
        failure |= packets != 0;
        ticks += 50;
        receive(&md, packet, at);
        flush(&md);
        printf("service-type browse after one second: responses=%u\n",
               packets);
        failure |= packets != 1;
    } else if (!strcmp(argv[1], "types-interface")) {
        configure_links(&md, TRUE, TRUE, FALSE);
        md.md_SendIf = 0;
        at = question_name(packet, "_services._dns-sd._udp.local",
            DNS_TYPE_PTR, DNS_CLASS_IN, 0);
        receive(&md, packet, at);
        flush(&md);
        packets = answer_records = announcements = 0;
        md.md_SendIf = 1;
        receive(&md, packet, at);
        flush(&md);
        printf("service-type browse on another interface: responses=%u\n",
               packets);
        failure = packets != 1;
    }
#if MDNS_IPC_VERSION >= 2
    if (!strcmp(argv[1], "identical-txt") || !strcmp(argv[1], "goodbye-txt") ||
        !strcmp(argv[1], "goodbye-srv")) {
        const int goodbye = strcmp(argv[1], "identical-txt") != 0;
        const int is_srv = !strcmp(argv[1], "goodbye-srv");
        memset(packet, 0, 12);
        put16(packet + 2, 0x8400);
        put16(packet + 6, 1);
        if (is_srv)
            length = srv(data, 9999);
        else {
            memcpy(data, goodbye ? "\003v=z" : "\003v=a", 4);
            length = 4;
        }
        at = record(packet, 12, "Session._midi2._udp.local",
            is_srv ? DNS_TYPE_SRV : DNS_TYPE_TXT, DNS_CLASS_IN_FLUSH,
            goodbye ? 0 : 60, data, length);
        receive(&md, packet, at);
        failure = strcmp(service->rs_Instance, "Session") != 0 ||
            service->rs_State != MDNS_NAME_ANNOUNCED;
    } else if (!strcmp(argv[1], "compressed-srv")) {
        memset(packet, 0, 12);
        put16(packet + 2, 0x8400);
        put16(packet + 4, 1);
        put16(packet + 6, 1);
        at = name(packet, 12, "AROSBOX.local");
        put16(packet + at, DNS_TYPE_A);
        put16(packet + at + 2, DNS_CLASS_IN);
        at += 4;
        memset(data, 0, 8);
        put16(data + 4, 5673);
        put16(data + 6, 0xc00c);
        at = record(packet, at, "Session._midi2._udp.local", DNS_TYPE_SRV,
            DNS_CLASS_IN_FLUSH, 60, data, 8);
        receive(&md, packet, at);
        failure = strcmp(service->rs_Instance, "Session") != 0;
    } else if (!strcmp(argv[1], "local-rename")) {
        struct MDNSIPCMsg msg;
        unsigned suffix;
        memset(&msg, 0, sizeof(msg));
        own_message(&msg);
        strcpy(msg.mi_ServiceType, "_midi2._udp");
        msg.mi_Port = 5674;
        for (suffix = 2; suffix <= 3; suffix++) {
            snprintf(msg.mi_Name, sizeof(msg.mi_Name), "Session (%u)", suffix);
            if (mdnsRegAdd(&md, &msg))
                abort();
        }
        memset(packet, 0, 12);
        put16(packet + 2, 0x8400);
        put16(packet + 6, 1);
        length = srv(data, 9999);
        at = record(packet, 12, "Session._midi2._udp.local", DNS_TYPE_SRV,
            DNS_CLASS_IN_FLUSH, 60, data, length);
        receive(&md, packet, at);
        failure = strcmp(service->rs_Instance, "Session (4)") != 0;
    } else if (!strcmp(argv[1], "reject-instance")) {
        struct MDNSIPCMsg msg;
        memset(&msg, 0, sizeof(msg));
        own_message(&msg);
        strcpy(msg.mi_ServiceType, "_midi2._udp");
        strcpy(msg.mi_Name, "Mr. Printer");
        msg.mi_Port = 5674;
        failure = mdnsRegAdd(&md, &msg) != MDNSERR_BADPARAM;
        memset(msg.mi_Name, 'A', 64);
        msg.mi_Name[64] = 0;
        failure |= mdnsRegAdd(&md, &msg) != MDNSERR_BADPARAM;
    } else if (!strcmp(argv[1], "proxy-own-name")) {
        struct MDNSIPCMsg msg;
        memset(&msg, 0, sizeof(msg));
        own_message(&msg);
        strcpy(msg.mi_ServiceType, "_midi2._udp");
        strcpy(msg.mi_Name, "Proxy");
        strcpy(msg.mi_HostName, "AROSBOX.local.");
        msg.mi_HostAddr = 0xc000024d;
        msg.mi_Port = 5674;
        failure = mdnsRegAdd(&md, &msg) != MDNSERR_BADPARAM;
        strcpy(md.md_HostName, "arosbox-2");
        strcpy(md.md_HostBase, "arosbox");
        failure |= mdnsRegAdd(&md, &msg) != MDNSERR_BADPARAM;
        strcpy(msg.mi_HostName, "printer.example");
        failure |= mdnsRegAdd(&md, &msg) != MDNSERR_BADPARAM;
        strcpy(msg.mi_HostName, "printer.local");
        failure |= mdnsRegAdd(&md, &msg) != MDNS_OK;
    } else if (!strcmp(argv[1], "restart-probes")) {
        unsigned i;
        mdnsRegRestartAll(&md);
        md.md_IfCount = 0;
        mdnsRegTick(&md);
        failure = service->rs_Step != 0;
        md.md_IfCount = 1;
        md.md_HostState = MDNS_NAME_PROBING;
        mdnsRegTick(&md);
        failure |= service->rs_Step != 0;
        md.md_HostState = MDNS_NAME_ANNOUNCED;
        for (i = 0; i < 3; i++) {
            mdnsRegTick(&md);
            ticks += MDNS_PROBE_TICKS;
        }
        failure |= service->rs_State != MDNS_NAME_PROBING || announcements != 0;
        mdnsRegTick(&md);
        failure |= service->rs_State != MDNS_NAME_ANNOUNCED || announcements != 1;
    } else if (!strcmp(argv[1], "task-ids")) {
        struct MDNSIPCMsg msg;
        mdnsClientReap(&md);
        failure = md.md_Services.lh_Head != &service->rs_Node;
        memset(&msg, 0, sizeof(msg));
        msg.mi_TaskID = live_task_id = 2;
        strcpy(msg.mi_Name, "New task at same address");
        strcpy(msg.mi_ServiceType, "_midi2._udp");
        msg.mi_Port = 5674;
        if (mdnsRegAdd(&md, &msg))
            abort();
        mdnsClientReap(&md);
        service = (struct MDNSRegisteredService *)md.md_Services.lh_Head;
        failure |= service->rs_TaskID != 2 || service->rs_Node.ln_Succ->ln_Succ != NULL;
    } else if (!strcmp(argv[1], "update-ttl")) {
        struct MDNSIPCMsg msg;
        memset(&msg, 0, sizeof(msg));
        own_message(&msg);
        msg.mi_Handle = service->rs_ClientRef;
        msg.mi_Port = 6000;
        msg.mi_TTL = 4501;
        failure = mdnsRegUpdate(&md, &msg) != MDNSERR_BADPARAM || service->rs_Port != 5673;
        msg.mi_TTL = 60;
        failure |= mdnsRegUpdate(&md, &msg) != MDNS_OK || service->rs_TTL != 60;
    }
    if (argc == 2)
        printf("review fixture %s: %s\n", argv[1], failure ? "FAIL" : "PASS");
#endif
    mdnsRegFree(&md);
    return failure;
}
