#include <applemidi/applemidi.h>
#include <applemidi/clock.h>
#include <applemidi/peers.h>
#include <applemidi/session.h>
#include <netmidi/text.h>

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

/* AM17: a name too long is cut between characters, not inside one. */
static void apple_names(void)
{
    /* "Bj" + o-diaeresis (2 bytes) + "rk " + a euro sign (3) + " " + a
       four-byte emoji: 2 + 2 + 3 + 3 + 1 + 4 = 15 bytes */
    static const uint8_t name[] = {
        'B', 'j', 0xc3, 0xb6, 'r', 'k', ' ', 0xe2, 0x82, 0xac, ' ',
        0xf0, 0x9f, 0x8e, 0xb9
    };
    static const uint8_t noise[] = {'a', 0x80, 0x80, 0x80, 0x80, 0x80, 'b'};

    assert(netmidi_name_fit(name, sizeof(name), 15) == 15);
    assert(netmidi_name_fit(name, sizeof(name), 63) == 15);
    assert(netmidi_name_fit(name, sizeof(name), 3) == 2);    /* not half an o */
    assert(netmidi_name_fit(name, sizeof(name), 4) == 4);
    assert(netmidi_name_fit(name, sizeof(name), 8) == 7);    /* nor of a euro */
    assert(netmidi_name_fit(name, sizeof(name), 9) == 7);
    assert(netmidi_name_fit(name, sizeof(name), 10) == 10);
    assert(netmidi_name_fit(name, sizeof(name), 12) == 11);
    assert(netmidi_name_fit(name, sizeof(name), 14) == 11);
    assert(netmidi_name_fit(name, sizeof(name), 0) == 0);
    /* bytes that are no UTF-8: cut where the limit falls */
    assert(netmidi_name_fit(noise, sizeof(noise), 5) == 5);

    /* AM18: a command we have no use for, such as Apple's bit-rate limit,
       is told apart from what is not AppleMIDI at all */
    {
        static const uint8_t limit[] = {0xff, 0xff, 'R', 'L', 0, 0, 0, 1,
                                        0, 0, 0x10, 0};
        static const uint8_t rtp[] = {0x80, 0x61, 0, 1, 0, 0, 0, 0,
                                      0, 0, 0, 1, 0};
        struct applemidi_packet packet;

        assert(applemidi_decode(limit, sizeof(limit), &packet) ==
               APPLEMIDI_UNKNOWN_COMMAND);
        assert(applemidi_decode(rtp, sizeof(rtp), &packet) ==
               APPLEMIDI_NOT_APPLEMIDI);
    }
}

/* AM15: a table told to hold two peers refuses the third, and takes one
 * again when one of the two has left. */
static void apple_peers_limit(void)
{
    static struct applemidi_peers peers;
    struct applemidi_packet in, reply;
    int data_port, index;
    enum applemidi_peer_event event;
    uint32_t n;

    assert(applemidi_peers_init(&peers, 0x01020304,
                                (const uint8_t *)"Two", 3) == 0);
    assert(peers.limit == APPLEMIDI_PEERS_MAX);
    peers.limit = 2;
    memset(&in, 0, sizeof(in));
    in.command = APPLEMIDI_IN;
    in.name = (const uint8_t *)"Peer";
    in.name_length = 4;
    for (n = 0; n < 3; ++n) {
        in.token = 100 + n;
        in.ssrc = 0xbb000000u + n;
        assert(applemidi_peers_receive(&peers, 0xc0a80010u + n, 5004, 0, &in,
                                       10, &reply, &data_port, &index,
                                       &event) == 1);
        assert(reply.token == 100 + n);
        if (n < 2)
            assert(reply.command == APPLEMIDI_OK && index == (int)n);
        else
            assert(reply.command == APPLEMIDI_NO && index == -1);
    }
    assert(peers.refused == 1);
    /* the first is ended: there is room for one again */
    (void)applemidi_peers_end(&peers, 0, &reply);
    assert(!peers.peer[0].used);
    in.command = APPLEMIDI_IN;
    in.token = 200;
    in.ssrc = 0xbb000009u;
    assert(applemidi_peers_receive(&peers, 0xc0a80019u, 5004, 0, &in, 30,
                                   &reply, &data_port, &index, &event) == 1 &&
           reply.command == APPLEMIDI_OK);
    assert(peers.refused == 1);
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

static void applemidi_timeout_ordering(void)
{
    struct applemidi_clock clock;
    struct applemidi_peers peers;
    struct applemidi_packet out;
    int peer;
    const uint32_t start = UINT32_MAX - 20;

    applemidi_clock_init(&clock, 0, 1000);
    assert(!applemidi_clock_expired(&clock, 994));
    assert(!applemidi_clock_expired(&clock, 999));
    assert(!applemidi_clock_expired(&clock, 1000));
    assert(!applemidi_clock_expired(&clock, 1000 + APPLEMIDI_CLOCK_TIMEOUT - 1));
    assert(applemidi_clock_expired(&clock, 1000 + APPLEMIDI_CLOCK_TIMEOUT));
    applemidi_clock_init(&clock, 1, 1000);
    assert(applemidi_clock_poll(&clock, 1, 1000, &out) == 1);
    assert(applemidi_clock_poll(&clock, 1, 994, &out) == 0);

    assert(applemidi_peers_init(&peers, 1, NULL, 0) == 0);
    peer = applemidi_peers_invite(&peers, 0xc0000201, 5004, 123, 1000, &out);
    assert(peer >= 0);
    assert(applemidi_peers_expired(&peers, 994, 50) == -1);
    assert(applemidi_peers_expired(&peers, 999, 50) == -1);
    assert(applemidi_peers_expired(&peers, 1000, 50) == -1);
    assert(applemidi_peers_expired(&peers, 1049, 50) == -1);
    assert(applemidi_peers_expired(&peers, 1050, 50) == peer);
    applemidi_peers_seen(&peers, peer, 2000);
    assert(applemidi_peers_expired(&peers, 1994, 50) == -1);
    assert(applemidi_peers_expired(&peers, 2050, 50) == peer);

    /* Low 32 bits of a 10 kHz clock wrap about every five days. */
    applemidi_peers_seen(&peers, peer, start);
    assert(applemidi_peers_expired(&peers, start - 6, 50) == -1);
    assert(applemidi_peers_expired(&peers, start + 49, 50) == -1);
    assert(applemidi_peers_expired(&peers, start + 50, 50) == peer);
    applemidi_peers_seen(&peers, peer, 20);
    assert(applemidi_peers_expired(&peers, UINT32_MAX - 5, 50) == -1);
    assert(applemidi_peers_expired(&peers, 70, 50) == peer);
}

static void clock_offset(void)
{
    uint64_t stamps[3] = {1000, 800, 1020};
    int64_t offset;
    assert(applemidi_clock_offset(stamps, 1, &offset) == 0 && offset == 210);
    assert(applemidi_clock_offset(stamps, 0, &offset) == 0 && offset == -210);
    stamps[2] = 999;
    assert(applemidi_clock_offset(stamps, 1, &offset) == -1);
}

int main(void)
{
    apple_exchange();
    apple_sync();
    apple_clock_delayed();
    apple_names();
    apple_peers_limit();
    apple_clock();
    apple_feedback();
    applemidi_peers();
    session_handshake();
    session_rejection();
    session_restart_and_collision();
    applemidi_accept_rule();
    applemidi_timeout_ordering();
    clock_offset();
    puts("AppleMIDI tests passed");
    return 0;
}
