#include "netmidi2/netmidi2.h"
#include "ump/ump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

/* What each side's sink received */
struct inbox {
    uint32_t words[256];
    unsigned count;
};

static void take(void *context, const uint32_t *words, size_t count)
{
    struct inbox *box = context;
    memcpy(box->words + box->count, words, count * sizeof(*words));
    box->count += (unsigned)count;
}

static struct netmidi2_session host, client;
static struct inbox host_in, client_in;
static uint32_t now;

/* Delivers a packet built by one side to the other, then any reply back,
   until neither has anything more to say. drop drops the first packet. */
static void exchange(struct netmidi2_writer *first, int from_client, int drop)
{
    uint8_t buffers[2][NETMIDI2_MAX_PACKET];
    struct netmidi2_writer packet = *first, reply;
    int to_host = from_client, rounds = 0;

    if (drop)
        return;
    while (packet.length > 4 && rounds++ < 8) {
        netmidi2_writer_init(&reply, buffers[rounds & 1], sizeof(buffers[0]));
        CHECK(netmidi2_receive(to_host ? &host : &client, packet.data, packet.length,
                             now, &reply) == 0);
        packet = reply;
        to_host = !to_host;
    }
}

static void test_codec(void)
{
    uint8_t buffer[256];
    struct netmidi2_writer w;
    struct netmidi2_command_packet commands[4];
    char name[NETMIDI2_NAME_MAX + 1], id[NETMIDI2_PRODUCT_ID_MAX + 1];
    static const uint8_t spec_example[] = {
        /* Appendix A.1.1: Invitation from "MyDev", Product Instance Id
           "8shYe3h5", no authentication */
        0x4d, 0x49, 0x44, 0x49,
        0x01, 0x04, 0x02, 0x00,
        'M', 'y', 'D', 'e', 'v', 0, 0, 0,
        '8', 's', 'h', 'Y', 'e', '3', 'h', '5'
    };
    int n;

    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_add_identity(&w, NETMIDI2_INVITATION, 0, "MyDev", "8shYe3h5") == 0);
    CHECK(w.length == sizeof(spec_example) && !memcmp(buffer, spec_example, w.length));

    n = netmidi2_parse(spec_example, sizeof(spec_example), commands, 4);
    CHECK(n == 1 && commands[0].code == NETMIDI2_INVITATION && commands[0].csd1 == 2);
    CHECK(netmidi2_identity(&commands[0], name, sizeof(name), id, sizeof(id)) == 0);
    CHECK(!strcmp(name, "MyDev") && !strcmp(id, "8shYe3h5"));

    /* Appendix A.1.2: UMP Data, sequence 16, a Timing Clock in group 0 */
    {
        static const uint8_t expected[] = {
            0x4d, 0x49, 0x44, 0x49, 0xff, 0x01, 0x00, 0x10, 0x10, 0xf8, 0x00, 0x00
        };
        uint32_t clock = 0x10f80000u;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_add_ump(&w, 16, &clock, 1) == 0);
        CHECK(w.length == sizeof(expected) && !memcmp(buffer, expected, w.length));
    }

    /* Appendix A.1.3, figure 14: one command, sequence 17, with a MIDI 2.0
       Note On and Note Off; and A.1.4, figure 15: the same two messages
       as two commands, sequences 0x3456 and 0x3457. */
    {
        static const uint8_t one_command[] = {
            0x4d, 0x49, 0x44, 0x49, 0xff, 0x04, 0x00, 0x11,
            0x46, 0x90, 0x40, 0x00, 0x12, 0x34, 0x00, 0x00,
            0x46, 0x80, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00
        };
        static const uint8_t two_commands[] = {
            0x4d, 0x49, 0x44, 0x49,
            0xff, 0x02, 0x34, 0x56,
            0x46, 0x90, 0x40, 0x00, 0x12, 0x34, 0x00, 0x00,
            0xff, 0x02, 0x34, 0x57,
            0x46, 0x80, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00
        };
        static const uint32_t notes[] = {
            0x46904000u, 0x12340000u, 0x46804000u, 0x01000000u
        };
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_add_ump(&w, 17, notes, 4) == 0);
        CHECK(w.length == sizeof(one_command) &&
              !memcmp(buffer, one_command, w.length));
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_add_ump(&w, 0x3456, notes, 2) == 0);
        CHECK(netmidi2_add_ump(&w, 0x3457, notes + 2, 2) == 0);
        CHECK(w.length == sizeof(two_commands) &&
              !memcmp(buffer, two_commands, w.length));
        n = netmidi2_parse(two_commands, sizeof(two_commands), commands, 4);
        CHECK(n == 2 && commands[1].code == NETMIDI2_UMP_DATA &&
              commands[1].csd1 == 0x34 && commands[1].csd2 == 0x57 &&
              commands[1].payload_words == 2);
    }
    /* Bad signature and truncated commands */
    CHECK(netmidi2_parse((const uint8_t *)"MIDX", 4, commands, 4) == -1);
    CHECK(netmidi2_parse(spec_example, sizeof(spec_example) - 1, commands, 4) == -1);
}

static void test_session(void)
{
    uint8_t buffer[NETMIDI2_MAX_PACKET];
    struct netmidi2_writer w;
    uint32_t note[2] = { 0x20903c64u, 0x20803c40u };
    uint32_t discovery[4] = { 0xf0000101u, 0x0000001fu, 0, 0 };
    unsigned i;

    now = 1000;
    netmidi2_session_init(&host, 1, "AROS MIDIHub", "aros-pi3-1", take, &host_in);
    netmidi2_session_init(&client, 0, "Test Client", "client-1", take, &client_in);

    /* Invitation, accepted */
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_invite(&client, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == NETMIDI2_ESTABLISHED && client.state == NETMIDI2_ESTABLISHED);
    CHECK(!strcmp(host.peer_name, "Test Client") && !strcmp(client.peer_name, "AROS MIDIHub"));

    /* Client to host: UMPs arrive once, in order */
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_send_ump(&client, note, 2, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host_in.count == 2 && host_in.words[0] == note[0] && host_in.words[1] == note[1]);

    /* FEC: lose two packets; the third carries both again */
    host_in.count = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t word = 0x20900000u | (i << 8) | 0x40;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, i < 2);
    }
    CHECK(host_in.count == 3);
    CHECK(host_in.words[0] == 0x20900040u && host_in.words[2] == 0x20900240u);
    CHECK(host.rx_lost == 0);

    /* Repeats of packets already received are skipped */
    host_in.count = 0;
    {
        uint32_t word = 0x20900340u;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, 0);
    }
    CHECK(host_in.count == 1 && host_in.words[0] == 0x20900340u);

    /* Endpoint Discovery is answered by the host: Info, Name, Product
       Instance Id, Stream Configuration */
    client_in.count = 0;
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_send_ump(&client, discovery, 4, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(client_in.count == 0);         /* stream replies are not passed on as MIDI */
    CHECK(client.rx_started && client.rx_expected == 4);

    /* A Stream Configuration Request for timestamps both ways (UMP
       specification 7.1.6.2): none are taken unless the caller uses
       them, and none are ever sent. */
    {
        uint32_t request[4] = { 0xf0050103u, 0, 0, 0 };

        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, request, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(host.jr == 0);
        host.jr_offered = UMP_JR_RECEIVE;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, request, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(host.jr == UMP_JR_RECEIVE);
        request[0] = 0xf0050100u;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, request, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(host.jr == 0);
        CHECK(client_in.count == 0);
    }

    /* Function Block Discovery (UMP specification 7.1.7): the one block
       is told of, info and name each in a UMP Data command of its own;
       a block that is not there is not answered for. */
    {
        uint32_t ask[4] = { 0xf0100003u, 0, 0, 0 };
        uint16_t before = client.rx_expected;

        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, ask, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(client.rx_expected == (uint16_t)(before + 2));
        ask[0] = 0xf010ff01u;               /* all of them, info only */
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, ask, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(client.rx_expected == (uint16_t)(before + 3));
        ask[0] = 0xf0100103u;               /* block 1: there is none */
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, ask, 4, now, &w) == 0);
        exchange(&w, 1, 0);
        CHECK(client.rx_expected == (uint16_t)(before + 3));
        CHECK(client_in.count == 0);
    }

    /* Idle: after data the sender says it has none, repeating for FEC */
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    now += 150;
    CHECK(netmidi2_tick(&client, now, &w) == 1);
    exchange(&w, 1, 0);
    CHECK(host.state == NETMIDI2_ESTABLISHED && host.rx_lost == 0);

    /* Ping when quiet; the peer answers */
    now += 6000;
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_tick(&host, now, &w) == 1);
    exchange(&w, 0, 0);
    CHECK(host.pings_unanswered == 0);

    /* No answers: Bye Timeout after the ping limit */
    for (i = 0; i < 4; ++i) {
        now += 6000;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        (void)netmidi2_tick(&host, now, &w);
    }
    CHECK(host.state == NETMIDI2_CLOSING);

    /* Bye from the client ends both sides */
    netmidi2_session_init(&host, 1, "AROS MIDIHub", "aros-pi3-1", take, &host_in);
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_invite(&client, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == NETMIDI2_ESTABLISHED);
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_bye(&client, NETMIDI2_BYE_USER_TERMINATED, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == NETMIDI2_IDLE && client.state == NETMIDI2_IDLE);

    /* UMP Data outside a session: Bye Session Not Established */
    {
        uint32_t word = 0x20900040u;
        struct netmidi2_writer reply;
        uint8_t out[64];
        struct netmidi2_command_packet commands[4];
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        netmidi2_add_ump(&w, 0, &word, 1);
        netmidi2_writer_init(&reply, out, sizeof(out));
        CHECK(netmidi2_receive(&host, buffer, w.length, now, &reply) == 0);
        CHECK(netmidi2_parse(out, reply.length, commands, 4) == 1);
        CHECK(commands[0].code == NETMIDI2_BYE &&
              commands[0].csd1 == NETMIDI2_BYE_NOT_ESTABLISHED);
    }

    /* An unknown command is answered with NAK Command Not Supported */
    {
        struct netmidi2_writer reply;
        uint8_t out[64];
        struct netmidi2_command_packet commands[4];
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        netmidi2_add(&w, 0x55, 0, 0, NULL, 0);
        netmidi2_writer_init(&reply, out, sizeof(out));
        CHECK(netmidi2_receive(&host, buffer, w.length, now, &reply) == 0);
        CHECK(netmidi2_parse(out, reply.length, commands, 4) == 1);
        CHECK(commands[0].code == NETMIDI2_NAK &&
              commands[0].csd1 == NETMIDI2_NAK_NOT_SUPPORTED &&
              commands[0].payload[0] == 0x55);
    }
}

/* One command sent to a session from a given peer; the reply's first
   command is left in reply_command, reply_count says how many came. */
static struct netmidi2_command_packet reply_command;
static int reply_count;
static uint8_t reply_bytes[256];

static void send_command(struct netmidi2_session *to, uint64_t from,
                         uint8_t code, uint8_t csd1, uint8_t csd2,
                         const uint8_t *payload, size_t payload_bytes)
{
    uint8_t in[256];
    struct netmidi2_writer packet, reply;
    struct netmidi2_command_packet commands[8];

    netmidi2_writer_init(&packet, in, sizeof(in));
    CHECK(netmidi2_add(&packet, code, csd1, csd2, payload, payload_bytes) == 0);
    netmidi2_writer_init(&reply, reply_bytes, sizeof(reply_bytes));
    CHECK(netmidi2_receive_from(to, from, in, packet.length, now, &reply) == 0);
    reply_count = netmidi2_parse(reply_bytes, reply.length, commands, 8);
    memset(&reply_command, 0, sizeof(reply_command));
    if (reply_count > 0)
        reply_command = commands[0];
}

static int replied(uint8_t code, uint8_t csd1)
{
    return reply_count == 1 && reply_command.code == code &&
           reply_command.csd1 == csd1;
}

/* What the specification says of each command out of its place. */
static void test_against_specification(void)
{
    uint8_t buffer[NETMIDI2_MAX_PACKET];
    struct netmidi2_writer w;
    static const uint8_t identity[] = {
        'O', 't', 'h', 'r', 'i', 'd', '-', '2'
    };
    static const uint8_t ping_id[] = { 1, 2, 3, 4 };
    static const uint8_t data[] = { 0x20, 0x90, 0x3c, 0x64 };
    static const uint8_t retransmit[] = { 0, 0, 0, 0 };
    const uint64_t first = 0x0a00000113c4u, second = 0x0a00000213c4u;
    uint32_t word = 0x20903c64u;
    unsigned i;

    now = 50000;
    netmidi2_session_init(&host, 1, "AROS MIDIHub", "aros-1", take, &host_in);
    netmidi2_session_init(&client, 0, "Test Client", "client-1", take, &client_in);

    /* Sections 3.2 and 6.4: a session is with one address and port. */
    send_command(&host, first, NETMIDI2_INVITATION, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_INVITATION_ACCEPTED, 3));
    CHECK(host.state == NETMIDI2_ESTABLISHED && host.peer == first);
    CHECK(!strcmp(host.peer_name, "Othr") && !strcmp(host.peer_product_id, "id-2"));
    /* the same Client again: accepted again, the session untouched */
    send_command(&host, first, NETMIDI2_UMP_DATA, 0, 0, data, 4);
    CHECK(host.rx_expected == 1);
    send_command(&host, first, NETMIDI2_INVITATION, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_INVITATION_ACCEPTED, 3) && host.rx_expected == 1);
    /* another Client: refused, and nothing of the session moves */
    host_in.count = 0;
    now += 100;
    send_command(&host, second, NETMIDI2_INVITATION, 1, 0,
                 (const uint8_t *)"EvilEvil", 8);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_TOO_MANY_SESSIONS));
    CHECK(host.peer == first && !strcmp(host.peer_name, "Othr"));
    send_command(&host, second, NETMIDI2_UMP_DATA, 0, 1, data, 4);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED));
    CHECK(host_in.count == 0 && host.rx_expected == 1);
    send_command(&host, second, NETMIDI2_SESSION_RESET, 0, 0, NULL, 0);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED) &&
          host.resets == 0);
    send_command(&host, second, NETMIDI2_BYE, NETMIDI2_BYE_USER_TERMINATED, 0,
                 NULL, 0);
    CHECK(replied(NETMIDI2_BYE_REPLY, 0) && host.state == NETMIDI2_ESTABLISHED);
    send_command(&host, second, NETMIDI2_PING, 0, 0, ping_id, 4);
    CHECK(replied(NETMIDI2_PING_REPLY, 0));
    /* and a stranger's packets do not keep the session alive */
    CHECK(host.last_receive_ms == now - 100);

    /* Section 7.2.3: no retransmission here, and the peer is told so. */
    send_command(&host, first, NETMIDI2_RETRANSMIT_REQUEST, 0, 0, retransmit, 4);
    CHECK(replied(NETMIDI2_NAK, NETMIDI2_NAK_NOT_SUPPORTED) &&
          reply_command.payload[0] == NETMIDI2_RETRANSMIT_REQUEST);
    CHECK(host.state == NETMIDI2_ESTABLISHED);

    /* Section 6.6: an Invitation Reply to a Host. */
    send_command(&host, first, NETMIDI2_INVITATION_PENDING, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_NAK, NETMIDI2_NAK_NOT_EXPECTED));
    /* Sections 6.9 and 6.10: authentication we never asked for. */
    netmidi2_session_init(&host, 1, "AROS MIDIHub", "aros-1", take, &host_in);
    send_command(&host, first, NETMIDI2_INVITATION_AUTH, 0, 0, identity, 8);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_AUTH_WITHOUT_INVITATION));
    CHECK(host.state == NETMIDI2_IDLE);

    /* Outside a session (6.11, 6.12, 7.2.3, 7.2.4): Bye, not established;
       a Bye is still answered (6.16). */
    send_command(&host, first, NETMIDI2_SESSION_RESET, 0, 0, NULL, 0);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED));
    send_command(&host, first, NETMIDI2_SESSION_RESET_REPLY, 0, 0, NULL, 0);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED));
    send_command(&host, first, NETMIDI2_RETRANSMIT_REQUEST, 0, 0, retransmit, 4);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED));
    send_command(&host, first, NETMIDI2_RETRANSMIT_ERROR, 1, 0, retransmit, 4);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NOT_ESTABLISHED));
    send_command(&host, first, NETMIDI2_BYE, 0, 0, NULL, 0);
    CHECK(replied(NETMIDI2_BYE_REPLY, 0));

    /* Sections 6.5 to 6.8 at a Client that invited nobody. */
    send_command(&client, first, NETMIDI2_INVITATION_ACCEPTED, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NO_PENDING_SESSION));
    send_command(&client, first, NETMIDI2_INVITATION_PENDING, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_NO_PENDING_SESSION));
    send_command(&client, first, NETMIDI2_INVITATION_AUTH_REQUIRED, 1, 0,
                 identity, 8);
    CHECK(replied(NETMIDI2_NAK, NETMIDI2_NAK_NOT_EXPECTED));
    send_command(&client, first, NETMIDI2_INVITATION, 1, 0, identity, 8);
    CHECK(replied(NETMIDI2_NAK, NETMIDI2_NAK_NOT_EXPECTED));
    /* asked to authenticate, which we said we cannot: the invitation is
       withdrawn */
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_invite_to(&client, first, now, &w) == 0);
    send_command(&client, first, NETMIDI2_INVITATION_AUTH_REQUIRED, 1, 0,
                 identity, 8);
    CHECK(replied(NETMIDI2_BYE, NETMIDI2_BYE_INVITATION_CANCELED));
    CHECK(client.state == NETMIDI2_CLOSING);

    /* Section 6.4: an invitation nobody answers ends with Bye, Invitation
       Canceled, which is then repeated like any Bye. */
    netmidi2_session_init(&client, 0, "Test Client", "client-1", take, &client_in);
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_invite_to(&client, first, now, &w) == 0);
    for (i = 0; i < 12 && client.state == NETMIDI2_INVITING; ++i) {
        now += 1000;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        (void)netmidi2_tick(&client, now, &w);
    }
    CHECK(client.state == NETMIDI2_CLOSING && w.length == 8 &&
          buffer[4] == NETMIDI2_BYE &&
          buffer[6] == NETMIDI2_BYE_INVITATION_CANCELED);

    /* A new pair for the rest. */
    netmidi2_session_init(&host, 1, "AROS MIDIHub", "aros-1", take, &host_in);
    netmidi2_session_init(&client, 0, "Test Client", "client-1", take, &client_in);
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_invite(&client, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == NETMIDI2_ESTABLISHED && client.state == NETMIDI2_ESTABLISHED);

    /* Section 7.2.2: a packet that has no room for the repeats and the
       new command is not written at all. */
    for (i = 0; i < 2; ++i) {
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, 0);
    }
    {
        uint8_t small[4 + 8 + 8 + 7];
        uint16_t sequence = client.tx_sequence;
        netmidi2_writer_init(&w, small, sizeof(small));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == -1);
        CHECK(w.length == 4 && client.tx_sequence == sequence);
    }

    /* A loss longer than the repeats: the caller is told, once. */
    host_in.count = 0;
    for (i = 0; i < 5; ++i) {
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, i < 4);
    }
    /* five sent, four lost: the last brings two of them back */
    CHECK(host_in.count == 3 && host.gaps == 1 && host.rx_lost == 2);

    /* Sections 6.11 and 6.12: a reset. No data until the reply; then both
       ends count from zero and repeat nothing from before. */
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_reset(&client, now, &w) == 0);
    CHECK(client.state == NETMIDI2_RESETTING);
    {
        uint8_t other[64];
        struct netmidi2_writer refused;
        netmidi2_writer_init(&refused, other, sizeof(other));
        CHECK(netmidi2_send_ump(&client, &word, 1, now, &refused) == -1);
    }
    exchange(&w, 1, 0);
    CHECK(client.state == NETMIDI2_ESTABLISHED && client.resets == 1);
    CHECK(host.state == NETMIDI2_ESTABLISHED && host.resets == 1);
    CHECK(client.tx_sequence == 0 && client.sent_count == 0 &&
          host.tx_sequence == 0 && host.rx_expected == 0);
    host_in.count = 0;
    netmidi2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(netmidi2_send_ump(&client, &word, 1, now, &w) == 0);
    CHECK(w.length == 12 && buffer[6] == 0 && buffer[7] == 0);
    exchange(&w, 1, 0);
    CHECK(host_in.count == 1 && host.gaps == 1);
    /* a reply to a reset never asked for: ask for one */
    send_command(&host, 0, NETMIDI2_SESSION_RESET_REPLY, 0, 0, NULL, 0);
    CHECK(replied(NETMIDI2_SESSION_RESET, 0) &&
          host.state == NETMIDI2_RESETTING);
    /* and one that is never answered ends the session: Bye, Timeout */
    for (i = 0; i < 12 && host.state == NETMIDI2_RESETTING; ++i) {
        now += 1000;
        netmidi2_writer_init(&w, buffer, sizeof(buffer));
        (void)netmidi2_tick(&host, now, &w);
    }
    CHECK(host.state == NETMIDI2_CLOSING && w.length == 8 &&
          buffer[4] == NETMIDI2_BYE && buffer[6] == NETMIDI2_BYE_TIMEOUT);
}

/* Several sessions on one port (section 3.2). */
static unsigned table_counts[NETMIDI2_SESSIONS_MAX];
static uint32_t table_last[NETMIDI2_SESSIONS_MAX];

static void table_take(void *context, int index, const uint32_t *words,
                       size_t count)
{
    (void)context;
    table_counts[index] += (unsigned)count;
    table_last[index] = words[count - 1];
}

static int table_reply_is(const struct netmidi2_writer *reply, uint8_t code,
                          uint8_t csd1)
{
    struct netmidi2_command_packet commands[4];
    return netmidi2_parse(reply->data, reply->length, commands, 4) == 1 &&
           commands[0].code == code && commands[0].csd1 == csd1;
}

/* Appendix A.2 and A.3 of M2-124-UM: figures 16 and 18 as sequences of
   packets. */
static uint32_t fec_got[16];
static unsigned fec_count;

static void fec_take(void *context, int index, const uint32_t *words,
                     size_t count)
{
    size_t i;
    (void)context;
    (void)index;
    for (i = 0; i < count && fec_count < 16; ++i)
        fec_got[fec_count++] = words[i];
}

/* The sequence numbers of a packet's UMP Data commands and whether each is
   empty, as "n" or "nz", with commas between. */
static const char *fec_shape(const uint8_t *packet, size_t length)
{
    static char text[64];
    struct netmidi2_command_packet commands[8];
    int n = netmidi2_parse(packet, length, commands, 8);
    int i;
    size_t at = 0;

    text[0] = 0;
    for (i = 0; i < n; ++i) {
        if (commands[i].code != NETMIDI2_UMP_DATA)
            continue;
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%u%s",
                               at ? "," : "",
                               (unsigned)((commands[i].csd1 << 8) |
                                          commands[i].csd2),
                               commands[i].payload_words ? "" : "z");
    }
    return text;
}

static void test_appendix_fec(void)
{
    static struct netmidi2_table host, client;
    static uint8_t sent[5][256];
    size_t sent_length[5];
    uint8_t in[256], out[256];
    struct netmidi2_writer packet, reply;
    struct netmidi2_session *session;
    int index;
    unsigned i;

    now = 500000;
    netmidi2_table_init(&host, "Host", "h", fec_take, NULL);
    netmidi2_table_init(&client, "Client", "c", fec_take, NULL);
    host.accept = 1;
    netmidi2_writer_init(&packet, in, sizeof(in));
    CHECK(netmidi2_table_invite(&client, 1, now, &packet) == 0);
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&host, 2, in, packet.length, now, &reply,
                                 &index) == 0);
    netmidi2_writer_init(&packet, in, sizeof(in));
    CHECK(netmidi2_table_receive(&client, 1, out, reply.length, now, &packet,
                                 &index) == 0);
    session = netmidi2_table_session(&client, 0);
    CHECK(session && session->state == NETMIDI2_ESTABLISHED);

    /* Figure 16: a Client with two repeats. Each packet has the command
       that is new and the two before it, in the order they were sent. */
    {
        static const char *const shape[5] = {
            "0", "0,1", "0,1,2", "1,2,3", "2,3,4"
        };

        for (i = 0; i < 5; ++i) {
            uint32_t word = 0x20900040u | ((uint32_t)(60 + i) << 8);

            netmidi2_writer_init(&packet, sent[i], sizeof(sent[i]));
            CHECK(netmidi2_send_ump(session, &word, 1, now, &packet) == 0);
            sent_length[i] = packet.length;
            CHECK(!strcmp(fec_shape(sent[i], sent_length[i]), shape[i]));
            now += 5;
        }
        /* "if UDP Packets #2 and #3 were dropped, then the receiver can
           safely continue ... without any loss of data" */
        fec_count = 0;
        for (i = 0; i < 5; ++i) {
            if (i == 1 || i == 2)
                continue;
            netmidi2_writer_init(&reply, out, sizeof(out));
            CHECK(netmidi2_table_receive(&host, 2, sent[i], sent_length[i],
                                         now, &reply, &index) == 0);
        }
        CHECK(fec_count == 5);
        for (i = 0; i < 5; ++i)
            CHECK(fec_got[i] == (0x20900040u | ((uint32_t)(60 + i) << 8)));
        CHECK(netmidi2_table_session(&host, 0)->gaps == 0);
    }

    /* Figure 18, the second of the two ways to go idle: each packet has a
       new Zero Length command and the two commands before it. Figure 17,
       which repeats one Zero Length command and lets the repeats run out,
       is the other; a receiver takes either, since it goes by sequence
       numbers. */
    {
        static const char *const shape[3] = {"3,4,5z", "4,5z,6z", "5z,6z,7z"};

        for (i = 0; i < 3; ++i) {
            now += 150;
            netmidi2_writer_init(&packet, in, sizeof(in));
            CHECK(netmidi2_table_tick(&client, 0, now, &packet) == 1);
            CHECK(!strcmp(fec_shape(in, packet.length), shape[i]));
            netmidi2_writer_init(&reply, out, sizeof(out));
            CHECK(netmidi2_table_receive(&host, 2, in, packet.length, now,
                                         &reply, &index) == 0);
        }
        CHECK(fec_count == 5);          /* nothing new was delivered */
        CHECK(netmidi2_table_session(&host, 0)->gaps == 0);
    }
}

static void test_table(void)
{
    static struct netmidi2_table table;
    uint8_t in[256], out[256];
    struct netmidi2_writer packet, reply;
    int index;
    unsigned i;
    uint32_t word;

    now = 90000;
    netmidi2_table_init(&table, "AROS MIDIHub", "aros-1", table_take, NULL);

    /* as many Clients as there are slots, each known by where it sends from */
    for (i = 0; i < NETMIDI2_SESSIONS_MAX; ++i) {
        netmidi2_writer_init(&packet, in, sizeof(in));
        netmidi2_add_identity(&packet, NETMIDI2_INVITATION, 0, "Client", "c");
        netmidi2_writer_init(&reply, out, sizeof(out));
        CHECK(netmidi2_table_receive(&table, 1000 + i, in, packet.length, now,
                                     &reply, &index) == 0);
        CHECK(index == (int)i && table_reply_is(&reply,
                  NETMIDI2_INVITATION_ACCEPTED, 3));
        CHECK(netmidi2_table_find(&table, 1000 + i) == (int)i);
    }
    /* one more is told there is no room, and takes none */
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 5000, in, packet.length, now, &reply,
                                 &index) == 0);
    CHECK(index == -1 && table_reply_is(&reply, NETMIDI2_BYE,
              NETMIDI2_BYE_TOO_MANY_SESSIONS));
    CHECK(netmidi2_table_find(&table, 5000) == -1);
    /* each Client's data goes to its own slot, with its own sequence */
    for (i = 0; i < NETMIDI2_SESSIONS_MAX; ++i) {
        word = 0x20900040u | (i << 8);
        netmidi2_writer_init(&packet, in, sizeof(in));
        netmidi2_add_ump(&packet, 0, &word, 1);
        netmidi2_writer_init(&reply, out, sizeof(out));
        CHECK(netmidi2_table_receive(&table, 1000 + i, in, packet.length, now,
                                     &reply, &index) == 0);
        CHECK(index == (int)i && table_counts[i] == 1 &&
              table_last[i] == word);
    }
    /* data from someone with no session: Bye, not established */
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 5000, in, packet.length, now, &reply,
                                 &index) == 0);
    CHECK(index == -1 && table_reply_is(&reply, NETMIDI2_BYE,
              NETMIDI2_BYE_NOT_ESTABLISHED));
    /* a Client leaves: its slot is free at the next tick, the others stay */
    netmidi2_writer_init(&packet, in, sizeof(in));
    netmidi2_add(&packet, NETMIDI2_BYE, NETMIDI2_BYE_USER_TERMINATED, 0, NULL, 0);
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 1003, in, packet.length, now, &reply,
                                 &index) == 0);
    CHECK(index == 3 && table_reply_is(&reply, NETMIDI2_BYE_REPLY, 0));
    netmidi2_writer_init(&reply, out, sizeof(out));
    (void)netmidi2_table_tick(&table, 3, now, &reply);
    CHECK(!netmidi2_table_session(&table, 3) &&
          netmidi2_table_session(&table, 2) &&
          netmidi2_table_find(&table, 1003) == -1);
    /* and the one that was refused can have it */
    netmidi2_writer_init(&packet, in, sizeof(in));
    netmidi2_add_identity(&packet, NETMIDI2_INVITATION, 0, "Late", "l");
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 5000, in, packet.length, now, &reply,
                                 &index) == 0);
    CHECK(index == 3 && !strcmp(netmidi2_table_session(&table, 3)->peer_name,
                                "Late"));
    /* a stranger's packet that starts nothing leaves no slot behind */
    netmidi2_writer_init(&packet, in, sizeof(in));
    netmidi2_add(&packet, NETMIDI2_BYE, 0, 0, NULL, 0);
    netmidi2_writer_init(&reply, out, sizeof(out));
    netmidi2_writer_init(&reply, out, sizeof(out));
    (void)netmidi2_table_tick(&table, 0, now, &reply);
    netmidi2_writer_init(&packet, in, sizeof(in));
    netmidi2_add(&packet, NETMIDI2_BYE, 0, 0, NULL, 0);
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 1000, in, packet.length, now, &reply,
                                 &index) == 0);
    netmidi2_writer_init(&reply, out, sizeof(out));
    (void)netmidi2_table_tick(&table, 0, now, &reply);
    CHECK(!netmidi2_table_session(&table, 0));
    netmidi2_writer_init(&packet, in, sizeof(in));
    netmidi2_add(&packet, NETMIDI2_PING, 0, 0, (const uint8_t *)"abcd", 4);
    netmidi2_writer_init(&reply, out, sizeof(out));
    CHECK(netmidi2_table_receive(&table, 7000, in, packet.length, now, &reply,
                                 &index) == 0);
    CHECK(index == -1 && table_reply_is(&reply, NETMIDI2_PING_REPLY, 0) &&
          !netmidi2_table_session(&table, 0));
    /* as a Client too, from the same table: one session to a Host, no two */
    netmidi2_writer_init(&reply, out, sizeof(out));
    index = netmidi2_table_invite(&table, 9000, now, &reply);
    CHECK(index == 0 && table_reply_is(&reply, NETMIDI2_INVITATION, 3));
    CHECK(netmidi2_table_invite(&table, 9000, now, &reply) == -1);
    CHECK(netmidi2_table_session(&table, 0)->state == NETMIDI2_INVITING &&
          !netmidi2_table_session(&table, 0)->host);
}

int main(void)
{
    test_codec();
    test_session();
    test_against_specification();
    test_table();
    test_appendix_fec();
    if (failures) {
        fprintf(stderr, "Network MIDI 2.0: %d failures\n", failures);
        return EXIT_FAILURE;
    }
    puts("Network MIDI 2.0 OK");
    return EXIT_SUCCESS;
}
