#include <midihub/config.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
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

void netmidi_config_defaults(struct netmidi_network_config *config)
{
    memset(config, 0, sizeof(*config));
    config->local_port = 5004;
#ifdef __AROS__
    /* the service's name on the network, until a user gives it one */
    strcpy(config->session_name, "AROS");
#else
    /* the Linux build is the program the host tests run */
    strcpy(config->session_name, "AROS MIDIHub");
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
    if (config->peer_name[0])
        fprintf(file, "peer_name=%s\n", config->peer_name);
    if (config->latency_ms)
        fprintf(file, "latency_ms=%d\n", config->latency_ms);
    if (config->accept[0])
        fprintf(file, "accept=%s\n", config->accept);
    if (config->midi2_port)
        fprintf(file, "midi2_port=%u\n", (unsigned)config->midi2_port);
    for (port = 0; port < 16; ++port) {
        if (config->port_name[port][0])
            fprintf(file, "applemidi.port.%d=%s\n", port,
                    config->port_name[port]);
        if (config->midi2_port_name[port][0])
            fprintf(file, "networkmidi2.port.%d=%s\n", port,
                    config->midi2_port_name[port]);
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
    char *value;
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
        length = strlen(line);
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
            if (strlen(value) >= sizeof(next.peer_ip))
                goto done;
            strcpy(next.peer_ip, value);
        } else if (strcmp(key, "session_name") == 0) {
            if (!*value || strlen(value) >= sizeof(next.session_name))
                goto done;
            strcpy(next.session_name, value);
        } else if (strcmp(key, "peer_name") == 0) {
            if (!*value || strlen(value) >= sizeof(next.peer_name))
                goto done;
            strcpy(next.peer_name, value);
        } else if (strcmp(key, "latency_ms") == 0) {
            char *end;
            unsigned long ms;

            errno = 0;
            ms = strtoul(value, &end, 10);
            if (errno || end == value || *end || ms > 2000)
                goto done;
            next.latency_ms = (int)ms;
        } else if (strcmp(key, "accept") == 0) {
            if (!*value || strlen(value) >= sizeof(next.accept))
                goto done;
            strcpy(next.accept, value);
        } else if (strcmp(key, "midi2_port") == 0) {
            if (port_value(value, &next.midi2_port))
                goto done;
        } else if (strncmp(key, "applemidi.port.", 15) == 0 ||
                   strncmp(key, "networkmidi2.port.", 18) == 0) {
            int second = key[0] == 'n';
            const char *number = key + (second ? 18 : 15);
            char (*names)[64] = second ? next.midi2_port_name : next.port_name;
            char *end;
            unsigned long port;

            errno = 0;
            port = strtoul(number, &end, 10);
            if (errno || end == number || *end || port >= 16 || !*value ||
                strlen(value) >= sizeof(next.port_name[0]))
                goto done;
            strcpy(names[port], value);
        } else
            goto done;
    }
    if (ferror(file) || (!!next.peer_ip[0] != !!next.peer_port))
        goto done;
    *config = next;
    result = 0;
done:
    fclose(file);
    return result;
}
