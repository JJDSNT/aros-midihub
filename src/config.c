/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <netmidi/config.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copies text into a field of size bytes, its zero byte with it. Returns
   -1, with nothing copied, when it does not fit. */
static int take(char *field, size_t size, const char *text)
{
    size_t length = 0;

    while (text[length])
        if (++length >= size)
            return -1;
    memcpy(field, text, length + 1);
    return 0;
}

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text))
        ++text;
    for (end = text; *end; ++end)
        ;
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = 0;
    return text;
}

static int port_value(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value;
    if (!*text || *text == '-')
        return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value == 0 || value >= 65535)
        return -1;
    *port = (uint16_t)value;
    return 0;
}

/* "<driver>.port.<n> = <name>": number is the text after the last dot. */
static int port_name(const char *number, const char *value,
                     char (*names)[64])
{
    char *end;
    unsigned long port;

    errno = 0;
    port = strtoul(number, &end, 10);
    if (errno || end == number || *end || port >= 16 || !*value)
        return -1;
    return take(names[port], sizeof(names[0]), value);
}

void netmidi_config_defaults(struct netmidi_network_config *config)
{
    memset(config, 0, sizeof(*config));
    config->local_port = 5004;
#ifdef __AROS__
    /* the service's name on the network, until a user gives it one */
    take(config->session_name, sizeof(config->session_name), "AROS");
#else
    /* the Linux build is the program the host tests run */
    take(config->session_name, sizeof(config->session_name),
         "AROS MIDIHub");
#endif
}

int netmidi_config_save(const char *path,
                        const struct netmidi_network_config *config)
{
    FILE *file;
    int port;
    int failed;

    if (!path || !config)
        return -1;
    file = fopen(path, "w");
    if (!file)
        return -1;
    fprintf(file, "local_port=%u\nsession_name=%s\n",
            (unsigned)config->local_port, config->session_name);
    if (config->peer_ip[0])
        fprintf(file, "peer_ip=%s\npeer_port=%u\n", config->peer_ip,
                (unsigned)config->peer_port);
#ifdef NETMIDI_BROWSE
    if (config->peer_name[0])
        fprintf(file, "peer_name=%s\n", config->peer_name);
#endif
    if (config->max_peers)
        fprintf(file, "max_peers=%d\n", config->max_peers);
    if (config->latency_ms)
        fprintf(file, "latency_ms=%d\n", config->latency_ms);
    if (config->accept[0])
        fprintf(file, "accept=%s\n", config->accept);
#ifdef NETMIDI_MIDI2
    if (config->midi2_port)
        fprintf(file, "midi2_port=%u\n", (unsigned)config->midi2_port);
    if (config->midi2_peer_ip[0])
        fprintf(file, "midi2_peer_ip=%s\nmidi2_peer_port=%u\n",
                config->midi2_peer_ip, (unsigned)config->midi2_peer_port);
#ifdef NETMIDI_BROWSE
    if (config->midi2_peer_name[0])
        fprintf(file, "midi2_peer_name=%s\n", config->midi2_peer_name);
#endif
#endif
    for (port = 0; port < 16; ++port) {
        if (config->port_name[port][0])
            fprintf(file, "applemidi.port.%d=%s\n", port,
                    config->port_name[port]);
#ifdef NETMIDI_MIDI2
        if (config->midi2_port_name[port][0])
            fprintf(file, "networkmidi2.port.%d=%s\n", port,
                    config->midi2_port_name[port]);
#endif
    }
    failed = ferror(file);
    return fclose(file) || failed ? -1 : 0;
}

int netmidi_config_load(const char *path, struct netmidi_network_config *config)
{
    struct netmidi_network_config next;
    FILE *file;
    char line[256];
    char *key;
    const char *value;
    char *equal;
    size_t length;
    int result = -1;

    if (!path || !config)
        return -1;
    file = fopen(path, "r");
    if (!file)
        return errno == ENOENT ? 1 : -1;
    next = *config;
    while (fgets(line, sizeof(line), file)) {
        /* a line that did not fit is no line of ours */
        for (length = 0; line[length]; ++length)
            ;
        if (length && line[length - 1] != '\n' && !feof(file))
            goto done;
        key = trim(line);
        if (!*key || *key == '#' || *key == ';')
            continue;
        equal = strchr(key, '=');
        if (!equal)
            goto done;
        *equal = 0;
        key = trim(key);
        value = trim(equal + 1);
        if (strcmp(key, "local_port") == 0) {
            if (port_value(value, &next.local_port))
                goto done;
        } else if (strcmp(key, "peer_port") == 0) {
            if (port_value(value, &next.peer_port))
                goto done;
        } else if (strcmp(key, "peer_ip") == 0) {
            if (take(next.peer_ip, sizeof(next.peer_ip), value))
                goto done;
        } else if (strcmp(key, "session_name") == 0) {
            if (!*value || take(next.session_name, sizeof(next.session_name), value))
                goto done;
            next.session_name_given = 1;
#ifdef NETMIDI_BROWSE
        } else if (strcmp(key, "peer_name") == 0) {
            if (!*value || take(next.peer_name, sizeof(next.peer_name), value))
                goto done;
#endif
        } else if (strcmp(key, "max_peers") == 0) {
            char *end;
            unsigned long peers;

            errno = 0;
            peers = strtoul(value, &end, 10);
            if (errno || end == value || *end || peers < 1 || peers > 8)
                goto done;
            next.max_peers = (int)peers;
        } else if (strcmp(key, "latency_ms") == 0) {
            char *end;
            unsigned long ms;

            errno = 0;
            ms = strtoul(value, &end, 10);
            if (errno || end == value || *end || ms > 2000)
                goto done;
            next.latency_ms = (int)ms;
        } else if (strcmp(key, "accept") == 0) {
            if (!*value || take(next.accept, sizeof(next.accept), value))
                goto done;
#ifdef NETMIDI_MIDI2
        } else if (strcmp(key, "midi2_port") == 0) {
            if (port_value(value, &next.midi2_port))
                goto done;
        } else if (strcmp(key, "midi2_peer_port") == 0) {
            if (port_value(value, &next.midi2_peer_port))
                goto done;
        } else if (strcmp(key, "midi2_peer_ip") == 0) {
            if (!*value || take(next.midi2_peer_ip, sizeof(next.midi2_peer_ip), value))
                goto done;
#ifdef NETMIDI_BROWSE
        } else if (strcmp(key, "midi2_peer_name") == 0) {
            if (!*value || take(next.midi2_peer_name,
                                sizeof(next.midi2_peer_name), value))
                goto done;
#endif
        } else if (strncmp(key, "networkmidi2.port.", 18) == 0) {
            if (port_name(key + 18, value, next.midi2_port_name))
                goto done;
#endif
        } else if (strncmp(key, "applemidi.port.", 15) == 0) {
            if (port_name(key + 15, value, next.port_name))
                goto done;
        } else
            goto done;
    }
    if (ferror(file) || (!!next.peer_ip[0] != !!next.peer_port))
        goto done;
#ifdef NETMIDI_MIDI2
    if ((!!next.midi2_peer_ip[0] != !!next.midi2_peer_port) ||
        (next.midi2_peer_ip[0] && !next.midi2_port))
        goto done;
#ifdef NETMIDI_BROWSE
    if (next.midi2_peer_name[0] && !next.midi2_port)
        goto done;
#endif
#endif
    *config = next;
    result = 0;
done:
    fclose(file);
    return result;
}
