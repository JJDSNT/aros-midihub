/* The browsing half of ports/aros/midihub/discovery.c against the queued
   discovery of mdnsd (DISCOVERY.md): compiled with the mocks of
   mdns_advertise_mock.h and NETMIDI_BROWSE, by
   tools/test-discovery-fixtures.sh. */
int main(void)
{
    struct netmidi_discovery d;
    struct netmidi_discovered seen;
    unsigned long checks = 0;
    char value[16];
    static const UBYTE address[4] = {192, 168, 1, 20};
    static const UBYTE txt[] = {
        5, 'f', 'l', 'a', 'g', 's',                 /* a key without value */
        19, 'u', 'm', 'p', 'e', 'n', 'd', 'p', 'o', 'i', 'n', 't',
            'n', 'a', 'm', 'e', '=', 'K', 'e', 'y',
        4, 'a', '=', 0, 'b'                         /* a NUL inside */
    };
    static const UBYTE broken[] = {9, 'a', '=', 'b'};
#define CHECK(condition) do { assert(condition); ++checks; } while (0)

    /* The text record as it is on the wire, keys in any case. */
    CHECK(netmidi_discovery_txt_value(txt, sizeof(txt), "UMPEndpointName",
                                      value, sizeof(value)) == 0 &&
          !strcmp(value, "Key"));
    CHECK(netmidi_discovery_txt_value(txt, sizeof(txt), "flags", value,
                                      sizeof(value)) == -1);
    CHECK(netmidi_discovery_txt_value(txt, sizeof(txt), "a", value,
                                      sizeof(value)) == 0 && !*value);
    CHECK(netmidi_discovery_txt_value(txt, sizeof(txt), "UMPEndpointName",
                                      value, 3) == 0 && !strcmp(value, "Ke"));
    CHECK(netmidi_discovery_txt_value(broken, sizeof(broken), "a", value,
                                      sizeof(value)) == -1);
    CHECK(netmidi_discovery_txt_value(txt, 0, "flags", value,
                                      sizeof(value)) == -1);

    netmidi_discovery_init(&d);
    CHECK(netmidi_discovery_browse(&d, "_apple-midi._udp") == 0);
    CHECK(netmidi_discovery_browse(&d, "_midi2._udp") == 1);
    CHECK(netmidi_discovery_browse(&d, "_third._udp") == -1);
    /* Nothing is read before the service answered. */
    CHECK(netmidi_discovery_next(&d, 1, &seen) == 0 && !reads);
    CHECK(netmidi_discovery_poll(&d, 1) == 0 && browses_started == 2 &&
          d.browse[0].reference && d.browse[1].reference);
    /* A complete snapshot, then the end of the queues. */
    queue_event(MEVENT_ADD, "Mac", address, 5004, txt, sizeof(txt));
    queue_event(MEVENT_RESOLVED, "Other", address, 1, txt, 0);
    CHECK(netmidi_discovery_next(&d, 2, &seen) == 1 && seen.present &&
          !strcmp(seen.name, "Mac") && seen.address == 0xc0a80114u &&
          seen.port == 5004 && seen.browse == 0 &&
          !strcmp(seen.endpoint, "Key"));
    /* a code that is not ADD or REMOVE is passed over */
    CHECK(netmidi_discovery_next(&d, 2, &seen) == 0 && !queued_count &&
          !d.reading);
    /* At most once a second. */
    reads = 0;
    queue_event(MEVENT_REMOVE, "Mac", address, 0, txt, 0);
    CHECK(netmidi_discovery_next(&d, 5000, &seen) == 0 && !reads);
    CHECK(netmidi_discovery_next(&d, 10002, &seen) == 1 && !seen.present &&
          !strcmp(seen.name, "Mac"));
    CHECK(netmidi_discovery_next(&d, 10002, &seen) == 0);
    /* An overflowed queue ends its search; it is begun again at once. */
    forced_read = MDNSERR_BUSY;
    CHECK(netmidi_discovery_next(&d, 30000, &seen) == 0 &&
          browses_stopped == 2 && !d.browse[0].reference &&
          !d.browse[1].reference);
    forced_read = 0;
    CHECK(netmidi_discovery_poll(&d, 30001) == 0 && browses_started == 4);
    /* mdnsd ended: everything it held is let go, and asked for again. */
    forced_read = MDNSERR_NOENGINE;
    CHECK(netmidi_discovery_next(&d, 50000, &seen) == 0 && !d.engine &&
          !d.browse[0].reference && !d.browse[1].reference);
    forced_read = 0;
    CHECK(netmidi_discovery_poll(&d, 150001) == 0 && browses_started == 6);
    /* A new run asks for its searches again; the old ones are stopped. */
    netmidi_discovery_begin(&d);
    CHECK(browses_stopped == 4 && !d.browses);
    CHECK(netmidi_discovery_browse(&d, "_apple-midi._udp") == 0);
    CHECK(netmidi_discovery_poll(&d, 150002) == 0 && browses_started == 7);
    netmidi_discovery_close(&d);
    CHECK(browses_stopped == 5 && !d.library && !d.browses);
    printf("PASS %lu browse checks\n", checks);
    return 0;
}
