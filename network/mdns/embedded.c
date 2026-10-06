#include "midihub/mdns.h"

#include <string.h>

static const char service_type[] = "_apple-midi._udp.local";

struct writer {
    uint8_t *data;
    size_t capacity;
    size_t length;
};

static int bytes(struct writer *w, const void *source, size_t count)
{
    if (count > w->capacity - w->length) return -1;
    memcpy(w->data + w->length, source, count);
    w->length += count;
    return 0;
}

static int octet(struct writer *w, uint8_t value)
{
    return bytes(w, &value, 1);
}

static int word(struct writer *w, uint16_t value)
{
    return octet(w, (uint8_t)(value >> 8)) ||
           octet(w, (uint8_t)value) ? -1 : 0;
}

static int dword(struct writer *w, uint32_t value)
{
    return word(w, (uint16_t)(value >> 16)) ||
           word(w, (uint16_t)value) ? -1 : 0;
}

static int label(struct writer *w, const char *start, size_t length)
{
    if (!length || length > 63 || octet(w, (uint8_t)length) < 0)
        return -1;
    return bytes(w, start, length);
}

static int dotted(struct writer *w, const char *name)
{
    const char *part = name;
    const char *end;
    if (!name || !*name) return -1;
    while (*part) {
        end = strchr(part, '.');
        if (!end) end = part + strlen(part);
        if (label(w, part, (size_t)(end - part)) < 0) return -1;
        if (!*end) break;
        part = end + 1;
    }
    return octet(w, 0);
}

static int instance(struct writer *w, const char *name)
{
    if (!name || label(w, name, strlen(name)) < 0) return -1;
    return dotted(w, service_type);
}

static int host(struct writer *w, const char *name)
{
    if (!name || label(w, name, strlen(name)) < 0) return -1;
    return dotted(w, "local");
}

static int record_head(struct writer *w, uint16_t type, uint16_t klass,
                       uint32_t ttl, size_t *length_at)
{
    if (word(w, type) < 0 || word(w, klass) < 0 ||
        dword(w, ttl) < 0)
        return -1;
    *length_at = w->length;
    return word(w, 0);
}

static int record_end(struct writer *w, size_t length_at)
{
    size_t rdlength = w->length - length_at - 2;
    if (rdlength > 65535) return -1;
    w->data[length_at] = (uint8_t)(rdlength >> 8);
    w->data[length_at + 1] = (uint8_t)rdlength;
    return 0;
}

int mh_mdns_build(const char *session_name, const char *host_name,
                  const uint8_t address[4], uint16_t control_port,
                  uint32_t ttl, uint16_t query_id,
                  uint8_t *out, size_t capacity, size_t *length)
{
    struct writer w;
    size_t at;
    if (!session_name || !*session_name || strlen(session_name) > 63 ||
        !host_name || !*host_name || strlen(host_name) > 63 ||
        !address || !control_port || !out || !length)
        return -1;
    w.data = out;
    w.capacity = capacity;
    w.length = 0;
    if (word(&w, query_id) < 0 || word(&w, 0x8400) < 0 ||
        word(&w, 0) < 0 || word(&w, 4) < 0 ||
        word(&w, 0) < 0 || word(&w, 0) < 0)
        return -1;
    if (dotted(&w, service_type) < 0 ||
        record_head(&w, 12, 1, ttl, &at) < 0 ||
        instance(&w, session_name) < 0 || record_end(&w, at) < 0)
        return -1;
    if (instance(&w, session_name) < 0 ||
        record_head(&w, 33, 0x8001, ttl, &at) < 0 ||
        word(&w, 0) < 0 || word(&w, 0) < 0 ||
        word(&w, control_port) < 0 || host(&w, host_name) < 0 ||
        record_end(&w, at) < 0)
        return -1;
    if (instance(&w, session_name) < 0 ||
        record_head(&w, 16, 0x8001, ttl, &at) < 0 ||
        octet(&w, 0) < 0 || record_end(&w, at) < 0)
        return -1;
    if (host(&w, host_name) < 0 ||
        record_head(&w, 1, 0x8001, ttl, &at) < 0 ||
        bytes(&w, address, 4) < 0 || record_end(&w, at) < 0)
        return -1;
    *length = w.length;
    return 0;
}

static uint16_t read_word(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}

static int equal_ascii(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return !*a && !*b;
}

static int read_name(const uint8_t *data, size_t length, size_t *offset,
                     char *name, size_t capacity)
{
    size_t cursor = *offset;
    size_t used = 0;
    size_t jumps = 0;
    int jumped = 0;
    uint8_t part;
    for (;;) {
        if (cursor >= length || ++jumps > length) return -1;
        part = data[cursor++];
        if ((part & 0xc0) == 0xc0) {
            size_t target;
            if (cursor >= length) return -1;
            target = ((size_t)(part & 0x3f) << 8) | data[cursor++];
            if (target >= length || target >= cursor - 2) return -1;
            if (!jumped) *offset = cursor;
            cursor = target;
            jumped = 1;
            continue;
        }
        if (part & 0xc0) return -1;
        if (!part) {
            if (!jumped) *offset = cursor;
            if (used >= capacity) return -1;
            name[used] = 0;
            return 0;
        }
        if (part > 63 || part > length - cursor ||
            used + part + 1 >= capacity)
            return -1;
        if (used) name[used++] = '.';
        memcpy(name + used, data + cursor, part);
        used += part;
        cursor += part;
    }
}

int mh_mdns_query(const uint8_t *packet, size_t length,
                  const char *session_name, const char *host_name,
                  int *unicast)
{
    size_t offset = 12;
    uint16_t count;
    uint16_t type;
    uint16_t klass;
    size_t i;
    char name[256];
    char expected[256];
    int found = 0;
    if (!packet || !session_name || !host_name || !unicast || length < 12 ||
        (packet[2] & 0x80))
        return -1;
    count = read_word(packet + 4);
    *unicast = 0;
    for (i = 0; i < count; ++i) {
        if (read_name(packet, length, &offset, name, sizeof(name)) < 0 ||
            length - offset < 4)
            return -1;
        type = read_word(packet + offset);
        klass = read_word(packet + offset + 2);
        offset += 4;
        if ((klass & 0x7fff) != 1) continue;
        if (equal_ascii(name, service_type) &&
            (type == 12 || type == 255))
            found = 1;
        if (strlen(session_name) + strlen(service_type) + 2 <
            sizeof(expected)) {
            strcpy(expected, session_name);
            strcat(expected, ".");
            strcat(expected, service_type);
            if (equal_ascii(name, expected) &&
                (type == 33 || type == 16 || type == 255))
                found = 1;
        }
        if (strlen(host_name) + 7 < sizeof(expected)) {
            strcpy(expected, host_name);
            strcat(expected, ".local");
            if (equal_ascii(name, expected) &&
                (type == 1 || type == 255))
                found = 1;
        }
        if (found && (klass & 0x8000)) *unicast = 1;
    }
    return found;
}
