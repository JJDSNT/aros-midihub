#include <netmidi/config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* What is saved is what is read back, every setting of it. */
static void config_round_trip(void)
{
    static struct netmidi_network_config written, read_back;
    const char *path = "build/config-round-trip.tmp";

    netmidi_config_defaults(&written);
    written.applemidi.local_port = 5010;
    strcpy(written.session_name, "A Session");
    written.session_name_given = 1;
    strcpy(written.applemidi.peer_ip, "192.0.2.7");
    written.applemidi.peer_port = 5104;
    strcpy(written.applemidi.peer_name, "Host Session");
    written.latency_ms = 40;
    written.applemidi.max_peers = 2;
    strcpy(written.applemidi.accept, "192.0.2.7,the iPad");
    written.midi2.port = 5673;
    strcpy(written.midi2.peer_ip, "192.0.2.9");
    written.midi2.peer_port = 5683;
    strcpy(written.midi2.peer_name, "A Host");
    strcpy(written.applemidi.port_name[3], "the iPad");
    strcpy(written.midi2.port_name[15], "A Workbench");
    assert(netmidi_config_save(path, &written) == 0);
    netmidi_config_defaults(&read_back);
    assert(netmidi_config_load(path, &read_back) == 0);
    assert(memcmp(&written, &read_back, sizeof(written)) == 0);
    remove(path);

    /* nothing but the defaults: a file that reads as the defaults */
    netmidi_config_defaults(&written);
    assert(netmidi_config_save(path, &written) == 0);
    written.session_name_given = 1;     /* a file always says the name */
    memset(&read_back, 0xff, sizeof(read_back));
    netmidi_config_defaults(&read_back);
    assert(netmidi_config_load(path, &read_back) == 0);
    assert(memcmp(&written, &read_back, sizeof(written)) == 0);
    remove(path);
}

int main(void)
{
    config_round_trip();
    puts("Network MIDI configuration tests passed");
    return 0;
}
