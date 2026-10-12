#include "rtpmidi/applemidi.h"
#include "rtpmidi/clock.h"
#include "rtpmidi/receiver.h"
#include "rtpmidi/rtpmidi.h"
#include "rtpmidi/session.h"
#include "rtpmidi/sender.h"
#include "rtpmidi/peers.h"
#include "rtpmidi/ports.h"
#include <midihub/config.h>
#include "rtpmidi/timing.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void apple_exchange(void)
{
    static const uint8_t invitation[] = {
        0xff, 0xff, 'I', 'N', 0, 0, 0, 2,
        0x12, 0x34, 0x56, 0x78, 0x89, 0xab, 0xcd, 0xef,
        'A', 'R', 'O', 'S', 0
    };
    struct applemidi_packet packet;
    uint8_t encoded[64];
    size_t length;
    size_t i;

    assert(applemidi_decode(invitation, sizeof(invitation), &packet) == 0);
    assert(packet.command == APPLEMIDI_IN);
    assert(packet.token == 0x12345678 && packet.ssrc == 0x89abcdef);
    assert(packet.name_length == 4 && !memcmp(packet.name, "AROS", 4));
    assert(applemidi_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(invitation));
    assert(!memcmp(encoded, invitation, length));
    for (i = 0; i < 4; ++i)
        assert(applemidi_decode(invitation, i, &packet) ==
               APPLEMIDI_NOT_APPLEMIDI);
    for (i = 4; i < 16; ++i)
        assert(applemidi_decode(invitation, i, &packet) ==
               APPLEMIDI_TRUNCATED);
    assert(applemidi_decode(invitation, 16, &packet) == 0);
    assert(packet.name_length == 0);
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[7] = 3;
    assert(applemidi_decode(encoded, sizeof(invitation), &packet) ==
           APPLEMIDI_BAD_VERSION);
    /* What a strict reading would refuse and a peer may well send: a name
     * cut short of its terminator, a name with none, bytes after it. */
    for (i = 17; i < sizeof(invitation); ++i) {
        assert(applemidi_decode(invitation, i, &packet) == 0);
        assert(packet.name_length == i - 16 &&
               !memcmp(packet.name, "AROS", i - 16));
    }
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[sizeof(invitation) - 1] = 'X';
    assert(applemidi_decode(encoded, sizeof(invitation), &packet) == 0);
    assert(packet.name_length == 5 && !memcmp(packet.name, "AROSX", 5));
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[18] = 0;
    assert(applemidi_decode(encoded, sizeof(invitation), &packet) == 0);
    assert(packet.name_length == 2 && !memcmp(packet.name, "AR", 2));
    /* Apple's bit-rate limit, and anything else we have no use for. */
    memcpy(encoded, invitation, sizeof(invitation));
    encoded[2] = 'R';
    encoded[3] = 'L';
    assert(applemidi_decode(encoded, sizeof(invitation), &packet) ==
           APPLEMIDI_UNKNOWN_COMMAND);
    encoded[0] = 0x80;
    assert(applemidi_decode(encoded, sizeof(invitation), &packet) ==
           APPLEMIDI_NOT_APPLEMIDI);
}

static void apple_sync(void)
{
    static const uint8_t sync[] = {
        0xff, 0xff, 'C', 'K', 0x89, 0xab, 0xcd, 0xef,
        2, 0, 0, 0,
        0, 0, 0, 1, 0, 0, 0, 2,
        0, 0, 0, 3, 0, 0, 0, 4,
        0, 0, 0, 5, 0, 0, 0, 6
    };
    struct applemidi_packet packet;
    uint8_t encoded[36];
    size_t length;

    assert(applemidi_decode(sync, sizeof(sync), &packet) == 0);
    assert(packet.command == APPLEMIDI_CK && packet.sync_count == 2);
    assert(packet.timestamps[0] == UINT64_C(0x0000000100000002));
    assert(packet.timestamps[2] == UINT64_C(0x0000000500000006));
    assert(applemidi_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(sync) && !memcmp(encoded, sync, length));
    assert(applemidi_decode(sync, sizeof(sync) - 1, &packet) ==
           APPLEMIDI_TRUNCATED);
    encoded[8] = 3;
    assert(applemidi_decode(encoded, sizeof(encoded), &packet) ==
           APPLEMIDI_MALFORMED);
    /* the three bytes after the count are nobody's: any value passes */
    memcpy(encoded, sync, sizeof(sync));
    encoded[9] = 0xff;
    encoded[10] = 0x12;
    encoded[11] = 0x34;
    assert(applemidi_decode(encoded, sizeof(encoded), &packet) == 0);
    assert(packet.sync_count == 2 &&
           packet.timestamps[1] == UINT64_C(0x0000000300000004));
}

/* Two clocks that differ by a known amount, a path that takes a known
 * time each way, and the exchange carried between them by hand. */
/* One exchange between an inviter and the side it invited, whose clock is
 * ahead by `ahead`; the answer takes `back` to return instead of `path`. */
static void clock_exchange(struct applemidi_clock *inviter,
                           struct applemidi_clock *invited, uint64_t *t,
                           uint64_t ahead, uint64_t path, uint64_t back)
{
    struct applemidi_packet ck0, ck1, ck2, none;

    assert(applemidi_clock_poll(inviter, 1, *t, &ck0) == 1);
    assert(applemidi_clock_receive(invited, 2, &ck0, *t + path + ahead,
                                   &ck1) == 1);
    assert(applemidi_clock_receive(inviter, 1, &ck1, *t + path + back,
                                   &ck2) == 1);
    assert(applemidi_clock_receive(invited, 2, &ck2,
                                   *t + path + back + path + ahead,
                                   &none) == 0);
    *t += APPLEMIDI_CLOCK_INTERVAL;
}

/* AM11: one CK held up among eight does not move the offset, at either
 * end; and what was learned on a slow path gives way to a quicker one. */
static void apple_clock_delayed(void)
{
    struct applemidi_clock inviter, invited;
    uint64_t t = 5000000;
    const uint64_t ahead = 123456;
    unsigned int i;

    applemidi_clock_init(&inviter, 1, t);
    applemidi_clock_init(&invited, 0, t + ahead);
    for (i = 0; i < 8; ++i) {
        /* the fifth answer is 30 ms late: alone it would say 15 ms */
        clock_exchange(&inviter, &invited, &t, ahead, 20, i == 4 ? 320 : 20);
        assert(inviter.peer_to_local == -(int64_t)ahead);
        assert(invited.peer_to_local == (int64_t)ahead);
    }
    assert(inviter.completed == 8 && invited.completed == 8);

    /* a first exchange that was held up is believed until a better one */
    applemidi_clock_init(&inviter, 1, t);
    applemidi_clock_init(&invited, 0, t + ahead);
    clock_exchange(&inviter, &invited, &t, ahead, 20, 320);
    assert(inviter.synchronized &&
           inviter.peer_to_local == -(int64_t)ahead + 150);
    clock_exchange(&inviter, &invited, &t, ahead, 20, 20);
    assert(inviter.peer_to_local == -(int64_t)ahead);

    /* and a path that became slower for good is followed once the
     * quicker exchanges are no longer among those kept */
    for (i = 0; i < APPLEMIDI_CLOCK_KEPT; ++i)
        clock_exchange(&inviter, &invited, &t, ahead, 20, 320);
    assert(inviter.peer_to_local == -(int64_t)ahead + 150);
}

/* Which port a peer is given (decision D2 of the AppleMIDI backlog). */
/* What is saved is what is read back, every setting of it. */
static void config_round_trip(void)
{
    static struct netmidi_network_config written, read_back;
    const char *path = "build/config-round-trip.tmp";

    netmidi_config_defaults(&written);
    written.local_port = 5010;
    strcpy(written.session_name, "A Session");
    strcpy(written.peer_ip, "192.0.2.7");
    written.peer_port = 5104;
    strcpy(written.peer_name, "Host Session");
    written.latency_ms = 40;
    strcpy(written.accept, "192.0.2.7,the iPad");
    written.midi2_port = 5673;
    strcpy(written.port_name[3], "the iPad");
    strcpy(written.midi2_port_name[15], "A Workbench");
    assert(netmidi_config_save(path, &written) == 0);
    netmidi_config_defaults(&read_back);
    assert(netmidi_config_load(path, &read_back) == 0);
    assert(memcmp(&written, &read_back, sizeof(written)) == 0);
    remove(path);

    /* nothing but the defaults: a file that reads as the defaults */
    netmidi_config_defaults(&written);
    assert(netmidi_config_save(path, &written) == 0);
    memset(&read_back, 0xff, sizeof(read_back));
    netmidi_config_defaults(&read_back);
    assert(netmidi_config_load(path, &read_back) == 0);
    assert(memcmp(&written, &read_back, sizeof(written)) == 0);
    remove(path);
}

static void apple_ports(void)
{
    struct applemidi_ports ports;
    static const uint8_t ipad[] = "the iPad";
    static const uint8_t other[] = "Someone";

    applemidi_ports_init(&ports, 4);
    assert(applemidi_ports_keep(&ports, 2, "the iPad") == 0);
    assert(applemidi_ports_keep(&ports, 2, "Another") == -1);   /* kept */
    assert(applemidi_ports_keep(&ports, 3, "the iPad") == -1);  /* twice */
    assert(applemidi_ports_keep(&ports, 4, "Nobody") == -1);    /* no port */
    assert(applemidi_ports_keep(&ports, 0, "") == -1);

    /* nobody named takes the first port kept for no one */
    assert(applemidi_ports_take(&ports, other, 7, 1) == 0);
    assert(applemidi_ports_take(&ports, NULL, 0, 2) == 1);
    /* the named one gets its own, whoever came first */
    assert(applemidi_ports_take(&ports, ipad, 8, 3) == 2);
    assert(applemidi_ports_take(&ports, other, 7, 4) == 3);
    /* none left, and a kept port is not given to another */
    assert(applemidi_ports_take(&ports, other, 7, 5) == -1);
    /* asking again says the same */
    assert(applemidi_ports_take(&ports, ipad, 8, 3) == 2);
    assert(applemidi_ports_of(&ports, 3) == 2 &&
           applemidi_ports_owner(&ports, 2) == 3);

    /* it leaves and comes back, into another slot of the table: the
       same port, though lower ones are free by then */
    applemidi_ports_release(&ports, 3);
    applemidi_ports_release(&ports, 1);
    assert(applemidi_ports_of(&ports, 3) == -1);
    assert(applemidi_ports_take(&ports, ipad, 8, 6) == 2);
    /* a second peer of the same name has no port while the first is there */
    assert(applemidi_ports_take(&ports, ipad, 8, 7) == -1);
    /* a name is compared whole */
    assert(applemidi_ports_take(&ports, ipad, 7, 8) == 0);
}

static void apple_clock(void)
{
    struct applemidi_clock inviter;
    struct applemidi_clock invited;
    struct applemidi_packet ck0;
    struct applemidi_packet ck1;
    struct applemidi_packet ck2;
    struct applemidi_packet none;
    const uint64_t ahead = 5000000;     /* the invited side's clock leads */
    const uint64_t path = 30;
    uint64_t t = 1000;                  /* the inviter's time */
    unsigned int round;

    applemidi_clock_init(&inviter, 1, t);
    applemidi_clock_init(&invited, 0, t + ahead);
    assert(!inviter.synchronized && !invited.synchronized);
    /* the invited side never starts */
    assert(applemidi_clock_poll(&invited, 2, t + ahead, &none) == 0);

    for (round = 0; round < 5; ++round) {
        assert(applemidi_clock_poll(&inviter, 1, t, &ck0) == 1);
        assert(ck0.command == APPLEMIDI_CK && ck0.ssrc == 1 &&
               ck0.sync_count == 0 && ck0.timestamps[0] == t);
        /* not again before the interval, however often it is asked */
        assert(applemidi_clock_poll(&inviter, 1, t + 1, &none) == 0);
        assert(applemidi_clock_receive(&invited, 2, &ck0, t + path + ahead,
                                       &ck1) == 1);
        assert(ck1.ssrc == 2 && ck1.sync_count == 1 &&
               ck1.timestamps[0] == t &&
               ck1.timestamps[1] == t + path + ahead);
        assert(applemidi_clock_receive(&inviter, 1, &ck1, t + 2 * path,
                                       &ck2) == 1);
        assert(ck2.ssrc == 1 && ck2.sync_count == 2 &&
               ck2.timestamps[2] == t + 2 * path);
        assert(inviter.synchronized && inviter.completed == round + 1);
        /* the peer's clock leads: its times come down to ours */
        assert(inviter.peer_to_local == -(int64_t)ahead);
        assert(applemidi_clock_receive(&invited, 2, &ck2,
                                       t + 3 * path + ahead, &none) == 0);
        assert(invited.synchronized && invited.completed == round + 1);
        assert(invited.peer_to_local == (int64_t)ahead);
        /* the third packet a second time belongs to no exchange */
        assert(applemidi_clock_receive(&invited, 2, &ck2,
                                       t + 4 * path + ahead, &none) == 0);
        assert(invited.completed == round + 1);

        if (round < APPLEMIDI_CLOCK_FIRST_EXCHANGES - 1) {
            assert(applemidi_clock_poll(&inviter, 1,
                       t + APPLEMIDI_CLOCK_FIRST_INTERVAL - 1, &none) == 0);
            t += APPLEMIDI_CLOCK_FIRST_INTERVAL;
        } else {
            assert(applemidi_clock_poll(&inviter, 1,
                       t + APPLEMIDI_CLOCK_INTERVAL - 1, &none) == 0);
            t += APPLEMIDI_CLOCK_INTERVAL;
        }
    }

    /* An answer to a CK we did not send, or sent before, changes nothing. */
    assert(applemidi_clock_poll(&inviter, 1, t, &ck0) == 1);
    ck1 = ck0;
    ck1.sync_count = 1;
    ck1.timestamps[0] = t - 7;
    ck1.timestamps[1] = 123;
    assert(applemidi_clock_receive(&inviter, 1, &ck1, t + 9, &none) == 0);
    assert(inviter.completed == 5 &&
           inviter.peer_to_local == -(int64_t)ahead);
    /* nor does a third packet whose times are not the ones we answered */
    assert(applemidi_clock_receive(&invited, 2, &ck0, t + ahead, &ck1) == 1);
    ck2 = ck1;
    ck2.sync_count = 2;
    ck2.timestamps[1] += 1;
    ck2.timestamps[2] = t + 60;
    assert(applemidi_clock_receive(&invited, 2, &ck2, t + ahead, &none) == 0);
    assert(invited.completed == 5 && invited.peer_to_local == (int64_t)ahead);
    /* an inviter does not answer a first packet, an invited not a second */
    assert(applemidi_clock_receive(&inviter, 1, &ck0, t, &none) == 0);
    ck1.sync_count = 1;
    assert(applemidi_clock_receive(&invited, 2, &ck1, t, &none) == 0);

    /* Silence: only the side that waits for the other to start gives up,
     * counted from the last exchange the other began. */
    assert(!applemidi_clock_expired(&invited,
               t + ahead + APPLEMIDI_CLOCK_TIMEOUT - 1));
    assert(applemidi_clock_expired(&invited,
               t + ahead + APPLEMIDI_CLOCK_TIMEOUT));
    assert(!applemidi_clock_expired(&inviter, t + 10 * APPLEMIDI_CLOCK_TIMEOUT));
    applemidi_clock_init(&invited, 0, 777);
    assert(!applemidi_clock_expired(&invited, 777 + APPLEMIDI_CLOCK_TIMEOUT - 1));
    assert(applemidi_clock_expired(&invited, 777 + APPLEMIDI_CLOCK_TIMEOUT));
}

static void apple_feedback(void)
{
    static const uint8_t feedback[] = {
        0xff, 0xff, 'R', 'S', 0x89, 0xab, 0xcd, 0xef,
        0x00, 0x01, 0x00, 0x02
    };
    struct applemidi_packet packet;
    uint8_t encoded[12];
    size_t length;

    assert(applemidi_decode(feedback, sizeof(feedback), &packet) == 0);
    assert(packet.command == APPLEMIDI_RS && packet.ssrc == 0x89abcdef);
    assert(packet.feedback_sequence == 0x00010002);
    /* What we send has the RTP sequence number in the first two bytes. */
    assert(applemidi_feedback_field(0x2e95) == 0x2e950000);
    /* What we receive is read both ways, and the reading nearer behind
     * what we last sent is the one: Apple's form, */
    assert(applemidi_feedback_acked(0x2e950000, 0x2e95) == 0x2e95);
    assert(applemidi_feedback_acked(0x2e930000, 0x2e95) == 0x2e93);
    assert(applemidi_feedback_acked(0xfffe0000, 0xffff) == 0xfffe);
    /* the extended form before and after its sequence number wraps, */
    assert(applemidi_feedback_acked(0x00002e95, 0x2e95) == 0x2e95);
    assert(applemidi_feedback_acked(0x00002e93, 0x2e95) == 0x2e93);
    assert(applemidi_feedback_acked(0x00030010, 0x0012) == 0x0010);
    /* and either when the number is zero. */
    assert(applemidi_feedback_acked(0x00000000, 0x0001) == 0x0000);
    assert(applemidi_encode(&packet, encoded, sizeof(encoded), &length) == 0);
    assert(length == sizeof(feedback) && !memcmp(encoded, feedback, length));
    assert(applemidi_decode(feedback, sizeof(feedback) - 1, &packet) ==
           APPLEMIDI_TRUNCATED);
    assert(applemidi_encode(&packet, encoded, sizeof(encoded) - 1,
                           &length) == -1);
}

static void applemidi_peers(void)
{
    struct applemidi_peers peers;
    struct applemidi_packet packet;
    struct applemidi_packet reply;
    int data_port;
    int index;
    enum applemidi_peer_event event;
    int first;
    int second;

    assert(applemidi_peers_init(&peers, 0x10203040,
                         (const uint8_t *)"AROS MIDIHub", 12) == 0);
    assert(peers.accept == APPLEMIDI_ACCEPT_ANYONE);

    first = applemidi_peers_invite(&peers, 0xc0a86401, 5004, 0x11111111,
                            100, &packet);
    second = applemidi_peers_invite(&peers, 0xc0a86402, 5004, 0x22222222,
                             100, &reply);
    assert(first >= 0 && second >= 0 && first != second);
    assert(packet.command == APPLEMIDI_IN && packet.token == 0x11111111);
    assert(applemidi_peers_retry(&peers, first, &reply, &data_port) == 0 &&
           reply.command == APPLEMIDI_IN && !data_port);
    assert(applemidi_peers_invite(&peers, 0xc0a86401, 5004, 3, 100,
                           &packet) == -1);

    memset(&packet, 0, sizeof(packet));
    packet.command = APPLEMIDI_OK;
    packet.token = 0x11111111;
    packet.ssrc = 0xaaaa0001;
    packet.name = (const uint8_t *)"iPhone";
    packet.name_length = 6;
    assert(applemidi_peers_receive(&peers, 0xc0a86401, 5004, 0, &packet, 110,
                            &reply, &data_port, &index, &event) == 1);
    assert(index == first && data_port && event == APPLEMIDI_PEER_NONE);
    assert(reply.command == APPLEMIDI_IN);
    assert(applemidi_peers_retry(&peers, first, &reply, &data_port) == 0 &&
           reply.command == APPLEMIDI_IN && data_port);
    assert(!strcmp(peers.peer[first].name, "iPhone"));
    assert(applemidi_peers_receive(&peers, 0xc0a86401, 5005, 1, &packet, 120,
                            &reply, &data_port, &index, &event) == 0);
    assert(index == first && event == APPLEMIDI_PEER_CONNECTED);
    assert(applemidi_peers_find_data(&peers, 0xc0a86401, 5005) == first);
    assert(applemidi_peers_find_ssrc(&peers, 0xaaaa0001) == first);

    memset(&packet, 0, sizeof(packet));
    packet.command = APPLEMIDI_IN;
    packet.token = 0x33333333;
    packet.ssrc = 0xaaaa0003;
    packet.name = (const uint8_t *)"Mac";
    packet.name_length = 3;
    assert(applemidi_peers_receive(&peers, 0xc0a86403, 6000, 0, &packet, 130,
                            &reply, &data_port, &index, &event) == 1);
    assert(index >= 0 && reply.command == APPLEMIDI_OK && !data_port);
    assert(applemidi_peers_receive(&peers, 0xc0a86403, 6001, 1, &packet, 140,
                            &reply, &data_port, &index, &event) == 1);
    assert(reply.command == APPLEMIDI_OK && data_port &&
           event == APPLEMIDI_PEER_CONNECTED);

    applemidi_peers_seen(&peers, first, 200);
    assert(applemidi_peers_expired(&peers, 249, 50) != first);
    assert(applemidi_peers_expired(&peers, 250, 50) == first);
    assert(applemidi_peers_end(&peers, first, &reply) == 0);
    assert(reply.command == APPLEMIDI_BY &&
           applemidi_peers_find_data(&peers, 0xc0a86401, 5005) == -1);

    peers.accept = 0;
    packet.command = APPLEMIDI_IN;
    packet.token = 0x44444444;
    packet.ssrc = 0xaaaa0004;
    assert(applemidi_peers_receive(&peers, 0xc0a86404, 7000, 0, &packet, 300,
                            &reply, &data_port, &index, &event) == 1);
    assert(index == -1 && reply.command == APPLEMIDI_NO);
}

static void rtp_short(void)
{
    static const uint8_t note[] = {0x90, 60, 100};
    static const uint8_t program[] = {0xc2, 10};
    static const uint8_t clock[] = {0xf8};
    static const uint8_t invalid_data[] = {0x90, 0x80, 100};
    static const uint8_t expected[] = {
        0x80, 0x61, 0x12, 0x34, 0x01, 0x02, 0x03, 0x04,
        0x89, 0xab, 0xcd, 0xef, 3, 0x90, 60, 100
    };
    struct rtpmidi_packet packet;
    uint8_t encoded[32];
    size_t length;
    size_t i;

    assert(rtpmidi_encode_short(0x1234, 0x01020304, 0x89abcdef,
                               note, sizeof(note), encoded,
                               sizeof(encoded), &length) == 0);
    assert(length == sizeof(expected) && !memcmp(encoded, expected, length));
    assert(rtpmidi_decode(expected, sizeof(expected), &packet) == 0);
    assert(packet.sequence == 0x1234 && packet.timestamp == 0x01020304);
    assert(packet.ssrc == 0x89abcdef && packet.midi_length == 3);
    assert(!memcmp(packet.midi, note, sizeof(note)));
    for (i = 0; i < sizeof(expected); ++i)
        assert(rtpmidi_decode(expected, i, &packet) == -1);
    assert(rtpmidi_encode_short(0, 0, 0, note, 0, encoded,
                               sizeof(encoded), &length) == -1);
    assert(rtpmidi_encode_short(0, 0, 0, program, sizeof(program), encoded,
                               sizeof(encoded), &length) == 0 && length == 15);
    assert(rtpmidi_encode_short(0, 0, 0, clock, sizeof(clock), encoded,
                               sizeof(encoded), &length) == 0 && length == 14);
    assert(rtpmidi_encode_short(0, 0, 0, invalid_data,
                               sizeof(invalid_data), encoded,
                               sizeof(encoded), &length) == -1);
    memcpy(encoded, expected, sizeof(expected));
    encoded[0] = 0x81;
    assert(rtpmidi_decode(encoded, sizeof(expected), &packet) == -1);
}

static void rtp_journal_and_lengths(void)
{
    static const uint8_t with_journal[] = {
        0x80, 0xe1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 2,
        0xc0, 3, 0x90, 60, 100, 0, 0
    };
    struct rtpmidi_packet packet;
    uint8_t broken[sizeof(with_journal)];

    assert(rtpmidi_decode(with_journal, sizeof(with_journal), &packet) == 0);
    assert(packet.midi_length == 3 && packet.journal_length == 2);
    assert(packet.journal == with_journal + 17);
    memcpy(broken, with_journal, sizeof(broken));
    broken[12] = 0xc0;
    broken[13] = 20;
    assert(rtpmidi_decode(broken, sizeof(broken), &packet) == -1);
    broken[12] = 0x00;
    assert(rtpmidi_decode(broken, sizeof(broken), &packet) == -1);
}

static void journal_framing(void)
{
    static const uint8_t empty[] = {0x80, 0x12, 0x34};
    static const uint8_t sections[] = {
        0xe1, 0x12, 0x34,
        0x00, 0x02,
        0x80, 0x03, 0x10,
        0xa8, 0x03, 0x20
    };
    uint8_t broken[sizeof(sections)];
    struct rtpmidi_journal journal;

    assert(rtpmidi_journal_decode(empty, sizeof(empty), &journal) == 0);
    assert(journal.checkpoint == 0x1234 &&
           journal.single_packet_safe && !journal.channel_count &&
           !journal.system);
    assert(rtpmidi_journal_decode(sections, sizeof(sections), &journal) == 0);
    assert(journal.system == sections + 3 && journal.system_length == 2);
    assert(journal.channel_count == 2);
    assert(journal.channels[0].number == 0 &&
           journal.channels[0].chapters == 0x10 &&
           journal.channels[0].length == 3);
    assert(journal.channels[1].number == 5 &&
           journal.channels[1].chapters == 0x20);
    assert(rtpmidi_journal_decode(empty, 2, &journal) == -1);
    assert(rtpmidi_journal_decode(sections, sizeof(sections) - 1,
                             &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[4] = 12;
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[6] = 2;
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == -1);
    memcpy(broken, sections, sizeof(broken));
    broken[8] = 0x80; /* Duplicate channel number. */
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == -1);
    assert(rtpmidi_journal_decode(empty, sizeof(empty) - 1, &journal) == -1);
    assert(rtpmidi_journal_covers_gap(10, 13, 11));
    assert(rtpmidi_journal_covers_gap(10, 13, 9));
    assert(!rtpmidi_journal_covers_gap(10, 13, 12));
    assert(!rtpmidi_journal_covers_gap(10, 13, 13));
    assert(rtpmidi_journal_covers_gap(0xfffe, 1, 0xffff));
    assert(!rtpmidi_journal_covers_gap(0xfffe, 1, 0));
    assert(!rtpmidi_journal_covers_gap(10, 11, 10));
}

static void journal_system(void)
{
    static const uint8_t bytes[] = {
        0x40, 0x00, 0x04,
        0x70, 0x0d,
        0x70, 0x85, 0x03, 0x0c,
        0x89,
        0xfb, 0x12, 0x34, 0x01, 0x02, 0x03
    };
    static const uint8_t mtc_bytes[] = {
        0x40, 0x00, 0x04,
        0x08, 0x0b,
        0xf3, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
    };
    static const uint8_t mtc_full_frame[] = {
        0x40, 0x00, 0x04,
        0x08, 0x07, 0x47, 0x61, 0x02, 0x03, 0x04
    };
    uint8_t broken[sizeof(bytes)];
    uint8_t mtc_broken[sizeof(mtc_bytes)];
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_system_state state;

    assert(rtpmidi_journal_decode(bytes, sizeof(bytes), &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == 1);
    assert(state.has_reset && state.reset_single_packet_safe &&
           state.reset_count == 5);
    assert(state.has_tune_request && !state.tune_single_packet_safe &&
           state.tune_count == 3);
    assert(state.has_song_select && state.song == 12);
    assert(state.has_active_sense && state.active_sense_single_packet_safe &&
           state.active_sense_count == 9);
    assert(state.has_sequencer && state.sequencer_single_packet_safe &&
           state.sequencer_running && state.downbeat_played &&
           state.has_clock && state.clock == 0x31234 &&
           state.has_time_tools && state.time_tools == 0x010203);

    memcpy(broken, bytes, sizeof(broken));
    broken[4] = 12;
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == -1);
    memcpy(broken, bytes, sizeof(broken));
    broken[10] |= 0x10;
    broken[4] = 10;
    assert(rtpmidi_journal_decode(broken, 13, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == -1);

    assert(rtpmidi_journal_decode(mtc_bytes, sizeof(mtc_bytes), &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == 1);
    assert(state.has_mtc && state.mtc_single_packet_safe &&
           state.mtc_has_complete && state.mtc_complete_quarter_frame &&
           state.mtc_has_partial && !state.mtc_reverse &&
           state.mtc_point == 3);
    assert(state.mtc_complete[0] == 0 && state.mtc_complete[7] == 7 &&
           state.mtc_partial[0] == 8 && state.mtc_partial[7] == 15);
    memcpy(mtc_broken, mtc_bytes, sizeof(mtc_broken));
    mtc_broken[5] = 0x13; /* Q without COMPLETE is invalid. */
    assert(rtpmidi_journal_decode(mtc_broken, sizeof(mtc_broken), &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == -1);
    assert(rtpmidi_journal_decode(mtc_full_frame, sizeof(mtc_full_frame),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == 1);
    assert(state.has_mtc && state.mtc_has_complete &&
           !state.mtc_complete_quarter_frame && !state.mtc_has_partial &&
           state.mtc_point == 7 && state.mtc_complete[0] == 0x61 &&
           state.mtc_complete[3] == 0x04);
    memcpy(mtc_broken, mtc_full_frame, sizeof(mtc_full_frame));
    mtc_broken[6] = 0xe1;
    assert(rtpmidi_journal_decode(mtc_broken, sizeof(mtc_full_frame),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &state) == -1);
}

static void journal_note_offs(void)
{
    static const uint8_t note_only[] = {
        0x20, 0, 4, 0x00, 0x06, 0x08, 0x00, 0x77, 0x08
    };
    static const uint8_t preceded[] = {
        0x20, 0, 4, 0x00, 0x10, 0xf8,
        0x80, 1, 0,
        0x00, 64, 127,
        0x00, 0x02,
        0x80, 0x00,
        0x80, 0x77, 0x08
    };
    static const uint8_t note_on[] = {
        0x20, 0, 4, 0x00, 0x07, 0x08, 0x01, 0xf1, 0x3c, 0xe4
    };
    static const uint8_t duplicate_notes[] = {
        0x20, 0, 4, 0x00, 0x09, 0x08, 0x02, 0xf1,
        0x3c, 0xe4, 0x3c, 0x64
    };
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_notes notes;

    assert(rtpmidi_journal_decode(note_only, sizeof(note_only), &journal) == 0);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(!notes.offbits_single_packet_safe &&
           notes.offbits[7] == 0x08 && notes.offbits[6] == 0);
    assert(rtpmidi_journal_decode(preceded, sizeof(preceded), &journal) == 0);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.offbits_single_packet_safe && notes.offbits[7] == 0x08);
    journal.channels[0].length--;
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == -1);
    assert(rtpmidi_journal_decode(note_on, sizeof(note_on), &journal) == 0);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.log_count == 1 && notes.logs[0].number == 60 &&
           notes.logs[0].velocity == 100 && notes.logs[0].simultaneous &&
           !notes.logs[0].single_packet_safe);
    assert(notes.offbits[7] == 0);
    assert(rtpmidi_journal_decode(duplicate_notes, sizeof(duplicate_notes),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == -1);
}

static void journal_note_extras(void)
{
    static const uint8_t bytes[] = {
        0x20, 0, 4, 0x00, 0x08, 0x04,
        0x01, 0xbc, 0x02, 0x3c, 0xad
    };
    uint8_t broken[sizeof(bytes)];
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_note_extras extras;

    assert(rtpmidi_journal_decode(bytes, sizeof(bytes), &journal) == 0);
    assert(rtpmidi_journal_decode_note_extras(&journal.channels[0], &extras) == 1);
    assert(extras.count == 2 && extras.logs[0].number == 60 &&
           !extras.logs[0].velocity && extras.logs[0].value == 2 &&
           extras.logs[0].single_packet_safe &&
           extras.logs[1].number == 60 && extras.logs[1].velocity &&
           extras.logs[1].value == 45 &&
           !extras.logs[1].single_packet_safe);
    memcpy(broken, bytes, sizeof(broken));
    broken[10] = 0x02; /* Duplicate count log for note 60. */
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == 0);
    assert(rtpmidi_journal_decode_note_extras(&journal.channels[0], &extras) == -1);
}

static void journal_parameters(void)
{
    static const uint8_t value[] = {
        0x20, 0, 4, 0x00, 0x0d, 0x20,
        0xa0, 0x0a, 0x00, 0x00, 0xee, 0x02, 0x00, 0x00, 0x03, 0x05
    };
    static const uint8_t pending[] = {
        0x20, 0, 4, 0x00, 0x06, 0x20, 0x40, 0x03, 0x81
    };
    uint8_t broken[sizeof(value)];
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_parameters parameters;

    assert(rtpmidi_journal_decode(value, sizeof(value), &journal) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 1);
    assert(parameters.single_packet_safe && parameters.transaction_open &&
           !parameters.has_pending && parameters.count == 1 &&
           parameters.logs[0].number == 0 && !parameters.logs[0].nrpn &&
           parameters.logs[0].value_tool &&
           parameters.logs[0].has_entry_msb &&
           parameters.logs[0].entry_msb == 2 &&
           parameters.logs[0].has_entry_lsb &&
           parameters.logs[0].entry_lsb == 0 &&
           parameters.logs[0].has_adjust &&
           parameters.logs[0].adjust == 3 &&
           parameters.logs[0].count_tool &&
           parameters.logs[0].has_count &&
           parameters.logs[0].count == 5);
    assert(rtpmidi_journal_decode(pending, sizeof(pending), &journal) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 1);
    assert(parameters.has_pending && parameters.pending_nrpn &&
           parameters.pending_msb == 1 && !parameters.transaction_open &&
           parameters.count == 0);
    memcpy(broken, value, sizeof(broken));
    broken[10] = 0xc0; /* Value fields without the value tool. */
    assert(rtpmidi_journal_decode(broken, sizeof(broken), &journal) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == -1);
}

static void journal_channel_state(void)
{
    static const uint8_t state_bytes[] = {
        0x20, 0, 4, 0x00, 0x08, 0x90,
        0x05, 0x82, 0x03, 0x01, 0x20
    };
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_channel_state state;

    assert(rtpmidi_journal_decode(state_bytes, sizeof(state_bytes),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_channel_state(&journal.channels[0],
                                            &state) == 1);
    assert(state.has_program && state.program == 5 && state.has_bank &&
           state.bank_msb == 2 && state.bank_lsb == 3 &&
           !state.program_single_packet_safe);
    assert(state.has_pitch && state.pitch_lsb == 1 &&
           state.pitch_msb == 32 && !state.pitch_single_packet_safe);
    journal.channels[0].length--;
    assert(rtpmidi_journal_decode_channel_state(&journal.channels[0],
                                            &state) == -1);
}

static void journal_controls(void)
{
    static const uint8_t controls_bytes[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x07, 0x64, 0xc0, 0x81
    };
    static const uint8_t multiple_tools[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x40, 0xc1, 0x40, 0x81
    };
    static const uint8_t repeated_tool[] = {
        0x20, 0, 4, 0x00, 0x08, 0x40,
        0x01, 0x07, 0x64, 0x07, 0x65
    };
    static const uint8_t enhanced[] = {
        0x30, 0, 4, 0x04, 0x0a, 0x40,
        0x02, 0x10, 0xc1, 0x10, 0x41, 0x10, 0x42
    };
    static const uint8_t enhanced_without_count[] = {
        0x30, 0, 4, 0x04, 0x08, 0x40,
        0x01, 0x10, 0x41, 0x10, 0x42
    };
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_controls controls;

    assert(rtpmidi_journal_decode(controls_bytes, sizeof(controls_bytes),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.count == 2 && controls.logs[0].number == 7 &&
           controls.logs[0].value == 100 && !controls.logs[0].alternate);
    assert(controls.logs[1].number == 64 && controls.logs[1].alternate &&
           !controls.logs[1].count_tool && controls.logs[1].value == 1 &&
           controls.logs[1].single_packet_safe);
    journal.channels[0].length--;
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                       &controls) == -1);
    assert(rtpmidi_journal_decode(multiple_tools, sizeof(multiple_tools),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.logs[0].count_tool && controls.logs[0].value == 1 &&
           controls.logs[1].alternate && !controls.logs[1].count_tool);
    assert(rtpmidi_journal_decode(repeated_tool, sizeof(repeated_tool),
                             &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                       &controls) == -1);
    assert(rtpmidi_journal_decode(enhanced, sizeof(enhanced), &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                      &controls) == 1);
    assert(controls.enhanced && controls.count == 3 &&
           controls.logs[0].number == 16 && controls.logs[0].count_tool &&
           controls.logs[0].command == 0 &&
           controls.logs[1].command == 0 &&
           controls.logs[1].value == 65 &&
           controls.logs[2].command == 1 &&
           controls.logs[2].value == 66);
    assert(rtpmidi_journal_decode(enhanced_without_count,
                             sizeof(enhanced_without_count), &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                      &controls) == -1);
    {
        uint8_t inconsistent[sizeof(enhanced)];
        memcpy(inconsistent, enhanced, sizeof(inconsistent));
        inconsistent[0] &= (uint8_t)~0x10;
        assert(rtpmidi_journal_decode(inconsistent, sizeof(inconsistent),
                                 &journal) == -1);
    }
}

static void outgoing_journal(void)
{
    const uint8_t on[] = {0x90, 60, 100};
    const uint8_t off[] = {0x80, 60, 0};
    const uint8_t bank[] = {0xb0, 0, 2};
    const uint8_t program[] = {0xc0, 5};
    const uint8_t pitch[] = {0xe0, 1, 32};
    const uint8_t volume[] = {0xb0, 7, 90};
    const uint8_t channel_five_volume[] = {0xb5, 7, 91};
    const uint8_t channel_pressure[] = {0xd0, 40};
    const uint8_t poly_pressure[] = {0xa0, 60, 50};
    const uint8_t reset_controllers[] = {0xb0, 121, 0};
    const uint8_t rpn_msb[] = {0xb0, 101, 0};
    const uint8_t rpn_lsb[] = {0xb0, 100, 0};
    const uint8_t data_msb[] = {0xb0, 6, 2};
    const uint8_t data_lsb[] = {0xb0, 38, 25};
    const uint8_t data_increment[] = {0xb0, 96, 0};
    const uint8_t general_data_msb[] = {0xb0, 6, 55};
    const uint8_t rpn_null_msb[] = {0xb0, 101, 127};
    const uint8_t rpn_null_lsb[] = {0xb0, 100, 127};
    const uint8_t soft_on[] = {0xb0, 67, 127};
    const uint8_t soft_off[] = {0xb0, 67, 0};
    const uint8_t omni_on[] = {0xb0, 125, 0};
    const uint8_t system_reset[] = {0xff};
    const uint8_t tune_request[] = {0xf6};
    const uint8_t song_select[] = {0xf3, 9};
    const uint8_t active_sense[] = {0xfe};
    const uint8_t start[] = {0xfa};
    const uint8_t continue_playback[] = {0xfb};
    const uint8_t clock[] = {0xf8};
    const uint8_t stop[] = {0xfc};
    const uint8_t song_position[] = {0xf2, 1, 0};
    const uint8_t mtc_full_frame[] = {
        0xf0, 0x7f, 0x7f, 0x01, 0x01, 0x61, 0x02, 0x03, 0x04, 0xf7
    };
    const uint8_t short_sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
    const uint8_t second_sysex[] = {0xf0, 0x7d, 0x03, 0xf7};
    const uint8_t mtc_forward[][2] = {
        {0xf1, 0x04}, {0xf1, 0x10}, {0xf1, 0x23}, {0xf1, 0x30},
        {0xf1, 0x42}, {0xf1, 0x50}, {0xf1, 0x61}, {0xf1, 0x76}
    };
    const uint8_t mtc_drop[][2] = {
        {0xf1, 0x0d}, {0xf1, 0x11}, {0xf1, 0x2b}, {0xf1, 0x33},
        {0xf1, 0x40}, {0xf1, 0x50}, {0xf1, 0x60}, {0xf1, 0x74}
    };
    const uint8_t mtc_reverse[][2] = {
        {0xf1, 0x04}, {0xf1, 0x76}, {0xf1, 0x61}, {0xf1, 0x50},
        {0xf1, 0x42}, {0xf1, 0x30}, {0xf1, 0x23}, {0xf1, 0x10}
    };
    struct rtpmidi_sender sender;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_notes notes;
    struct rtpmidi_journal_note_extras note_extras;
    struct rtpmidi_journal_channel_state channel_state;
    struct rtpmidi_journal_controls controls;
    struct rtpmidi_journal_parameters parameters;
    struct rtpmidi_journal_aftertouch aftertouch;
    struct rtpmidi_journal_system_state system_state;
    uint8_t bytes[256];
    size_t length;
    size_t mtc_index;

    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_journal(&sender, 100, 1000,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 3 && bytes[0] == 0x80 && bytes[2] == 100);
    assert(rtpmidi_encode_list(100, 1000, 7, NULL, 0,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 13 && rtpmidi_decode(bytes, length, &(struct rtpmidi_packet){0}) == 0);
    rtpmidi_sender_record(&sender, 100, 1000, on, sizeof(on));
    assert(rtpmidi_sender_journal(&sender, 101, 1050,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(journal.checkpoint == 100 && !journal.single_packet_safe &&
           journal.channel_count == 1);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.log_count == 1 && notes.logs[0].number == 60 &&
           notes.logs[0].velocity == 100 && notes.logs[0].simultaneous &&
           !notes.logs[0].single_packet_safe);
    rtpmidi_sender_record(&sender, 101, 1050, off, sizeof(off));
    assert(rtpmidi_sender_journal(&sender, 102, 1100,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1);
    assert(notes.offbits[7] == 0x08 && notes.log_count == 0 &&
           !notes.offbits_single_packet_safe);
    rtpmidi_sender_ack(&sender, 100);
    assert(sender.count == 1 && sender.events[0].sequence == 101);
    rtpmidi_sender_ack(&sender, 101);
    assert(sender.count == 0);
    assert(rtpmidi_sender_journal(&sender, 102, 1100,
                              bytes, sizeof(bytes), &length) == 0);
    assert(length == 3 && bytes[2] == 102);

    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 102, 1100, on, sizeof(on));
    rtpmidi_sender_record(&sender, 103, 1150, on, sizeof(on));
    assert(rtpmidi_sender_journal(&sender, 104, 1200,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_note_extras(&journal.channels[0],
                                         &note_extras) == 1);
    assert(note_extras.count == 1 && !note_extras.logs[0].velocity &&
           note_extras.logs[0].number == 60 &&
           note_extras.logs[0].value == 2);
    {
        const uint8_t release[] = {0x80, 60, 45};
        rtpmidi_sender_record(&sender, 104, 1200, release, sizeof(release));
    }
    assert(rtpmidi_sender_journal(&sender, 105, 1250,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_note_extras(&journal.channels[0],
                                         &note_extras) == 1);
    assert(note_extras.count == 2 && !note_extras.logs[0].velocity &&
           note_extras.logs[0].value == 1 &&
           note_extras.logs[1].velocity &&
           note_extras.logs[1].value == 45);
    rtpmidi_sender_reset(&sender);

    /* CHAN occupies bits 6..3; bit 2 is the enhanced Chapter C H flag. */
    rtpmidi_sender_record(&sender, 99, 950, channel_five_volume,
                     sizeof(channel_five_volume));
    assert(rtpmidi_sender_journal(&sender, 100, 1000,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(journal.channel_count == 1 &&
           journal.channels[0].number == 5 &&
           !(journal.channels[0].data[0] & 0x04));
    rtpmidi_sender_reset(&sender);

    /* Controllers 6/38/96/97 are ordinary Chapter C controllers when no
     * non-null RPN or NRPN is selected (RFC 6295 A.3.4). */
    rtpmidi_sender_record(&sender, 105, 1250, general_data_msb,
                     sizeof(general_data_msb));
    assert(rtpmidi_sender_journal(&sender, 106, 1300,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0], &controls) == 1);
    assert(controls.count == 1 && controls.logs[0].number == 6 &&
           controls.logs[0].value == 55 && !controls.logs[0].alternate);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 0);
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 106, 1300, rpn_null_msb,
                     sizeof(rpn_null_msb));
    rtpmidi_sender_record(&sender, 107, 1350, rpn_null_lsb,
                     sizeof(rpn_null_lsb));
    rtpmidi_sender_record(&sender, 108, 1400, general_data_msb,
                     sizeof(general_data_msb));
    assert(rtpmidi_sender_journal(&sender, 109, 1450,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0], &controls) == 1);
    assert(controls.count == 1 && controls.logs[0].number == 6 &&
           controls.logs[0].value == 55);
    rtpmidi_sender_reset(&sender);

    rtpmidi_sender_record(&sender, 105, 1250, rpn_msb, sizeof(rpn_msb));
    rtpmidi_sender_record(&sender, 106, 1300, rpn_lsb, sizeof(rpn_lsb));
    rtpmidi_sender_record(&sender, 107, 1350, data_msb, sizeof(data_msb));
    rtpmidi_sender_record(&sender, 108, 1400, data_lsb, sizeof(data_lsb));
    assert(rtpmidi_sender_journal(&sender, 109, 1450,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0], &controls) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 1);
    assert(parameters.transaction_open && parameters.count == 1 &&
           parameters.logs[0].number == 0 && !parameters.logs[0].nrpn &&
           parameters.logs[0].has_entry_msb &&
           parameters.logs[0].entry_msb == 2 &&
           parameters.logs[0].has_entry_lsb &&
           parameters.logs[0].entry_lsb == 25 &&
           parameters.logs[0].has_count &&
           parameters.logs[0].count == 1);
    rtpmidi_sender_ack(&sender, 106);
    assert(rtpmidi_sender_journal(&sender, 109, 1450,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 1);
    assert(parameters.logs[0].count_tool &&
           !parameters.logs[0].has_count);
    rtpmidi_sender_record(&sender, 109, 1450, data_increment,
                     sizeof(data_increment));
    rtpmidi_sender_record(&sender, 110, 1500, data_increment,
                     sizeof(data_increment));
    assert(rtpmidi_sender_journal(&sender, 111, 1550,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_parameters(&journal.channels[0],
                                        &parameters) == 1);
    assert(parameters.logs[0].has_adjust &&
           parameters.logs[0].adjust == 2);
    rtpmidi_sender_reset(&sender);

    rtpmidi_sender_record(&sender, 112, 1600, soft_on, sizeof(soft_on));
    rtpmidi_sender_record(&sender, 113, 1650, soft_off, sizeof(soft_off));
    rtpmidi_sender_record(&sender, 114, 1700, soft_on, sizeof(soft_on));
    assert(rtpmidi_sender_journal(&sender, 115, 1750,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0], &controls) == 1);
    assert(controls.count == 1 && controls.logs[0].number == 67 &&
           controls.logs[0].alternate && !controls.logs[0].count_tool &&
           controls.logs[0].value == 3);
    rtpmidi_sender_reset(&sender);

    rtpmidi_sender_record(&sender, 116, 1800, omni_on, sizeof(omni_on));
    rtpmidi_sender_record(&sender, 117, 1850, omni_on, sizeof(omni_on));
    assert(rtpmidi_sender_journal(&sender, 118, 1900,
                             bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0], &controls) == 1);
    assert(controls.count == 1 && controls.logs[0].number == 125 &&
           controls.logs[0].alternate && controls.logs[0].count_tool &&
           controls.logs[0].value == 2);
    rtpmidi_sender_reset(&sender);

    rtpmidi_sender_record(&sender, 102, 1100, bank, sizeof(bank));
    rtpmidi_sender_record(&sender, 103, 1200, program, sizeof(program));
    rtpmidi_sender_record(&sender, 104, 1300, pitch, sizeof(pitch));
    rtpmidi_sender_record(&sender, 105, 1400, volume, sizeof(volume));
    assert(rtpmidi_sender_journal(&sender, 106, 1500,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(journal.checkpoint == 102 && journal.channel_count == 1);
    assert(rtpmidi_journal_decode_channel_state(&journal.channels[0],
                                            &channel_state) == 1);
    assert(channel_state.has_program && channel_state.program == 5 &&
           channel_state.has_bank && channel_state.bank_msb == 2 &&
           channel_state.has_pitch && channel_state.pitch_lsb == 1);
    assert(rtpmidi_journal_decode_controls(&journal.channels[0],
                                       &controls) == 1);
    assert(controls.count == 2);
    rtpmidi_sender_ack(&sender, 104);
    assert(sender.count == 1 && sender.events[0].sequence == 105);
    rtpmidi_sender_clear_history(&sender);
    assert(sender.count == 0);
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 200, 2000, on, sizeof(on));
    rtpmidi_sender_record(&sender, 201, 2010, channel_pressure,
                      sizeof(channel_pressure));
    rtpmidi_sender_record(&sender, 202, 2020, poly_pressure,
                      sizeof(poly_pressure));
    assert(rtpmidi_sender_journal(&sender, 203, 2030,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(journal.channels[0].chapters == 0x0b);
    assert(rtpmidi_journal_decode_aftertouch(&journal.channels[0],
                                        &aftertouch) == 1);
    assert(aftertouch.has_channel_pressure &&
           aftertouch.channel_pressure == 40 &&
           aftertouch.poly_count == 1 &&
           aftertouch.poly[0].number == 60 &&
           aftertouch.poly[0].pressure == 50);
    rtpmidi_sender_record(&sender, 203, 2030, reset_controllers,
                      sizeof(reset_controllers));
    assert(rtpmidi_sender_journal(&sender, 204, 2040,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_aftertouch(&journal.channels[0],
                                        &aftertouch) == 0);

    assert(rtpmidi_sender_supported(system_reset, sizeof(system_reset)) &&
           rtpmidi_sender_supported(tune_request, sizeof(tune_request)) &&
           rtpmidi_sender_supported(song_select, sizeof(song_select)) &&
           rtpmidi_sender_supported(active_sense, sizeof(active_sense)) &&
           rtpmidi_sender_supported(start, sizeof(start)) &&
           rtpmidi_sender_supported(continue_playback,
                               sizeof(continue_playback)) &&
           rtpmidi_sender_supported(clock, sizeof(clock)) &&
           rtpmidi_sender_supported(stop, sizeof(stop)) &&
           rtpmidi_sender_supported(song_position, sizeof(song_position)));
    assert(rtpmidi_sender_supported(mtc_forward[0], sizeof(mtc_forward[0])));
    rtpmidi_sender_record(&sender, 300, 3000, system_reset,
                     sizeof(system_reset));
    assert(sender.count == 1);
    rtpmidi_sender_record(&sender, 301, 3010, tune_request,
                     sizeof(tune_request));
    rtpmidi_sender_record(&sender, 302, 3020, song_select,
                     sizeof(song_select));
    rtpmidi_sender_record(&sender, 303, 3030, active_sense,
                     sizeof(active_sense));
    assert(rtpmidi_sender_journal(&sender, 304, 3040,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0 &&
           journal.system && journal.channel_count == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.has_reset && system_state.reset_count == 1 &&
           system_state.has_tune_request && system_state.tune_count == 1 &&
           system_state.has_song_select && system_state.song == 9 &&
           system_state.has_active_sense &&
           system_state.active_sense_count == 1);
    rtpmidi_sender_ack(&sender, 303);
    assert(sender.count == 0);
    assert(rtpmidi_sender_journal(&sender, 304, 3040,
                              bytes, sizeof(bytes), &length) == 0 &&
           length == 3);

    rtpmidi_sender_record(&sender, 400, 4000, start, sizeof(start));
    assert(rtpmidi_sender_journal(&sender, 401, 4010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.has_sequencer && system_state.sequencer_running &&
           !system_state.downbeat_played && !system_state.has_clock &&
           !system_state.sequencer_single_packet_safe);

    rtpmidi_sender_record(&sender, 401, 4010, clock, sizeof(clock));
    assert(rtpmidi_sender_journal(&sender, 402, 4020,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.sequencer_running && system_state.downbeat_played &&
           system_state.has_clock && system_state.clock == 0);

    rtpmidi_sender_record(&sender, 402, 4020, clock, sizeof(clock));
    rtpmidi_sender_record(&sender, 403, 4030, stop, sizeof(stop));
    assert(rtpmidi_sender_journal(&sender, 404, 4040,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(!system_state.sequencer_running &&
           system_state.downbeat_played && system_state.has_clock &&
           system_state.clock == 1);

    rtpmidi_sender_record(&sender, 404, 4040, song_position,
                     sizeof(song_position));
    assert(rtpmidi_sender_journal(&sender, 405, 4050,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(!system_state.sequencer_running &&
           !system_state.downbeat_played && system_state.has_clock &&
           system_state.clock == 6);

    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 500, 5000, continue_playback,
                     sizeof(continue_playback));
    assert(rtpmidi_sender_journal(&sender, 501, 5010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.sequencer_running &&
           !system_state.downbeat_played && system_state.has_clock &&
           system_state.clock == 0);

    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_record_sysex(&sender, 600, 6000, mtc_full_frame,
                                  sizeof(mtc_full_frame)) == 1);
    assert(rtpmidi_sender_journal(&sender, 601, 6010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.has_mtc && system_state.mtc_has_complete &&
           !system_state.mtc_complete_quarter_frame &&
           !system_state.mtc_has_partial &&
           !system_state.mtc_single_packet_safe &&
           system_state.mtc_complete[0] == 0x61 &&
           system_state.mtc_complete[3] == 0x04);

    rtpmidi_sender_reset(&sender);
    for (mtc_index = 0; mtc_index < 4; ++mtc_index)
        rtpmidi_sender_record(&sender, (uint16_t)(700 + mtc_index),
                         (uint32_t)(7000 + mtc_index),
                         mtc_forward[mtc_index], sizeof(mtc_forward[0]));
    assert(rtpmidi_sender_journal(&sender, 704, 7010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.has_mtc && !system_state.mtc_has_complete &&
           system_state.mtc_has_partial && !system_state.mtc_reverse &&
           system_state.mtc_point == 3 &&
           system_state.mtc_partial[0] == 4 &&
           system_state.mtc_partial[2] == 3);
    for (; mtc_index < 8; ++mtc_index)
        rtpmidi_sender_record(&sender, (uint16_t)(700 + mtc_index),
                         (uint32_t)(7000 + mtc_index),
                         mtc_forward[mtc_index], sizeof(mtc_forward[0]));
    assert(rtpmidi_sender_journal(&sender, 708, 7020,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.mtc_has_complete &&
           system_state.mtc_complete_quarter_frame &&
           !system_state.mtc_has_partial && !system_state.mtc_reverse &&
           system_state.mtc_complete[0] == 6 &&
           system_state.mtc_complete[2] == 3 &&
           system_state.mtc_complete[4] == 2 &&
           system_state.mtc_complete[6] == 1);

    rtpmidi_sender_reset(&sender);
    for (mtc_index = 0; mtc_index < 8; ++mtc_index)
        rtpmidi_sender_record(&sender, (uint16_t)(800 + mtc_index),
                         (uint32_t)(8000 + mtc_index),
                         mtc_drop[mtc_index], sizeof(mtc_drop[0]));
    assert(rtpmidi_sender_journal(&sender, 808, 8020,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.mtc_complete[0] == 3 &&
           system_state.mtc_complete[2] == 0 &&
           system_state.mtc_complete[4] == 1 &&
           system_state.mtc_complete[7] == 4);

    rtpmidi_sender_reset(&sender);
    for (mtc_index = 0; mtc_index < 4; ++mtc_index)
        rtpmidi_sender_record(&sender, (uint16_t)(900 + mtc_index),
                         (uint32_t)(9000 + mtc_index),
                         mtc_reverse[mtc_index], sizeof(mtc_reverse[0]));
    assert(rtpmidi_sender_journal(&sender, 904, 9010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.mtc_has_partial && system_state.mtc_reverse &&
           system_state.mtc_point == 5);
    for (; mtc_index < 8; ++mtc_index)
        rtpmidi_sender_record(&sender, (uint16_t)(900 + mtc_index),
                         (uint32_t)(9000 + mtc_index),
                         mtc_reverse[mtc_index], sizeof(mtc_reverse[0]));
    assert(rtpmidi_sender_journal(&sender, 908, 9020,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    assert(system_state.mtc_has_complete &&
           system_state.mtc_complete_quarter_frame &&
           !system_state.mtc_has_partial && system_state.mtc_reverse &&
           system_state.mtc_complete[0] == 4 &&
           system_state.mtc_complete[6] == 1);

    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_record_sysex(&sender, 1000, 10000, short_sysex,
                                  sizeof(short_sysex)) == 1);
    assert(rtpmidi_sender_journal(&sender, 1001, 10010,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    {
        struct rtpmidi_journal_sysex sysex;
        assert(rtpmidi_journal_decode_sysex(&system_state, &sysex) == 1);
        assert(sysex.count == 1 && sysex.logs[0].list_tool &&
               sysex.logs[0].status == 3 && sysex.logs[0].has_count &&
               sysex.logs[0].count == 1 && !sysex.logs[0].has_first &&
               sysex.logs[0].data_length == 3 &&
               sysex.logs[0].data[0] == 0x7d &&
               sysex.logs[0].data[2] == 0x82);
        bytes[length - 1] &= 0x7f;
        assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
        assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
        assert(rtpmidi_journal_decode_sysex(&system_state, &sysex) == -1);
    }
    assert(rtpmidi_sender_record_sysex(&sender, 1001, 10010, second_sysex,
                                  sizeof(second_sysex)) == 1);
    assert(sender.sysex_log_count == 2);
    assert(rtpmidi_sender_journal(&sender, 1002, 10020,
                              bytes, sizeof(bytes), &length) == 0);
    assert(rtpmidi_journal_decode(bytes, length, &journal) == 0);
    assert(rtpmidi_journal_decode_system(&journal, &system_state) == 1);
    {
        struct rtpmidi_journal_sysex sysex;
        assert(rtpmidi_journal_decode_sysex(&system_state, &sysex) == 1);
        assert(sysex.count == 2 && sysex.logs[0].count == 1 &&
               sysex.logs[0].data_length == 3 &&
               sysex.logs[1].count == 2 &&
               sysex.logs[1].data_length == 2);
    }
    rtpmidi_sender_ack(&sender, 1000);
    assert(sender.sysex_log_count == 1 &&
           sender.sysex_logs[0].count == 2);
    rtpmidi_sender_ack(&sender, 1001);
    assert(sender.sysex_log_count == 0);
}

static void rtp_command_reader(void)
{
    static const uint8_t midi[] = {
        0x90, 60, 100, 0x00, 61, 101, 0x81, 0x00, 0x80, 60, 64
    };
    struct rtpmidi_packet packet = {0};
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;

    packet.timestamp = 100;
    packet.midi = midi;
    packet.midi_length = sizeof(midi);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 100 && event.length == 3);
    assert(event.bytes[0] == 0x90 && event.bytes[1] == 60 &&
           event.bytes[2] == 100);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 100 && event.bytes[0] == 0x90 &&
           event.bytes[1] == 61 && event.bytes[2] == 101);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 228 && event.bytes[0] == 0x80 &&
           event.bytes[1] == 60 && event.bytes[2] == 64);
    assert(rtpmidi_reader_next(&reader, &event) == 0);

    packet.z = 1;
    packet.midi = (const uint8_t *)"\x01\xc0\x22";
    packet.midi_length = 3;
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(event.timestamp == 101 && event.length == 2 &&
           event.bytes[0] == 0xc0 && event.bytes[1] == 0x22);

    packet.z = 0;
    packet.midi = (const uint8_t *)"\x40\x22";
    packet.midi_length = 2;
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == -1);
    packet.midi = (const uint8_t *)"\xf0";
    packet.midi_length = 1;
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == -1);
}

static void rtp_system_common(void)
{
    static const uint8_t midi[] = {
        0xf1, 0x7f, 0, 0xf2, 1, 2, 0, 0xf3, 4, 0, 0xf6
    };
    static const uint8_t broken_running_status[] = {
        0x90, 60, 100, 0, 0xf1, 1, 0, 61, 101
    };
    struct rtpmidi_packet packet = {0};
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;

    packet.midi = midi;
    packet.midi_length = sizeof(midi);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1 &&
           event.length == 2 && event.bytes[0] == 0xf1);
    assert(rtpmidi_reader_next(&reader, &event) == 1 &&
           event.length == 3 && event.bytes[0] == 0xf2);
    assert(rtpmidi_reader_next(&reader, &event) == 1 &&
           event.length == 2 && event.bytes[0] == 0xf3);
    assert(rtpmidi_reader_next(&reader, &event) == 1 &&
           event.length == 1 && event.bytes[0] == 0xf6);
    assert(rtpmidi_reader_next(&reader, &event) == 0);

    packet.midi = broken_running_status;
    packet.midi_length = sizeof(broken_running_status);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(rtpmidi_reader_next(&reader, &event) == -1);
}

static void rtp_sysex(void)
{
    static const uint8_t complete[] = {0xf0, 0x7d, 1, 2, 0xf7};
static const uint8_t first[] = {0xf0, 0x7d, 1, 0xf0};
    static const uint8_t middle[] = {0xf7, 2, 0xf0};
    static const uint8_t last[] = {0xf7, 2, 0xf7};
    struct rtpmidi_packet packet;
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;
    struct rtpmidi_sysex_assembler assembler;
    const uint8_t *message;
    size_t message_length;
    uint8_t wire[32];
    size_t wire_length;

    rtpmidi_sysex_reset(&assembler);
    assert(rtpmidi_encode_list(1, 100, 7, complete, sizeof(complete),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(rtpmidi_decode(wire, wire_length, &packet) == 0);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1 &&
           event.sysex_length == sizeof(complete));
    assert(rtpmidi_sysex_feed(&assembler, &event, 1, &message,
                          &message_length) == 1);
    assert(message_length == sizeof(complete) &&
           !memcmp(message, complete, message_length) &&
           assembler.timestamp == 100);

    assert(rtpmidi_encode_list(2, 101, 7, first, sizeof(first),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(rtpmidi_decode(wire, wire_length, &packet) == 0);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(rtpmidi_sysex_feed(&assembler, &event, 2, &message,
                          &message_length) == 0);
    assert(rtpmidi_encode_list(3, 102, 7, last, sizeof(last),
                              wire, sizeof(wire), &wire_length) == 0);
    assert(rtpmidi_decode(wire, wire_length, &packet) == 0);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1);
    assert(rtpmidi_sysex_feed(&assembler, &event, 3, &message,
                          &message_length) == 1);
    assert(message_length == sizeof(complete) &&
           !memcmp(message, complete, message_length) &&
           assembler.timestamp == 101);

    rtpmidi_sysex_reset(&assembler);
    event.sysex = first;
    event.sysex_length = sizeof(first);
    event.timestamp = 200;
    assert(rtpmidi_sysex_feed(&assembler, &event, 7, &message,
                          &message_length) == 0);
    event.sysex = last;
    event.sysex_length = sizeof(last);
    assert(rtpmidi_sysex_feed(&assembler, &event, 9, &message,
                          &message_length) == -1);

    rtpmidi_sysex_reset(&assembler);
    event.sysex = first;
    event.sysex_length = sizeof(first);
    assert(rtpmidi_sysex_feed(&assembler, &event, 10, &message,
                          &message_length) == 0);
    event.sysex = middle;
    event.sysex_length = sizeof(middle);
    assert(rtpmidi_sysex_feed(&assembler, &event, 11, &message,
                          &message_length) == 0);
    event.sysex = last;
    event.sysex_length = sizeof(last);
    assert(rtpmidi_sysex_feed(&assembler, &event, 12, &message,
                          &message_length) == 1);
    assert(message_length == 6 && message[0] == 0xf0 &&
           message[5] == 0xf7);
}

static void session_handshake(void)
{
    struct applemidi_session initiator;
    struct applemidi_session responder;
    struct applemidi_packet packet;
    struct applemidi_packet response;
    struct applemidi_packet ignored;
    int data_port;

    assert(applemidi_session_init(&initiator, 0x11111111,
                           (const uint8_t *)"AROS", 4) == 0);
    assert(applemidi_session_init(&responder, 0x22222222,
                           (const uint8_t *)"Peer", 4) == 0);
    assert(applemidi_session_invite(&initiator, 0x33333333, &packet) == 0);
    assert(packet.command == APPLEMIDI_IN);
    assert(applemidi_session_retry(&initiator, &ignored, &data_port) == 0);
    assert(ignored.command == APPLEMIDI_IN && !data_port);
    assert(applemidi_session_receive(&responder, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == APPLEMIDI_SESSION_IDLE);
    assert(applemidi_session_receive(&responder, 0, &packet, &response,
                              &data_port) == 1);
    assert(response.command == APPLEMIDI_OK && !data_port);
    assert(responder.phase == APPLEMIDI_SESSION_WAITING_DATA);
    assert(applemidi_session_receive(&responder, 0, &packet, &ignored,
                              &data_port) == 1);
    assert(ignored.command == APPLEMIDI_OK && !data_port);
    packet = response;
    assert(applemidi_session_receive(&initiator, 0, &packet, &response,
                              &data_port) == 1);
    assert(response.command == APPLEMIDI_IN && data_port);
    assert(initiator.phase == APPLEMIDI_SESSION_INVITING_DATA);
    assert(applemidi_session_retry(&initiator, &ignored, &data_port) == 0);
    assert(ignored.command == APPLEMIDI_IN && data_port);
    packet = response;
    packet.token++;
    assert(applemidi_session_receive(&responder, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == APPLEMIDI_SESSION_WAITING_DATA);
    packet.token--;
    assert(applemidi_session_receive(&responder, 1, &packet, &response,
                              &data_port) == 1);
    assert(response.command == APPLEMIDI_OK && data_port);
    assert(responder.phase == APPLEMIDI_SESSION_CONNECTED);
    packet = response;
    assert(applemidi_session_receive(&initiator, 1, &packet, &ignored,
                              &data_port) == 0);
    assert(initiator.phase == APPLEMIDI_SESSION_CONNECTED);
    assert(applemidi_session_end(&initiator, &packet) == 0);
    assert(packet.command == APPLEMIDI_BY);
    assert(applemidi_session_receive(&responder, 0, &packet, &ignored,
                              &data_port) == 0);
    assert(responder.phase == APPLEMIDI_SESSION_IDLE);
}

static void session_rejection(void)
{
    struct applemidi_session initiator;
    struct applemidi_packet packet;
    struct applemidi_packet reply;
    int data_port;

    assert(applemidi_session_init(&initiator, 1, 0, 0) == 0);
    assert(applemidi_session_invite(&initiator, 2, &packet) == 0);
    reply = packet;
    reply.command = APPLEMIDI_NO;
    reply.token = 3;
    assert(applemidi_session_receive(&initiator, 0, &reply, &packet,
                              &data_port) == 0);
    assert(initiator.phase == APPLEMIDI_SESSION_INVITING_CONTROL);
    reply.token = 2;
    assert(applemidi_session_receive(&initiator, 0, &reply, &packet,
                              &data_port) == 0);
    assert(initiator.phase == APPLEMIDI_SESSION_IDLE);
}

static void clock_and_queue(void)
{
    const uint8_t first[] = {0x90, 60, 100};
    const uint8_t second[] = {0x80, 60, 0};
    const uint8_t sysex[] = {0xf0, 0x7d, 1, 0xf7};
    uint64_t stamps[3] = {1000, 800, 1020};
    uint64_t due;
    int64_t offset;
    struct rtpmidi_event_queue queue = {0};
    struct rtpmidi_queued_event event;

    assert(rtpmidi_clock_offset(stamps, 1, &offset) == 0 && offset == 210);
    assert(rtpmidi_clock_due(800, 1010, offset, &due) == 0 && due == 1010);
    assert(rtpmidi_clock_due(900, 1010, offset, &due) == 0 && due == 1110);
    assert(rtpmidi_clock_offset(stamps, 0, &offset) == 0 && offset == -210);
    assert(rtpmidi_clock_due(1010, 800, offset, &due) == 0 && due == 800);
    assert(rtpmidi_clock_due(1110, 800, offset, &due) == 0 && due == 900);
    stamps[2] = 999;
    assert(rtpmidi_clock_offset(stamps, 1, &offset) == -1);

    assert(rtpmidi_clock_due(0x64, UINT64_C(0x100000000), 0, &due) == 0 &&
           due == UINT64_C(0x100000064));
    assert(rtpmidi_clock_due(0xffffff9c, UINT64_C(0x100000000), 0,
                        &due) == 0 && due == UINT64_C(0xffffff9c));
    assert(rtpmidi_queue_push(&queue, 300, first, sizeof(first)) == 0);
    assert(rtpmidi_queue_push(&queue, 200, second, sizeof(second)) == 0);
    assert(rtpmidi_queue_push(&queue, 300, second, sizeof(second)) == 0);
    assert(rtpmidi_queue_next_due(&queue, &due) == 1 && due == 200);
    assert(rtpmidi_queue_pop_due(&queue, 199, &event) == 0);
    assert(rtpmidi_queue_pop_due(&queue, 200, &event) == 1 &&
           !memcmp(event.bytes, second, sizeof(second)));
    assert(rtpmidi_queue_pop_due(&queue, 300, &event) == 1 &&
           !memcmp(event.bytes, first, sizeof(first)));
    assert(rtpmidi_queue_pop_due(&queue, 300, &event) == 1 &&
           !memcmp(event.bytes, second, sizeof(second)));
    assert(rtpmidi_queue_next_due(&queue, &due) == 0);
    assert(rtpmidi_queue_push(&queue, 300, first, sizeof(first)) == 0);
    assert(rtpmidi_queue_push(&queue, 200, second, sizeof(second)) == 0);
    assert(rtpmidi_queue_push_sysex(&queue, 250, sysex, sizeof(sysex)) == 0);
    assert(queue.sysex_bytes == sizeof(sysex));
    assert(rtpmidi_queue_cancel_note_on(&queue, 0, 60) == 1);
    assert(!rtpmidi_queue_has_note_on(&queue, 0, 60));
    assert(rtpmidi_queue_next_due(&queue, &due) == 1 && due == 200);
    assert(rtpmidi_queue_cancel_note_on(&queue, 0, 60) == 0);
    assert(rtpmidi_queue_pop_due(&queue, 200, &event) == 1 && !event.sysex);
    rtpmidi_queue_event_release(&event);
    assert(rtpmidi_queue_pop_due(&queue, 250, &event) == 1 &&
           event.sysex_length == sizeof(sysex) &&
           !memcmp(event.sysex, sysex, sizeof(sysex)) &&
           queue.sysex_bytes == 0);
    rtpmidi_queue_event_release(&event);
    assert(rtpmidi_queue_push(&queue, 400, first, sizeof(first)) == 0);
    assert(rtpmidi_queue_push_sysex(&queue, 350, sysex, sizeof(sysex)) == 0);
    assert(rtpmidi_queue_has_note_on(&queue, 0, 60));
    rtpmidi_queue_reset(&queue);
    assert(queue.sysex_bytes == 0);
    assert(rtpmidi_queue_pop_due(&queue, 400, &event) == 0);
}

/* A sender, and two receivers of its stream: one is given every packet, the
 * other loses some and has only the journal of the next packet it gets. */
struct lossy_link {
    struct rtpmidi_sender sender;
    struct rtpmidi_receiver whole;
    struct rtpmidi_receiver lossy;
    struct rtpmidi_event_queue whole_queue;
    struct rtpmidi_event_queue lossy_queue;
    uint16_t sequence;
    uint32_t timestamp;
    unsigned int lost;
    int synchronized;
    unsigned int recovered_messages;
    unsigned int recovered_sysex;
    unsigned int reports;
    unsigned int uncovered;
    int recovering;
    uint8_t last[3];
};

static void link_message(void *context, const uint8_t *bytes, size_t length)
{
    struct lossy_link *link = context;
    if (!link->recovering) return;
    ++link->recovered_messages;
    memset(link->last, 0, sizeof(link->last));
    memcpy(link->last, bytes, length);
}

static void link_sysex(void *context, const uint8_t *message, size_t length)
{
    struct lossy_link *link = context;
    assert(length >= 2 && message[0] == 0xf0 && message[length - 1] == 0xf7);
    if (link->recovering) ++link->recovered_sysex;
}

static void link_report(void *context, const char *text)
{
    struct lossy_link *link = context;
    assert(text[0] >= 'a' && text[0] <= 'z');
    assert(text[strlen(text) - 1] == '\n');
    ++link->reports;
}

static void quiet_message(void *context, const uint8_t *bytes, size_t length)
{
    (void)context; (void)bytes; (void)length;
}

static void quiet_sysex(void *context, const uint8_t *message, size_t length)
{
    (void)context; (void)message; (void)length;
}

static const struct rtpmidi_receiver_output link_output = {
    link_message, link_sysex, link_report
};
static const struct rtpmidi_receiver_output quiet_output = {
    quiet_message, quiet_sysex, NULL
};

static void link_init(struct lossy_link *link)
{
    memset(link, 0, sizeof(*link));
    rtpmidi_sender_reset(&link->sender);
    rtpmidi_queue_reset(&link->whole_queue);
    rtpmidi_queue_reset(&link->lossy_queue);
    rtpmidi_receiver_init(&link->whole, &quiet_output, link, &link->whole_queue);
    rtpmidi_receiver_init(&link->lossy, &link_output, link, &link->lossy_queue);
    link->sequence = 500;
    link->timestamp = 100000;
}

/* What the receiver that lost packets does with the next one: the journal
 * first, as ports/aros/midihub/main.c does. */
static void link_repair(struct lossy_link *link)
{
    uint8_t wire[1100];
    size_t length;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_system_state system;
    struct rtpmidi_receiver_clock clock;
    int has_system = 0;

    if (!link->lost) return;
    assert(rtpmidi_sender_journal(&link->sender, link->sequence, link->timestamp,
                             wire, sizeof(wire), &length) == 0);
    assert(rtpmidi_journal_decode(wire, length, &journal) == 0);
    if (!rtpmidi_journal_covers_gap(
            (uint16_t)(link->sequence - link->lost - 1), link->sequence,
            journal.checkpoint)) {
        /* what was lost is older than the journal reaches: nothing is
         * repaired, as in the program */
        ++link->uncovered;
        link->lost = 0;
        return;
    }
    if (journal.system) {
        assert(rtpmidi_journal_decode_system(&journal, &system) > 0);
        has_system = 1;
    }
    clock.synchronized = link->synchronized;
    clock.peer_to_local = 0;
    clock.now = link->timestamp;
    link->recovering = 1;
    rtpmidi_receiver_recover(&link->lossy, &journal, has_system ? &system : NULL,
                        link->lost == 1, link->timestamp,
                        link->synchronized ? &clock : NULL);
    link->recovering = 0;
    link->lost = 0;
}

static void link_send(struct lossy_link *link, const uint8_t *message,
                      size_t length, int lose)
{
    if (!lose) link_repair(link);
    if (message[0] == 0xf0) {
        rtpmidi_receiver_deliver_sysex(&link->whole, message, length);
        if (!lose) rtpmidi_receiver_deliver_sysex(&link->lossy, message, length);
        /* a message too long for chapter X is sent without an entry */
        (void)rtpmidi_sender_record_sysex(&link->sender, link->sequence,
                                          link->timestamp, message, length);
    } else {
        assert(rtpmidi_sender_supported(message, length));
        rtpmidi_receiver_deliver(&link->whole, message, length);
        if (!lose) rtpmidi_receiver_deliver(&link->lossy, message, length);
        rtpmidi_sender_record(&link->sender, link->sequence, link->timestamp,
                         message, length);
    }
    if (lose) ++link->lost;
    ++link->sequence;
    link->timestamp += 10;
}

static void receiver_recovery(void)
{
    static struct lossy_link link;
    static const uint8_t on[] = {0x90, 60, 100};
    static const uint8_t off[] = {0x80, 60, 64};
    static const uint8_t other_on[] = {0x91, 72, 90};
    static const uint8_t volume[] = {0xb0, 7, 90};
    static const uint8_t pan[] = {0xb0, 10, 20};
    static const uint8_t bank[] = {0xb2, 0, 3};
    static const uint8_t program[] = {0xc2, 17};
    static const uint8_t pitch[] = {0xe2, 0x10, 0x50};
    static const uint8_t channel_pressure[] = {0xd0, 55};
    static const uint8_t poly_pressure[] = {0xa0, 60, 33};
    static const uint8_t song[] = {0xf3, 5};
    static const uint8_t start[] = {0xfa};
    static const uint8_t rpn_msb[] = {0xb3, 101, 0};
    static const uint8_t rpn_lsb[] = {0xb3, 100, 0};
    static const uint8_t data_msb[] = {0xb3, 6, 12};
    static const uint8_t sysex[] = {0xf0, 0x7d, 1, 2, 3, 0xf7};

    /* Chapter N: a lost Note Off is sent; nothing else is. */
    link_init(&link);
    link_send(&link, on, sizeof(on), 0);
    link_send(&link, off, sizeof(off), 1);
    assert(link.lossy.active_notes[0][60] == 100);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.recovered_messages == 1 && (link.last[0] & 0xf0) == 0x80 &&
           link.last[1] == 60);
    assert(link.lossy.active_notes[0][60] == 0);
    assert(link.lossy.note_count[0][60] == 0);
    assert(link.reports == 1);

    /* A lost Note On is not played late without a clock, and is with one. */
    link_init(&link);
    link_send(&link, on, sizeof(on), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.recovered_messages == 0);
    assert(link.lossy.active_notes[0][60] == 0);
    link_init(&link);
    link.synchronized = 1;
    link_send(&link, on, sizeof(on), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.recovered_messages == 1 && link.last[0] == 0x90 &&
           link.last[1] == 60 && link.last[2] == 100);
    assert(link.lossy.active_notes[0][60] == 100);

    /* Chapters P, W and C, several packets lost on several channels. */
    link_init(&link);
    link_send(&link, pan, sizeof(pan), 0);
    link_send(&link, bank, sizeof(bank), 1);
    link_send(&link, program, sizeof(program), 1);
    link_send(&link, pitch, sizeof(pitch), 1);
    link_send(&link, volume, sizeof(volume), 1);
    link_send(&link, other_on, sizeof(other_on), 0);
    assert(link.lossy.bank_msb[2] == 3 && link.lossy.bank_msb_known[2]);
    assert(link.lossy.program[2] == 17 && link.lossy.program_known[2]);
    assert(link.lossy.pitch[2] == link.whole.pitch[2]);
    assert(link.lossy.pitch[2] == (0x10 | (0x50 << 7)));
    assert(link.lossy.controllers[0][7] == 90);
    assert(link.lossy.controllers[0][10] == 20);
    assert(link.lossy.active_notes[1][72] == 90);
    /* The same journal again changes nothing: the state already agrees. */
    link.recovered_messages = 0;
    link.lost = 1;
    link_send(&link, pan, sizeof(pan), 0);
    assert(link.recovered_messages == 0);

    /* Chapters T and A. */
    link_init(&link);
    link_send(&link, on, sizeof(on), 0);
    link_send(&link, channel_pressure, sizeof(channel_pressure), 1);
    link_send(&link, poly_pressure, sizeof(poly_pressure), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.lossy.channel_pressure[0] == 55);
    assert(link.lossy.poly_pressure[0][60] == 33);

    /* Chapter M: the selection, the data and the closing null selection. */
    link_init(&link);
    link_send(&link, rpn_msb, sizeof(rpn_msb), 1);
    link_send(&link, rpn_lsb, sizeof(rpn_lsb), 1);
    link_send(&link, data_msb, sizeof(data_msb), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.lossy.controllers[3][6] == 12);
    assert(link.recovered_messages >= 3);

    /* System chapters: Song Select, a sequencer Start, SysEx. */
    link_init(&link);
    link_send(&link, song, sizeof(song), 1);
    link_send(&link, start, sizeof(start), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.lossy.song_select_known && link.lossy.song_select == 5);
    assert(link.lossy.sequencer_running == 1);
    assert(link.lossy.sequencer_clock == link.whole.sequencer_clock);
    link_init(&link);
    link_send(&link, sysex, sizeof(sysex), 1);
    link_send(&link, volume, sizeof(volume), 0);
    assert(link.recovered_sysex == 1);
    assert(link.lossy.sysex_count == link.whole.sysex_count);

    /* A SysEx received is not delivered again after a later loss: not
     * when a long one before it was lost for good, and not when several
     * are still in the journal unacknowledged. */
    {
        static uint8_t long_sysex[700];
        static const uint8_t second[] = {0xf0, 0x7d, 9, 8, 0xf7};
        unsigned int n;

        memset(long_sysex, 0x22, sizeof(long_sysex));
        long_sysex[0] = 0xf0;
        long_sysex[sizeof(long_sysex) - 1] = 0xf7;
        link_init(&link);
        link_send(&link, long_sysex, sizeof(long_sysex), 1);
        link_send(&link, sysex, sizeof(sysex), 0);
        /* the journal does not claim the packet it has no entry for */
        assert(link.recovered_sysex == 0 && link.uncovered == 1);
        link_send(&link, volume, sizeof(volume), 1);
        link_send(&link, pan, sizeof(pan), 0);
        assert(link.recovered_sysex == 0);
        assert(link.lossy.sysex_count == link.sender.sysex_count);

        link_init(&link);
        for (n = 0; n < 3; ++n)
            link_send(&link, n & 1 ? second : sysex,
                      n & 1 ? sizeof(second) : sizeof(sysex), 0);
        link_send(&link, volume, sizeof(volume), 1);
        link_send(&link, pan, sizeof(pan), 0);
        assert(link.recovered_sysex == 0);
        assert(link.lossy.controllers[0][7] == 90);

        /* Six lost in a row: the four the journal still has are
         * delivered, the count is taken up from the last, and a seventh
         * received after that is not repeated. */
        link_init(&link);
        for (n = 0; n < 6; ++n)
            link_send(&link, n & 1 ? second : sysex,
                      n & 1 ? sizeof(second) : sizeof(sysex), 1);
        link_send(&link, volume, sizeof(volume), 0);
        assert(link.recovered_sysex == RTPMIDI_SYSEX_JOURNAL_LOGS);
        assert(link.lossy.sysex_count == 6);
        link_send(&link, sysex, sizeof(sysex), 0);
        link_send(&link, volume, sizeof(volume), 1);
        link_send(&link, pan, sizeof(pan), 0);
        assert(link.recovered_sysex == RTPMIDI_SYSEX_JOURNAL_LOGS);
        assert(link.lossy.sysex_count == 7);
    }

    /* The end of a session releases what still sounds. */
    link_init(&link);
    link_send(&link, on, sizeof(on), 0);
    link_send(&link, on, sizeof(on), 0);
    link_send(&link, other_on, sizeof(other_on), 0);
    link.recovering = 1;
    rtpmidi_receiver_release_notes(&link.lossy);
    link.recovering = 0;
    assert(link.recovered_messages == 3);
    assert(link.lossy.note_count[0][60] == 0 &&
           link.lossy.active_notes[1][72] == 0);
}

/* RFC 3550 section 5.1 and RFC 6295 section 2.1: what a header may hold
 * besides the twelve bytes every packet has. */
static void rtp_header_variants(void)
{
    struct rtpmidi_packet packet;
    uint8_t wire[64];
    size_t length;
    static const uint8_t note[] = {0x90, 60, 100};
    /* the same Note On in every form; the command section is 03 90 3c 64 */
    static const uint8_t plain[] = {
        0x80, 0xe1, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5, 0x03, 0x90, 60, 100
    };
    static const uint8_t marker_clear[] = {
        0x80, 0x61, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5, 0x03, 0x90, 60, 100
    };
    static const uint8_t one_source[] = {
        0x81, 0xe1, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5, 1, 2, 3, 4,
        0x03, 0x90, 60, 100
    };
    static const uint8_t extension[] = {
        0x90, 0xe1, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5,
        0xbe, 0xde, 0, 1, 9, 9, 9, 9, 0x03, 0x90, 60, 100
    };
    static const uint8_t padded[] = {
        0xa0, 0xe1, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5, 0x03, 0x90, 60, 100,
        0, 0, 3
    };
    static const uint8_t all[] = {
        0xb1, 0x61, 0, 7, 0, 0, 0, 9, 0, 0, 0, 5, 1, 2, 3, 4,
        0xbe, 0xde, 0, 0, 0x03, 0x90, 60, 100, 0, 2
    };
    static const uint8_t *const good[] = {
        plain, marker_clear, one_source, extension, padded, all
    };
    static const size_t good_length[] = {
        sizeof(plain), sizeof(marker_clear), sizeof(one_source),
        sizeof(extension), sizeof(padded), sizeof(all)
    };
    /* a journal-only packet as another implementation sends it */
    static const uint8_t guard[] = {
        0x80, 0x61, 0, 8, 0, 0, 0, 9, 0, 0, 0, 5, 0x40, 0x80, 0, 7
    };
    uint8_t bad[32];
    size_t i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        memset(&packet, 0, sizeof(packet));
        assert(rtpmidi_decode(good[i], good_length[i], &packet) == 0);
        assert(packet.sequence == 7 && packet.timestamp == 9 &&
               packet.ssrc == 5);
        assert(packet.midi_length == 3 &&
               memcmp(packet.midi, note, 3) == 0);
        assert(!packet.journal && packet.journal_length == 0);
    }
    assert(rtpmidi_decode(guard, sizeof(guard), &packet) == 0);
    assert(packet.midi_length == 0 && packet.journal_length == 3);

    memcpy(bad, plain, sizeof(plain));
    bad[0] = 0x40;                              /* version 1 */
    assert(rtpmidi_decode(bad, sizeof(plain), &packet) == -1);
    bad[0] = 0xc0;                              /* version 3: AppleMIDI */
    assert(rtpmidi_decode(bad, sizeof(plain), &packet) == -1);
    memcpy(bad, plain, sizeof(plain));
    bad[1] = 0xe0;                              /* another payload type */
    assert(rtpmidi_decode(bad, sizeof(plain), &packet) == -1);
    memcpy(bad, plain, sizeof(plain));
    bad[0] = 0x8f;                              /* sources that are not there */
    assert(rtpmidi_decode(bad, sizeof(plain), &packet) == -1);
    memcpy(bad, extension, sizeof(extension));
    bad[15] = 9;                                /* extension past the end */
    assert(rtpmidi_decode(bad, sizeof(extension), &packet) == -1);
    assert(rtpmidi_decode(extension, 14, &packet) == -1);
    memcpy(bad, padded, sizeof(padded));
    bad[sizeof(padded) - 1] = 0;                /* padding of nothing */
    assert(rtpmidi_decode(bad, sizeof(padded), &packet) == -1);
    bad[sizeof(padded) - 1] = 8;                /* padding into the header */
    assert(rtpmidi_decode(bad, sizeof(padded), &packet) == -1);
    bad[sizeof(padded) - 1] = 7;                /* all payload is padding */
    assert(rtpmidi_decode(bad, sizeof(padded), &packet) == -1);
    assert(rtpmidi_decode(plain, 12, &packet) == -1);

    /* what we send: the marker clear with commands and without, as
       AppleMIDI implementations do */
    assert(rtpmidi_encode_short(7, 9, 5, note, sizeof(note), wire,
                                sizeof(wire), &length) == 0);
    assert(length == sizeof(marker_clear) &&
           memcmp(wire, marker_clear, length) == 0);
    assert(rtpmidi_encode_list(8, 9, 5, NULL, 0, wire, sizeof(wire),
                               &length) == 0);
    assert(length == 13 && wire[0] == 0x80 && wire[1] == 0x61 &&
           wire[12] == 0);
}

/* A command is never held back for its journal, and what the journal
 * cannot hold does not cost the rest its protection. */
/* Several messages in one packet, each at its time (backlog R11). */
static void sender_packet_list(void)
{
    static struct rtpmidi_sender sender;
    struct rtpmidi_timed_message chord[3] = {
        {1000, 3, {0x90, 60, 100}},
        {1000, 3, {0x90, 64, 100}},
        {1250, 3, {0x90, 67, 100}}
    };
    struct rtpmidi_timed_message many[400];
    struct rtpmidi_packet packet;
    struct rtpmidi_reader reader;
    struct rtpmidi_event event;
    struct rtpmidi_journal journal;
    struct rtpmidi_journal_notes notes;
    static const uint8_t off[] = {0x80, 60, 0};
    uint8_t wire[1100];
    size_t length;
    size_t taken;
    size_t total;
    unsigned int i;
    uint16_t sequence = 10;

    /* A chord leaves as one packet; the receiver reads the sender's
     * times: the third note 25 ms after the two that came together. */
    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_packet_list(&sender, sequence, 1, chord, 3, wire,
                                      sizeof(wire), &length, &taken) == 0);
    assert(taken == 3);
    assert(rtpmidi_decode(wire, length, &packet) == 0);
    assert(packet.timestamp == 1000 && !packet.z);
    /* 3 + (1 + 3) + (2 + 3): the delta of 250 takes two bytes */
    assert(packet.midi_length == 12 && packet.midi[3] == 0 &&
           packet.midi[7] == 0x81 && packet.midi[8] == 0x7a);
    rtpmidi_reader_init(&reader, &packet);
    for (i = 0; i < 3; ++i) {
        assert(rtpmidi_reader_next(&reader, &event) == 1);
        assert(event.length == 3 &&
               memcmp(event.bytes, chord[i].bytes, 3) == 0 &&
               event.timestamp == chord[i].timestamp);
    }
    assert(rtpmidi_reader_next(&reader, &event) == 0);
    /* All three are in the history under the packet's number: were it
     * lost, the next packet's journal has the three notes. */
    assert(sender.count == 3);
    ++sequence;
    assert(rtpmidi_sender_packet(&sender, sequence, 2000, 1, off, sizeof(off),
                                 wire, sizeof(wire), &length) == 0);
    assert(rtpmidi_decode(wire, length, &packet) == 0 && packet.journal);
    assert(rtpmidi_journal_decode(packet.journal, packet.journal_length,
                                  &journal) == 0);
    assert(journal.channel_count == 1 &&
           rtpmidi_journal_decode_notes(&journal.channels[0], &notes) == 1 &&
           notes.log_count == 3);

    /* A time that goes backwards is sent as the time before it. */
    chord[1].timestamp = 900;
    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_packet_list(&sender, sequence, 1, chord, 3, wire,
                                      sizeof(wire), &length, &taken) == 0);
    assert(rtpmidi_decode(wire, length, &packet) == 0);
    rtpmidi_reader_init(&reader, &packet);
    assert(rtpmidi_reader_next(&reader, &event) == 1 && event.timestamp == 1000);
    assert(rtpmidi_reader_next(&reader, &event) == 1 && event.timestamp == 1000);
    assert(rtpmidi_reader_next(&reader, &event) == 1 && event.timestamp == 1250);

    /* More than a packet holds: as many as fit are taken, in order, and
     * the rest is the caller's to send next. Nothing is lost or doubled. */
    for (i = 0; i < 400; ++i) {
        many[i].timestamp = 5000 + i * 300;
        many[i].length = 3;
        many[i].bytes[0] = 0xb0;
        many[i].bytes[1] = (uint8_t)(i & 127);
        many[i].bytes[2] = (uint8_t)((i >> 2) & 127);
    }
    rtpmidi_sender_reset(&sender);
    total = 0;
    while (total < 400) {
        assert(rtpmidi_sender_packet_list(&sender, sequence++, 1,
                                          many + total, 400 - total, wire,
                                          sizeof(wire), &length,
                                          &taken) == 0);
        assert(taken > 0 && length <= sizeof(wire));
        assert(rtpmidi_decode(wire, length, &packet) == 0);
        assert(packet.midi_length <= RTPMIDI_LIST_MAX);
        rtpmidi_reader_init(&reader, &packet);
        for (i = 0; i < taken; ++i) {
            assert(rtpmidi_reader_next(&reader, &event) == 1);
            assert(memcmp(event.bytes, many[total + i].bytes, 3) == 0 &&
                   event.timestamp == many[total + i].timestamp);
        }
        assert(rtpmidi_reader_next(&reader, &event) == 0);
        total += taken;
    }
    assert(total == 400);

    /* An invalid message ends the packet before it; an invalid first
     * one is refused. */
    chord[1].timestamp = 1000;
    chord[1].bytes[1] = 0x80;
    rtpmidi_sender_reset(&sender);
    assert(rtpmidi_sender_packet_list(&sender, sequence, 1, chord, 3, wire,
                                      sizeof(wire), &length, &taken) == 0 &&
           taken == 1);
    assert(rtpmidi_sender_packet_list(&sender, sequence, 1, chord + 1, 2,
                                      wire, sizeof(wire), &length,
                                      &taken) == -1);
}

static void sender_journal_limits(void)
{
    static struct rtpmidi_sender sender;
    struct rtpmidi_packet packet;
    struct rtpmidi_journal journal;
    uint8_t wire[1100];
    uint8_t sysex[700];
    uint8_t message[3];
    size_t length;
    size_t journal_length;
    unsigned int i;
    uint16_t sequence = 100;
    static const uint8_t on[] = {0x90, 60, 100};
    static const uint8_t volume[] = {0xb1, 7, 90};

    /* No feedback ever: the history fills and slides, every command goes. */
    rtpmidi_sender_reset(&sender);
    for (i = 0; i < 400; ++i) {
        message[0] = (uint8_t)((i & 1 ? 0x90 : 0xb0) | (i & 15));
        message[1] = (uint8_t)(i & 127);
        message[2] = (uint8_t)((i * 7) & 127);
        assert(rtpmidi_sender_packet(&sender, sequence, i, 1, message, 3,
                                     wire, sizeof(wire), &length) == 0);
        assert(rtpmidi_decode(wire, length, &packet) == 0);
        assert(packet.sequence == sequence && packet.midi_length == 3 &&
               memcmp(packet.midi, message, 3) == 0);
        assert(packet.journal && rtpmidi_journal_decode(
                   packet.journal, packet.journal_length, &journal) == 0);
        ++sequence;
    }
    assert(sender.journals_omitted == 0 && sender.unjournalled == 0);
    assert(sender.count == RTPMIDI_SENDER_HISTORY);

    /* Room for the command and not for the journal: the command goes
     * alone, is still recorded, and the next packet's journal has it. */
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, sequence++, 1, on, sizeof(on));
    assert(rtpmidi_sender_journal(&sender, sequence, 2, wire, sizeof(wire),
                                  &journal_length) == 0);
    assert(rtpmidi_sender_packet(&sender, sequence, 2, 1, volume,
                                 sizeof(volume), wire,
                                 16 + journal_length - 1, &length) == 0);
    assert(length == 16 && !(wire[12] & 0x40));
    assert(rtpmidi_decode(wire, length, &packet) == 0 && !packet.journal);
    assert(sender.journals_omitted == 1 && sender.count == 2);
    ++sequence;
    assert(rtpmidi_sender_packet(&sender, sequence, 3, 1, volume,
                                 sizeof(volume), wire, sizeof(wire),
                                 &length) == 0);
    assert(rtpmidi_decode(wire, length, &packet) == 0 && packet.journal);
    assert(rtpmidi_journal_decode(packet.journal, packet.journal_length,
                                  &journal) == 0);
    assert(journal.channel_count == 2);
    /* no room even for the command */
    assert(rtpmidi_sender_packet(&sender, sequence, 3, 1, volume,
                                 sizeof(volume), wire, 15, &length) == -1);

    /* A SysEx too long for chapter X leaves the note's protection alone. */
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 10, 1, on, sizeof(on));
    memset(sysex, 0x11, sizeof(sysex));
    sysex[0] = 0xf0;
    sysex[sizeof(sysex) - 1] = 0xf7;
    assert(rtpmidi_sender_record_sysex(&sender, 11, 2, sysex,
                                       sizeof(sysex)) == 0);
    assert(sender.count == 1 && sender.unjournalled == 1 &&
           sender.sysex_count == 0 && sender.sysex_log_count == 0);
    assert(rtpmidi_sender_journal(&sender, 12, 3, wire, sizeof(wire),
                                  &journal_length) == 0);
    assert(rtpmidi_journal_decode(wire, journal_length, &journal) == 0);
    assert(journal.checkpoint == 10 && journal.channel_count == 1 &&
           journal.channels[0].number == 0 &&
           (journal.channels[0].chapters & 0x08));

    /* More small ones than chapter X has logs: the oldest log goes, the
     * note stays, and the count keeps step. */
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 10, 1, on, sizeof(on));
    sysex[5] = 0xf7;
    for (i = 0; i < RTPMIDI_SYSEX_JOURNAL_LOGS + 2; ++i) {
        sysex[1] = (uint8_t)i;
        assert(rtpmidi_sender_record_sysex(&sender, (uint16_t)(11 + i), 2,
                                           sysex, 6) == 1);
    }
    assert(sender.sysex_log_count == RTPMIDI_SYSEX_JOURNAL_LOGS);
    assert(sender.sysex_logs[0].data[0] == 2);
    assert(sender.sysex_count == RTPMIDI_SYSEX_JOURNAL_LOGS + 2 &&
           sender.unjournalled == 2);
    assert(sender.events[0].bytes[0] == 0x90);
    assert(rtpmidi_sender_journal(&sender, 30, 3, wire, sizeof(wire),
                                  &journal_length) == 0);
    assert(rtpmidi_journal_decode(wire, journal_length, &journal) == 0);
    assert(journal.checkpoint == 10 && journal.system &&
           journal.channel_count == 1);

    /* Two that together pass the limit of bytes: the first gives way. */
    rtpmidi_sender_reset(&sender);
    rtpmidi_sender_record(&sender, 10, 1, on, sizeof(on));
    sysex[1] = sysex[5] = 0x11;
    sysex[301] = 0xf7;
    assert(rtpmidi_sender_record_sysex(&sender, 11, 2, sysex, 302) == 1);
    assert(rtpmidi_sender_record_sysex(&sender, 12, 2, sysex, 302) == 1);
    assert(sender.sysex_log_count == 1 && sender.sysex_logs[0].sequence == 12);
    assert(sender.events[0].bytes[0] == 0x90 && sender.unjournalled == 1);
}

/* A peer that went away without BY and invites again, and a peer that
 * turns out to have our SSRC. */
static void session_restart_and_collision(void)
{
    struct applemidi_session session;
    struct applemidi_packet in;
    struct applemidi_packet out;
    struct applemidi_peers peers;
    enum applemidi_peer_event event;
    int data_port;
    int index;
    static const uint32_t ours = 0x0a0b0c0d;

    assert(applemidi_session_init(&session, ours,
                                  (const uint8_t *)"AROS", 4) == 0);
    memset(&in, 0, sizeof(in));
    in.command = APPLEMIDI_IN;
    in.token = 0x111;
    in.ssrc = 0x222;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 1);
    assert(applemidi_session_receive(&session, 1, &in, &out, &data_port) == 1);
    assert(session.phase == APPLEMIDI_SESSION_CONNECTED &&
           session.restarts == 0);

    /* the same invitation again is a retransmission, not a new start */
    assert(applemidi_session_receive(&session, 1, &in, &out, &data_port) == 1);
    assert(session.phase == APPLEMIDI_SESSION_CONNECTED &&
           session.restarts == 0);

    /* a new token and SSRC on the control port: the peer began again */
    in.token = 0x333;
    in.ssrc = 0x444;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 1);
    assert(out.command == APPLEMIDI_OK && out.token == 0x333 && !data_port);
    assert(session.phase == APPLEMIDI_SESSION_WAITING_DATA &&
           session.restarts == 1 && session.peer_ssrc == 0x444);
    assert(applemidi_session_receive(&session, 1, &in, &out, &data_port) == 1);
    assert(session.phase == APPLEMIDI_SESSION_CONNECTED && data_port);
    /* the old session's BY, arriving late, does not end the new one */
    in.command = APPLEMIDI_BY;
    in.token = 0x111;
    in.ssrc = 0x222;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 0);
    assert(session.phase == APPLEMIDI_SESSION_CONNECTED);
    /* a new token on the data port alone starts nothing */
    in.command = APPLEMIDI_IN;
    in.token = 0x555;
    in.ssrc = 0x666;
    assert(applemidi_session_receive(&session, 1, &in, &out, &data_port) == 0);
    assert(session.phase == APPLEMIDI_SESSION_CONNECTED &&
           session.restarts == 1 && session.peer_ssrc == 0x444);
    /* and one that comes while we are the ones inviting is left alone */
    assert(applemidi_session_init(&session, ours,
                                  (const uint8_t *)"AROS", 4) == 0);
    assert(applemidi_session_invite(&session, 0x777, &out) == 0);
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 0);
    assert(session.phase == APPLEMIDI_SESSION_INVITING_CONTROL &&
           session.restarts == 0);

    /* An invitation from a peer with our SSRC is refused. */
    assert(applemidi_session_init(&session, ours,
                                  (const uint8_t *)"AROS", 4) == 0);
    in.command = APPLEMIDI_IN;
    in.token = 0x888;
    in.ssrc = ours;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 1);
    assert(out.command == APPLEMIDI_NO && out.token == 0x888 && !data_port);
    assert(session.phase == APPLEMIDI_SESSION_IDLE &&
           session.ssrc_collisions == 1);
    /* An acceptance from one is answered with BY, and we may take another
     * SSRC once the session is idle, not before. */
    assert(applemidi_session_invite(&session, 0x999, &out) == 0);
    assert(applemidi_session_renew(&session, 1) == -1);
    in.command = APPLEMIDI_OK;
    in.token = 0x999;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 1);
    assert(out.command == APPLEMIDI_BY && out.token == 0x999);
    assert(session.phase == APPLEMIDI_SESSION_IDLE &&
           session.ssrc_collisions == 2);
    assert(applemidi_session_renew(&session, 0x0e0f1011) == 0);
    assert(applemidi_session_invite(&session, 0xaaa, &out) == 0 &&
           out.ssrc == 0x0e0f1011);
    /* an acceptance with our SSRC for a token we never sent is noise */
    assert(applemidi_session_init(&session, ours,
                                  (const uint8_t *)"AROS", 4) == 0);
    assert(applemidi_session_invite(&session, 0xbbb, &out) == 0);
    in.token = 0xccc;
    assert(applemidi_session_receive(&session, 0, &in, &out, &data_port) == 0);
    assert(session.phase == APPLEMIDI_SESSION_INVITING_CONTROL &&
           session.ssrc_collisions == 0);

    /* The peer table says so. */
    assert(applemidi_peers_init(&peers, ours, (const uint8_t *)"AROS", 4) == 0);
    in.command = APPLEMIDI_IN;
    in.token = 0x111;
    in.ssrc = 0x222;
    assert(applemidi_peers_receive(&peers, 0xc0a80001, 5004, 0, &in, 10,
                                   &out, &data_port, &index, &event) == 1);
    assert(applemidi_peers_receive(&peers, 0xc0a80001, 5005, 1, &in, 11,
                                   &out, &data_port, &index, &event) == 1);
    assert(event == APPLEMIDI_PEER_CONNECTED && index == 0);
    in.token = 0x333;
    in.ssrc = 0x444;
    assert(applemidi_peers_receive(&peers, 0xc0a80001, 5004, 0, &in, 12,
                                   &out, &data_port, &index, &event) == 1);
    assert(event == APPLEMIDI_PEER_RESTARTED && index == 0 &&
           out.command == APPLEMIDI_OK);
    assert(applemidi_peers_find_data(&peers, 0xc0a80001, 5005) == -1);
    assert(applemidi_peers_receive(&peers, 0xc0a80001, 5005, 1, &in, 13,
                                   &out, &data_port, &index, &event) == 1);
    assert(event == APPLEMIDI_PEER_CONNECTED && index == 0);
    assert(applemidi_peers_find_data(&peers, 0xc0a80001, 5005) == 0);
    /* a second peer with our SSRC takes no place in the table */
    in.token = 0x555;
    in.ssrc = ours;
    assert(applemidi_peers_receive(&peers, 0xc0a80002, 5004, 0, &in, 14,
                                   &out, &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_NO && index == -1 &&
           event == APPLEMIDI_PEER_NONE && peers.ssrc_collisions == 1);
    assert(!peers.peer[1].used && peers.peer[0].used);
}

/* Who may connect: anyone, nobody, or those of a list. */
static void applemidi_accept_rule(void)
{
    struct applemidi_peers peers;
    struct applemidi_packet in;
    struct applemidi_packet out;
    enum applemidi_peer_event event;
    int data_port;
    int index;
    static const uint8_t ipad[] = "Jaime's iPad";
    static const uint8_t other[] = "Somebody";

    memset(&in, 0, sizeof(in));
    in.command = APPLEMIDI_IN;
    in.token = 1;
    in.ssrc = 2;
    in.name = other;
    in.name_length = sizeof(other) - 1;

    /* nobody: every invitation is answered NO and takes no place */
    assert(applemidi_peers_init(&peers, 9, (const uint8_t *)"AROS", 4) == 0);
    peers.accept = APPLEMIDI_ACCEPT_NOBODY;
    assert(applemidi_peers_receive(&peers, 0xc0a80005, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_NO && out.token == 1 && index == -1 &&
           peers.refused == 1 && !peers.peer[0].used);
    /* which does not stop us inviting */
    assert(applemidi_peers_invite(&peers, 0xc0a80005, 5004, 7, 2, &out) == 0);

    /* a list: by address, */
    assert(applemidi_peers_init(&peers, 9, (const uint8_t *)"AROS", 4) == 0);
    peers.accept = APPLEMIDI_ACCEPT_LISTED;
    assert(applemidi_peers_allow(&peers, 0xc0a80007, NULL) == 0);
    assert(applemidi_peers_allow(&peers, 0, (const char *)ipad) == 0);
    assert(applemidi_peers_allow(&peers, 0, NULL) == -1);
    assert(applemidi_peers_allow(&peers, 0, "") == -1);
    assert(applemidi_peers_receive(&peers, 0xc0a80005, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_NO && index == -1 && peers.refused == 1);
    assert(applemidi_peers_receive(&peers, 0xc0a80007, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_OK && index == 0);
    /* by name, from wherever, */
    in.name = ipad;
    in.name_length = sizeof(ipad) - 1;
    in.token = 3;
    in.ssrc = 4;
    assert(applemidi_peers_receive(&peers, 0xc0a80063, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_OK && index == 1);
    /* and not by a name that only begins the same, or by none */
    in.name_length = sizeof(ipad) - 2;
    assert(applemidi_peers_receive(&peers, 0xc0a80064, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_NO && index == -1);
    in.name = NULL;
    in.name_length = 0;
    assert(applemidi_peers_receive(&peers, 0xc0a80065, 5004, 0, &in, 1, &out,
                                   &data_port, &index, &event) == 1);
    assert(out.command == APPLEMIDI_NO && peers.refused == 3);
    /* a session already let in is not asked again */
    in.command = APPLEMIDI_BY;
    in.token = 3;
    in.ssrc = 4;
    assert(applemidi_peers_receive(&peers, 0xc0a80063, 5004, 0, &in, 2, &out,
                                   &data_port, &index, &event) == 0);
    assert(!peers.peer[1].used && peers.peer[0].used);
    /* the list has an end */
    {
        int i;
        for (i = 2; i < APPLEMIDI_ALLOWED_MAX; ++i)
            assert(applemidi_peers_allow(&peers, 0x0a000000 + (uint32_t)i,
                                         NULL) == 0);
        assert(applemidi_peers_allow(&peers, 0x0a0000ff, NULL) == -1);
    }
}

int main(void)
{
    apple_exchange();
    apple_sync();
    apple_clock();
    apple_ports();
    config_round_trip();
    apple_clock_delayed();
    apple_feedback();
    applemidi_peers();
    applemidi_accept_rule();
    rtp_short();
    rtp_journal_and_lengths();
    rtp_header_variants();
    journal_framing();
    journal_system();
    journal_note_offs();
    journal_note_extras();
    journal_parameters();
    journal_channel_state();
    journal_controls();
    outgoing_journal();
    sender_journal_limits();
    sender_packet_list();
    rtp_command_reader();
    rtp_system_common();
    rtp_sysex();
    session_handshake();
    session_rejection();
    session_restart_and_collision();
    clock_and_queue();
    receiver_recovery();
    puts("protocol tests passed");
    return 0;
}
