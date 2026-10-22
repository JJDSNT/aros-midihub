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
    netmidi_discovery_close(&d);
    CHECK(!active && !d.library && !d.engine && !d.service[0].reference);
    CHECK(withdrawals == 1 && releases == 4);
    netmidi_discovery_close(&d);
    CHECK(withdrawals == 1);
    printf("PASS %lu advertisement lifecycle checks\n", checks);
    return 0;
}
