#include <midihub/routes.h>

#include <string.h>

static int copy_name(char *dst, const char *start, size_t length)
{
    if (length == 0 || length > MH_ROUTE_NAME_MAX)
        return -1;
    memcpy(dst, start, length);
    dst[length] = '\0';
    return 0;
}

static int parse_line(struct mh_route_table *table, const char *line, size_t length)
{
    const char *field[5];
    size_t field_length[5];
    size_t start = 0;
    size_t count = 0;
    size_t i;

    while (length != 0 && (line[length - 1] == ' ' || line[length - 1] == '\r'))
        --length;
    while (start < length && line[start] == ' ')
        ++start;
    if (start == length || line[start] == '#' || line[start] == ';')
        return 0;
    for (i = start; i <= length; ++i) {
        if (i != length && line[i] != '\t')
            continue;
        if (count == 5)
            return -1;
        field[count] = line + start;
        field_length[count++] = i - start;
        start = i + 1;
    }
    if (count != 5 || field_length[0] != 5 ||
        memcmp(field[0], "route", 5) != 0 ||
        field_length[1] != 1 || (field[1][0] != '0' && field[1][0] != '1') ||
        field_length[2] != 1 || (field[2][0] != '0' && field[2][0] != '1') ||
        table->count == MH_ROUTE_MAX)
        return -1;
    table->routes[table->count].enabled = field[1][0] == '1';
    table->routes[table->count].reconnect = field[2][0] == '1';
    if (copy_name(table->routes[table->count].source, field[3], field_length[3]) != 0 ||
        copy_name(table->routes[table->count].destination, field[4], field_length[4]) != 0)
        return -1;
    if (strcmp(table->routes[table->count].source,
               table->routes[table->count].destination) == 0)
        return -1;
    ++table->count;
    return 0;
}

static int reaches(const struct mh_route_table *table, const char *current,
                   const char *target, unsigned char seen[MH_ROUTE_MAX])
{
    size_t i;

    for (i = 0; i < table->count; ++i) {
        const struct mh_route_config *route = &table->routes[i];
        if (!route->enabled || seen[i] || strcmp(route->source, current) != 0)
            continue;
        if (strcmp(route->destination, target) == 0)
            return 1;
        seen[i] = 1;
        if (reaches(table, route->destination, target, seen))
            return 1;
    }
    return 0;
}

int mh_routes_parse(struct mh_route_table *table, const char *text, size_t length,
                    size_t *error_line)
{
    size_t line = 1;
    size_t start = 0;
    size_t i;

    if (!table || (!text && length != 0))
        return -1;
    memset(table, 0, sizeof(*table));
    if (error_line)
        *error_line = 0;
    for (i = 0; i <= length; ++i) {
        if (i != length && text[i] != '\n')
            continue;
        if (parse_line(table, text + start, i - start) != 0) {
            if (error_line)
                *error_line = line;
            memset(table, 0, sizeof(*table));
            return -1;
        }
        start = i + 1;
        ++line;
    }
    for (i = 0; i < table->count; ++i) {
        unsigned char seen[MH_ROUTE_MAX] = {0};
        if (!table->routes[i].enabled)
            continue;
        seen[i] = 1;
        if (reaches(table, table->routes[i].destination,
                    table->routes[i].source, seen)) {
            memset(table, 0, sizeof(*table));
            return -1;
        }
    }
    return 0;
}
