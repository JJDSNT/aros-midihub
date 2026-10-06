#include "midihub/netmidi2.h"
#include "midihub/ump.h"

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

static struct mh_nm2_session host, client;
static struct inbox host_in, client_in;
static uint32_t now;

/* Delivers a packet built by one side to the other, then any reply back,
   until neither has anything more to say. drop drops the first packet. */
static void exchange(struct mh_nm2_writer *first, int from_client, int drop)
{
    uint8_t buffers[2][MH_NM2_MAX_PACKET];
    struct mh_nm2_writer packet = *first, reply;
    int to_host = from_client, rounds = 0;

    if (drop)
        return;
    while (packet.length > 4 && rounds++ < 8) {
        mh_nm2_writer_init(&reply, buffers[rounds & 1], sizeof(buffers[0]));
        CHECK(mh_nm2_receive(to_host ? &host : &client, packet.data, packet.length,
                             now, &reply) == 0);
        packet = reply;
        to_host = !to_host;
    }
}

static void test_codec(void)
{
    uint8_t buffer[256];
    struct mh_nm2_writer w;
    struct mh_nm2_command_packet commands[4];
    char name[MH_NM2_NAME_MAX + 1], id[MH_NM2_PRODUCT_ID_MAX + 1];
    static const uint8_t spec_example[] = {
        /* Appendix A.1.1: Invitation from "MyDev", Product Instance Id
           "8shYe3h5", no authentication */
        0x4d, 0x49, 0x44, 0x49,
        0x01, 0x04, 0x02, 0x00,
        'M', 'y', 'D', 'e', 'v', 0, 0, 0,
        '8', 's', 'h', 'Y', 'e', '3', 'h', '5'
    };
    int n;

    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_add_identity(&w, MH_NM2_INVITATION, 0, "MyDev", "8shYe3h5") == 0);
    CHECK(w.length == sizeof(spec_example) && !memcmp(buffer, spec_example, w.length));

    n = mh_nm2_parse(spec_example, sizeof(spec_example), commands, 4);
    CHECK(n == 1 && commands[0].code == MH_NM2_INVITATION && commands[0].csd1 == 2);
    CHECK(mh_nm2_identity(&commands[0], name, sizeof(name), id, sizeof(id)) == 0);
    CHECK(!strcmp(name, "MyDev") && !strcmp(id, "8shYe3h5"));

    /* Appendix A.1.2: UMP Data, sequence 16, a Timing Clock in group 0 */
    {
        static const uint8_t expected[] = {
            0x4d, 0x49, 0x44, 0x49, 0xff, 0x01, 0x00, 0x10, 0x10, 0xf8, 0x00, 0x00
        };
        uint32_t clock = 0x10f80000u;
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(mh_nm2_add_ump(&w, 16, &clock, 1) == 0);
        CHECK(w.length == sizeof(expected) && !memcmp(buffer, expected, w.length));
    }

    /* Bad signature and truncated commands */
    CHECK(mh_nm2_parse((const uint8_t *)"MIDX", 4, commands, 4) == -1);
    CHECK(mh_nm2_parse(spec_example, sizeof(spec_example) - 1, commands, 4) == -1);
}

static void test_session(void)
{
    uint8_t buffer[MH_NM2_MAX_PACKET];
    struct mh_nm2_writer w;
    uint32_t note[2] = { 0x20903c64u, 0x20803c40u };
    uint32_t discovery[4] = { 0xf0000101u, 0x0000001fu, 0, 0 };
    unsigned i;

    now = 1000;
    mh_nm2_session_init(&host, 1, "AROS MIDIHub", "aros-pi3-1", take, &host_in);
    mh_nm2_session_init(&client, 0, "Test Client", "client-1", take, &client_in);

    /* Invitation, accepted */
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_invite(&client, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == MH_NM2_ESTABLISHED && client.state == MH_NM2_ESTABLISHED);
    CHECK(!strcmp(host.peer_name, "Test Client") && !strcmp(client.peer_name, "AROS MIDIHub"));

    /* Client to host: UMPs arrive once, in order */
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_send_ump(&client, note, 2, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host_in.count == 2 && host_in.words[0] == note[0] && host_in.words[1] == note[1]);

    /* FEC: lose two packets; the third carries both again */
    host_in.count = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t word = 0x20900000u | (i << 8) | 0x40;
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(mh_nm2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, i < 2);
    }
    CHECK(host_in.count == 3);
    CHECK(host_in.words[0] == 0x20900040u && host_in.words[2] == 0x20900240u);
    CHECK(host.rx_lost == 0);

    /* Repeats of packets already received are skipped */
    host_in.count = 0;
    {
        uint32_t word = 0x20900340u;
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        CHECK(mh_nm2_send_ump(&client, &word, 1, now, &w) == 0);
        exchange(&w, 1, 0);
    }
    CHECK(host_in.count == 1 && host_in.words[0] == 0x20900340u);

    /* Endpoint Discovery is answered by the host: Info, Name, Product
       Instance Id, Stream Configuration */
    client_in.count = 0;
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_send_ump(&client, discovery, 4, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(client_in.count == 0);         /* stream replies are not passed on as MIDI */
    CHECK(client.rx_started && client.rx_expected == 4);

    /* Idle: after data the sender says it has none, repeating for FEC */
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    now += 150;
    CHECK(mh_nm2_tick(&client, now, &w) == 1);
    exchange(&w, 1, 0);
    CHECK(host.state == MH_NM2_ESTABLISHED && host.rx_lost == 0);

    /* Ping when quiet; the peer answers */
    now += 6000;
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_tick(&host, now, &w) == 1);
    exchange(&w, 0, 0);
    CHECK(host.pings_unanswered == 0);

    /* No answers: Bye Timeout after the ping limit */
    for (i = 0; i < 4; ++i) {
        now += 6000;
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        (void)mh_nm2_tick(&host, now, &w);
    }
    CHECK(host.state == MH_NM2_CLOSING);

    /* Bye from the client ends both sides */
    mh_nm2_session_init(&host, 1, "AROS MIDIHub", "aros-pi3-1", take, &host_in);
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_invite(&client, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == MH_NM2_ESTABLISHED);
    mh_nm2_writer_init(&w, buffer, sizeof(buffer));
    CHECK(mh_nm2_bye(&client, MH_NM2_BYE_USER_TERMINATED, now, &w) == 0);
    exchange(&w, 1, 0);
    CHECK(host.state == MH_NM2_IDLE && client.state == MH_NM2_IDLE);

    /* UMP Data outside a session: Bye Session Not Established */
    {
        uint32_t word = 0x20900040u;
        struct mh_nm2_writer reply;
        uint8_t out[64];
        struct mh_nm2_command_packet commands[4];
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        mh_nm2_add_ump(&w, 0, &word, 1);
        mh_nm2_writer_init(&reply, out, sizeof(out));
        CHECK(mh_nm2_receive(&host, buffer, w.length, now, &reply) == 0);
        CHECK(mh_nm2_parse(out, reply.length, commands, 4) == 1);
        CHECK(commands[0].code == MH_NM2_BYE &&
              commands[0].csd1 == MH_NM2_BYE_NOT_ESTABLISHED);
    }

    /* An unknown command is answered with NAK Command Not Supported */
    {
        struct mh_nm2_writer reply;
        uint8_t out[64];
        struct mh_nm2_command_packet commands[4];
        mh_nm2_writer_init(&w, buffer, sizeof(buffer));
        mh_nm2_add(&w, 0x55, 0, 0, NULL, 0);
        mh_nm2_writer_init(&reply, out, sizeof(out));
        CHECK(mh_nm2_receive(&host, buffer, w.length, now, &reply) == 0);
        CHECK(mh_nm2_parse(out, reply.length, commands, 4) == 1);
        CHECK(commands[0].code == MH_NM2_NAK &&
              commands[0].csd1 == MH_NM2_NAK_NOT_SUPPORTED &&
              commands[0].payload[0] == 0x55);
    }
}

int main(void)
{
    test_codec();
    test_session();
    if (failures) {
        fprintf(stderr, "Network MIDI 2.0: %d failures\n", failures);
        return EXIT_FAILURE;
    }
    puts("Network MIDI 2.0 OK");
    return EXIT_SUCCESS;
}
