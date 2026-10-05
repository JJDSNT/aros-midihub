#include <midihub/routes.h>

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "route test failed: %s\n", #x); return 1; } } while (0)

int main(void)
{
    static const char valid[] =
        "# MIDIHub routes\n"
        "route\t1\t1\tBLE Piano\tMIDIHub Synth\n"
        "route\t0\t0\tMIDIHub In\tUSB MIDI Out\r\n";
    struct mh_route_table table;
    size_t line;

    CHECK(mh_routes_parse(&table, valid, sizeof(valid) - 1, &line) == 0);
    CHECK(table.count == 2);
    CHECK(table.routes[0].enabled && table.routes[0].reconnect);
    CHECK(strcmp(table.routes[0].source, "BLE Piano") == 0);
    CHECK(strcmp(table.routes[0].destination, "MIDIHub Synth") == 0);
    CHECK(!table.routes[1].enabled && !table.routes[1].reconnect);
    CHECK(mh_routes_parse(&table, "route\t1\tmaybe\ta\tb\n", 20, &line) != 0);
    CHECK(line == 1 && table.count == 0);
    CHECK(mh_routes_parse(&table, "route\t1\t1\t\tb\n", 13, &line) != 0);
    CHECK(mh_routes_parse(&table, "route\t1\t1\ta\ta\n", 14, &line) != 0);
    {
        static const char cycle[] =
            "route\t1\t1\ta\tb\n"
            "route\t1\t1\tb\ta\n";
        CHECK(mh_routes_parse(&table, cycle, sizeof(cycle) - 1, &line) != 0);
        CHECK(line == 0 && table.count == 0);
    }
    puts("route configuration OK");
    return 0;
}
