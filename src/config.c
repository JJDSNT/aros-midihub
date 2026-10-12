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
    strcpy(config->session_name, "AROS MIDIHub");
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
        } else if (strncmp(key, "port.", 5) == 0) {
            char *end;
            unsigned long port;

            errno = 0;
            port = strtoul(key + 5, &end, 10);
            if (errno || end == key + 5 || *end ||
                port >= sizeof(next.port_name) / sizeof(next.port_name[0]) ||
                !*value || strlen(value) >= sizeof(next.port_name[0]))
                goto done;
            strcpy(next.port_name[port], value);
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
