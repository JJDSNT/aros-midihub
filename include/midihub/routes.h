#ifndef MIDIHUB_ROUTES_H
#define MIDIHUB_ROUTES_H

#include <stddef.h>

#define MH_ROUTE_MAX 64
#define MH_ROUTE_NAME_MAX 127

struct mh_route_config {
    char source[MH_ROUTE_NAME_MAX + 1];
    char destination[MH_ROUTE_NAME_MAX + 1];
    int enabled;
    int reconnect;
};

struct mh_route_table {
    struct mh_route_config routes[MH_ROUTE_MAX];
    size_t count;
};

/* One route per line:
 * route<TAB>enabled<TAB>reconnect<TAB>source cluster<TAB>destination cluster
 * enabled and reconnect are 0 or 1. Blank lines and #/; comments are ignored. */
int mh_routes_parse(struct mh_route_table *table, const char *text, size_t length,
                    size_t *error_line);

/* The transports were programs with clusters of their own and are drivers
 * now: a route saved with a cluster's old name is given the name of the
 * driver's port that took its place. "MIDIHub In" and "MIDIHub Out" become
 * the first port of the driver applemidi, "BLE MIDI In" and "BLE MIDI Out"
 * that of btmidi, which is the peripheral role as those clusters were.
 * Returns how many names were changed. */
size_t mh_routes_migrate(struct mh_route_table *table);

#endif
