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

#endif
