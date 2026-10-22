int main(void)
{
    struct netmidi_discovery d;
    unsigned long checks = 0;
#define CHECK(condition) do { assert(condition); ++checks; } while (0)
    netmidi_discovery_init(&d);
    CHECK(netmidi_discovery_poll(&d, 1) == 0 && !opens);
    CHECK(netmidi_discovery_add(&d, "AROS Test", "_apple-midi._udp", 5004) == 0);
    library_available = 0;
    CHECK(netmidi_discovery_poll(&d, 1) == -1 && !registrations);
    library_available = 1;
    available = 0;
    CHECK(netmidi_discovery_poll(&d, 100001) == 0 && !registrations);
    available = 1;
    CHECK(netmidi_discovery_poll(&d, 200001) == 1 && active && registrations == 1);
    CHECK(netmidi_discovery_poll(&d, 200002) == 1 && !refreshes);
    CHECK(netmidi_discovery_poll(&d, 300001) == 1 && refreshes == 1);
    available = 0;
    CHECK(netmidi_discovery_poll(&d, 400001) == 0 && !d.engine && !d.service[0].reference);
    available = 1;
    ++generation;
    CHECK(netmidi_discovery_poll(&d, 500001) == 1 && registrations == 2);
    /* Fast restart: the process was never observed absent. */
    ++generation;
    CHECK(netmidi_discovery_poll(&d, 600001) == 0 && !d.service[0].reference);
    CHECK(netmidi_discovery_poll(&d, 700001) == 1 && registrations == 3);
    /* A restarted daemon may have reused an ID owned by another task. */
    forced_update = MDNSERR_BADPARAM;
    CHECK(netmidi_discovery_poll(&d, 800001) == 0 && !d.engine);
    forced_update = 0;
    CHECK(netmidi_discovery_poll(&d, 900001) == 1 && registrations == 4);
    /* Transient memory failure does not discard a valid registration. */
    forced_update = MDNSERR_NOMEM;
    CHECK(netmidi_discovery_poll(&d, 1000001) == 1 && d.service[0].reference);
    forced_update = 0;
    /* The configuration changed and asks for the same: the registration
       is kept, nothing is withdrawn or registered again. */
    netmidi_discovery_begin(&d);
    CHECK(netmidi_discovery_add(&d, "AROS Test", "_apple-midi._udp", 5004) == 0);
    CHECK(netmidi_discovery_poll(&d, 1000002) == 1 && registrations == 4 &&
          !withdrawals && d.count == 1);
    /* It asks for nothing: withdrawn at once, with its goodbye. */
    netmidi_discovery_begin(&d);
    CHECK(netmidi_discovery_poll(&d, 1000003) == 0 && withdrawals == 1 &&
          !d.count && !active);
    /* Another name: registered as soon as asked for. */
    netmidi_discovery_begin(&d);
    expected_name = "AROS Other";
    CHECK(netmidi_discovery_add(&d, "AROS Other", "_apple-midi._udp", 5004) == 0);
    CHECK(netmidi_discovery_poll(&d, 1000004) == 1 && registrations == 5);
    /* A refusal is kept with its reason, and tried again later. */
    netmidi_discovery_begin(&d);
    expected_name = "AROS Taken";
    CHECK(netmidi_discovery_add(&d, "AROS Taken", "_apple-midi._udp", 5004) == 0);
    forced_register = MDNSERR_DUPLICATE;
    CHECK(netmidi_discovery_poll(&d, 1000005) == 0 && withdrawals == 2 &&
          d.service[0].refused == MDNSERR_DUPLICATE);
    CHECK(netmidi_discovery_poll(&d, 1100006) == 0 &&
          d.service[0].refused == MDNSERR_DUPLICATE);
    forced_register = 0;
    CHECK(netmidi_discovery_poll(&d, 1200007) == 1 && !d.service[0].refused);
    /* A name longer than one label is not taken. */
    CHECK(netmidi_discovery_add(&d, "0123456789012345678901234567890123456789"
                                    "012345678901234567890123", "_apple-midi._udp",
                                5004) == -1);
    netmidi_discovery_close(&d);
    CHECK(!active && !d.library && !d.engine && !d.count);
    CHECK(withdrawals == 3 && releases == 4);
    netmidi_discovery_close(&d);
    CHECK(withdrawals == 3);
    printf("PASS %lu advertisement lifecycle checks\n", checks);
    return 0;
}
