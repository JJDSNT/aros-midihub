#include "midihub/netmidi2.h"
#include "midihub/ump.h"

#include <string.h>

/* Timing; the specification leaves most values to the implementation. */
#define IDLE_FIRST_MS 100       /* first Zero Length UMP Data; spec: within 300 */
#define IDLE_FEC_MS 20          /* FEC repeats of the last data, then doubling */
#define IDLE_STOP_MS 8000       /* stop sending idle commands after this gap */
#define PING_IDLE_MS 5000       /* ping when the peer has been quiet this long */
#define PING_LIMIT 3            /* unanswered pings before Bye Timeout */
#define RETRY_MS 1000           /* Invitation and Bye repeats */
#define RETRY_LIMIT 10

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int mh_nm2_parse(const uint8_t *packet, size_t length,
                 struct mh_nm2_command_packet *commands, size_t capacity)
{
    size_t offset = 4, count = 0;

    if (length < 4 || get32(packet) != MH_NM2_SIGNATURE)
        return -1;
    while (offset < length) {
        size_t words;
        if (length - offset < 4)
            return -1;
        words = packet[offset + 1];
        if (length - offset - 4 < words * 4)
            return -1;
        if (count < capacity) {
            commands[count].code = packet[offset];
            commands[count].csd1 = packet[offset + 2];
            commands[count].csd2 = packet[offset + 3];
            commands[count].payload = packet + offset + 4;
            commands[count].payload_words = words;
        }
        ++count;
        offset += 4 + words * 4;
    }
    return (int)(count < capacity ? count : capacity);
}

int mh_nm2_writer_init(struct mh_nm2_writer *writer, uint8_t *data, size_t capacity)
{
    writer->data = data;
    writer->capacity = capacity < MH_NM2_MAX_PACKET ? capacity : MH_NM2_MAX_PACKET;
    writer->length = 0;
    if (writer->capacity < 4)
        return -1;
    put32(data, MH_NM2_SIGNATURE);
    writer->length = 4;
    return 0;
}

int mh_nm2_add(struct mh_nm2_writer *writer, uint8_t code, uint8_t csd1, uint8_t csd2,
               const uint8_t *payload, size_t payload_bytes)
{
    size_t words = (payload_bytes + 3) / 4;
    uint8_t *p;

    if (words > 255 || writer->capacity - writer->length < 4 + words * 4)
        return -1;
    p = writer->data + writer->length;
    p[0] = code;
    p[1] = (uint8_t)words;
    p[2] = csd1;
    p[3] = csd2;
    memset(p + 4, 0, words * 4);
    if (payload_bytes)
        memcpy(p + 4, payload, payload_bytes);
    writer->length += 4 + words * 4;
    return 0;
}

int mh_nm2_add_identity(struct mh_nm2_writer *writer, uint8_t code, uint8_t csd2,
                        const char *name, const char *product_id)
{
    uint8_t payload[(MH_NM2_NAME_MAX + 3) / 4 * 4 + (MH_NM2_PRODUCT_ID_MAX + 3) / 4 * 4];
    size_t name_length = strlen(name), id_length = strlen(product_id);
    size_t name_words = (name_length + 3) / 4;

    if (name_length > MH_NM2_NAME_MAX || id_length > MH_NM2_PRODUCT_ID_MAX)
        return -1;
    memset(payload, 0, sizeof(payload));
    memcpy(payload, name, name_length);
    memcpy(payload + name_words * 4, product_id, id_length);
    return mh_nm2_add(writer, code, (uint8_t)name_words, csd2, payload,
                      name_words * 4 + id_length);
}

int mh_nm2_add_ping(struct mh_nm2_writer *writer, uint8_t code, uint32_t id)
{
    uint8_t payload[4];
    put32(payload, id);
    return mh_nm2_add(writer, code, 0, 0, payload, 4);
}

int mh_nm2_add_ump(struct mh_nm2_writer *writer, uint16_t sequence,
                   const uint32_t *words, size_t count)
{
    uint8_t payload[MH_NM2_MAX_UMP_WORDS * 4];
    size_t i;

    if (count > MH_NM2_MAX_UMP_WORDS)
        return -1;
    for (i = 0; i < count; ++i)
        put32(payload + i * 4, words[i]);
    return mh_nm2_add(writer, MH_NM2_UMP_DATA, (uint8_t)(sequence >> 8),
                      (uint8_t)sequence, payload, count * 4);
}

int mh_nm2_add_nak(struct mh_nm2_writer *writer, uint8_t reason,
                   const struct mh_nm2_command_packet *original)
{
    uint8_t header[4];
    header[0] = original->code;
    header[1] = (uint8_t)original->payload_words;
    header[2] = original->csd1;
    header[3] = original->csd2;
    return mh_nm2_add(writer, MH_NM2_NAK, reason, 0, header, 4);
}

static void copy_text(char *to, size_t size, const uint8_t *from, size_t length)
{
    size_t i;
    for (i = 0; i < length && i + 1 < size && from[i]; ++i)
        to[i] = (char)from[i];
    to[i] = 0;
}

int mh_nm2_identity(const struct mh_nm2_command_packet *command,
                    char *name, size_t name_size, char *product_id, size_t id_size)
{
    size_t name_words = command->csd1;

    if (name_words > command->payload_words)
        return -1;
    copy_text(name, name_size, command->payload, name_words * 4);
    copy_text(product_id, id_size, command->payload + name_words * 4,
              (command->payload_words - name_words) * 4);
    return 0;
}

/* Sessions */

void mh_nm2_session_init(struct mh_nm2_session *session, int host,
                         const char *name, const char *product_id,
                         mh_nm2_ump_sink sink, void *context)
{
    memset(session, 0, sizeof(*session));
    session->host = host;
    copy_text(session->name, sizeof(session->name), (const uint8_t *)name, strlen(name));
    copy_text(session->product_id, sizeof(session->product_id),
              (const uint8_t *)product_id, strlen(product_id));
    session->protocols = MH_UMP_PROTOCOL_MIDI1;
    session->ump_sink = sink;
    session->ump_context = context;
}

static void set_state(struct mh_nm2_session *session, enum mh_nm2_state state)
{
    session->state = state;
    session->state_since_ms = session->now_ms;
    session->retries = 0;
}

static void start_session(struct mh_nm2_session *session)
{
    set_state(session, MH_NM2_ESTABLISHED);
    session->tx_sequence = 0;
    session->sent_count = 0;
    session->rx_started = 0;
    session->rx_lost = 0;
    session->last_receive_ms = session->now_ms;
    session->last_send_ms = session->now_ms;
    session->idle_sent = 0;
    session->next_idle_ms = session->now_ms + IDLE_STOP_MS;
    session->pings_unanswered = 0;
}

int mh_nm2_invite(struct mh_nm2_session *session, uint32_t now_ms,
                  struct mh_nm2_writer *out)
{
    if (session->host)
        return -1;
    session->now_ms = now_ms;
    set_state(session, MH_NM2_INVITING);
    return mh_nm2_add_identity(out, MH_NM2_INVITATION, 0, session->name,
                               session->product_id);
}

static int append_ump_command(struct mh_nm2_session *session, const uint32_t *words,
                              size_t count, struct mh_nm2_writer *out)
{
    unsigned i;
    struct mh_nm2_sent_ump *slot;

    /* FEC: the previous UMP Data commands first, in the order sent. */
    for (i = 0; i < session->sent_count; ++i)
        if (mh_nm2_add_ump(out, session->sent[i].sequence, session->sent[i].data,
                           session->sent[i].words))
            return -1;
    if (mh_nm2_add_ump(out, session->tx_sequence, words, count))
        return -1;
    if (session->sent_count == MH_NM2_FEC_REPEATS) {
        memmove(&session->sent[0], &session->sent[1],
                sizeof(session->sent[0]) * (MH_NM2_FEC_REPEATS - 1));
        --session->sent_count;
    }
    slot = &session->sent[session->sent_count++];
    slot->sequence = session->tx_sequence;
    slot->words = (uint8_t)count;
    if (count)
        memcpy(slot->data, words, count * sizeof(*words));
    ++session->tx_sequence;
    return 0;
}

int mh_nm2_send_ump(struct mh_nm2_session *session, const uint32_t *words,
                    size_t count, uint32_t now_ms, struct mh_nm2_writer *out)
{
    if (session->state != MH_NM2_ESTABLISHED || count == 0 ||
        count > MH_NM2_MAX_UMP_WORDS)
        return -1;
    session->now_ms = now_ms;
    if (append_ump_command(session, words, count, out))
        return -1;
    session->last_send_ms = now_ms;
    session->idle_sent = 0;
    session->next_idle_ms = now_ms + IDLE_FIRST_MS;
    return 0;
}

static void answer_stream(struct mh_nm2_session *session, const uint32_t *words,
                          struct mh_nm2_writer *out)
{
    uint32_t reply[4 * 8];
    unsigned status = mh_ump_stream_status(words), n;
    unsigned filter;

    if (status == MH_UMP_ENDPOINT_DISCOVERY) {
        filter = words[1] & 0xff;
        if (filter & MH_UMP_FILTER_ENDPOINT_INFO) {
            mh_ump_endpoint_info(reply, session->protocols);
            (void)mh_nm2_send_ump(session, reply, 4, session->now_ms, out);
        }
        if (filter & MH_UMP_FILTER_ENDPOINT_NAME) {
            n = mh_ump_stream_text(reply, sizeof(reply) / sizeof(reply[0]),
                                   MH_UMP_ENDPOINT_NAME, session->name,
                                   strlen(session->name));
            if (n)
                (void)mh_nm2_send_ump(session, reply, n, session->now_ms, out);
        }
        if (filter & MH_UMP_FILTER_PRODUCT_INSTANCE_ID) {
            n = mh_ump_stream_text(reply, sizeof(reply) / sizeof(reply[0]),
                                   MH_UMP_PRODUCT_INSTANCE_ID, session->product_id,
                                   strlen(session->product_id));
            if (n)
                (void)mh_nm2_send_ump(session, reply, n, session->now_ms, out);
        }
        if (filter & MH_UMP_FILTER_STREAM_CONFIG) {
            mh_ump_stream_config(reply, MH_UMP_PROTOCOL_MIDI1);
            (void)mh_nm2_send_ump(session, reply, 4, session->now_ms, out);
        }
    } else if (status == MH_UMP_STREAM_CONFIG_REQUEST) {
        /* This endpoint speaks the MIDI 1.0 protocol in UMP only. */
        mh_ump_stream_config(reply, MH_UMP_PROTOCOL_MIDI1);
        (void)mh_nm2_send_ump(session, reply, 4, session->now_ms, out);
    }
}

static void receive_ump(struct mh_nm2_session *session,
                        const struct mh_nm2_command_packet *command,
                        struct mh_nm2_writer *out)
{
    uint16_t sequence = (uint16_t)((command->csd1 << 8) | command->csd2);
    uint32_t words[MH_NM2_MAX_UMP_WORDS];
    size_t count = command->payload_words, i;

    if (count > MH_NM2_MAX_UMP_WORDS)
        return;
    if (session->rx_started) {
        int16_t ahead = (int16_t)(sequence - session->rx_expected);
        if (ahead < 0)
            return;                 /* an FEC repeat already handled */
        session->rx_lost += (unsigned long)ahead;
    }
    session->rx_started = 1;
    session->rx_expected = (uint16_t)(sequence + 1);
    for (i = 0; i < count; ++i)
        words[i] = get32(command->payload + i * 4);
    for (i = 0; i < count; ) {
        unsigned n = mh_ump_words(words[i]);
        if (i + n > count)
            break;
        if ((words[i] >> 28) == MH_UMP_STREAM)
            answer_stream(session, words + i, out);
        else if (session->ump_sink)
            session->ump_sink(session->ump_context, words + i, n);
        i += n;
    }
}

int mh_nm2_receive(struct mh_nm2_session *session, const uint8_t *packet,
                   size_t length, uint32_t now_ms, struct mh_nm2_writer *out)
{
    struct mh_nm2_command_packet commands[64];
    int count, i;

    count = mh_nm2_parse(packet, length, commands, 64);
    if (count < 0)
        return -1;
    session->now_ms = now_ms;
    session->last_receive_ms = now_ms;
    for (i = 0; i < count; ++i) {
        const struct mh_nm2_command_packet *c = &commands[i];

        switch (c->code) {
        case MH_NM2_INVITATION:
            if (!session->host) {
                (void)mh_nm2_add_nak(out, MH_NM2_NAK_NOT_EXPECTED, c);
                break;
            }
            if (mh_nm2_identity(c, session->peer_name, sizeof(session->peer_name),
                                session->peer_product_id,
                                sizeof(session->peer_product_id)) ||
                !session->peer_name[0] || !session->peer_product_id[0]) {
                (void)mh_nm2_add(out, MH_NM2_BYE, MH_NM2_BYE_PROTOCOL_ERROR, 0, NULL, 0);
                break;
            }
            /* An invitation within a session is accepted again. */
            if (session->state != MH_NM2_ESTABLISHED)
                start_session(session);
            (void)mh_nm2_add_identity(out, MH_NM2_INVITATION_ACCEPTED, 0,
                                      session->name, session->product_id);
            break;
        case MH_NM2_INVITATION_AUTH:
        case MH_NM2_INVITATION_USER_AUTH:
            /* No authentication here; the client may try a plain one. */
            (void)mh_nm2_add(out, MH_NM2_BYE, MH_NM2_BYE_AUTH_NOT_SUPPORTED, 0, NULL, 0);
            break;
        case MH_NM2_INVITATION_ACCEPTED:
            if (session->host)
                (void)mh_nm2_add_nak(out, MH_NM2_NAK_NOT_EXPECTED, c);
            else if (session->state == MH_NM2_INVITING) {
                (void)mh_nm2_identity(c, session->peer_name, sizeof(session->peer_name),
                                      session->peer_product_id,
                                      sizeof(session->peer_product_id));
                start_session(session);
            } else if (session->state != MH_NM2_ESTABLISHED) {
                (void)mh_nm2_add(out, MH_NM2_BYE, MH_NM2_BYE_NO_PENDING_SESSION, 0, NULL, 0);
            }
            break;
        case MH_NM2_INVITATION_PENDING:
            if (session->state == MH_NM2_INVITING)
                session->state_since_ms = now_ms;   /* keep waiting */
            break;
        case MH_NM2_INVITATION_AUTH_REQUIRED:
        case MH_NM2_INVITATION_USER_AUTH_REQUIRED:
            if (session->state == MH_NM2_INVITING) {
                (void)mh_nm2_add(out, MH_NM2_BYE, MH_NM2_BYE_AUTH_NOT_SUPPORTED, 0, NULL, 0);
                set_state(session, MH_NM2_IDLE);
            }
            break;
        case MH_NM2_PING:
            if (c->payload_words >= 1)
                (void)mh_nm2_add_ping(out, MH_NM2_PING_REPLY, get32(c->payload));
            break;
        case MH_NM2_PING_REPLY:
            if (c->payload_words >= 1 && get32(c->payload) == session->ping_id)
                session->pings_unanswered = 0;
            break;
        case MH_NM2_UMP_DATA:
            if (session->state != MH_NM2_ESTABLISHED)
                (void)mh_nm2_add(out, MH_NM2_BYE, MH_NM2_BYE_NOT_ESTABLISHED, 0, NULL, 0);
            else
                receive_ump(session, c, out);
            break;
        case MH_NM2_SESSION_RESET:
            if (session->state == MH_NM2_ESTABLISHED) {
                session->rx_started = 0;
                (void)mh_nm2_add(out, MH_NM2_SESSION_RESET_REPLY, 0, 0, NULL, 0);
            }
            break;
        case MH_NM2_RETRANSMIT_REQUEST:
            /* Retransmit is optional; FEC covers short losses. */
            (void)mh_nm2_add(out, MH_NM2_RETRANSMIT_ERROR, c->csd1, c->csd2, NULL, 0);
            break;
        case MH_NM2_BYE:
            (void)mh_nm2_add(out, MH_NM2_BYE_REPLY, 0, 0, NULL, 0);
            set_state(session, MH_NM2_IDLE);
            break;
        case MH_NM2_BYE_REPLY:
            if (session->state == MH_NM2_CLOSING)
                set_state(session, MH_NM2_IDLE);
            break;
        case MH_NM2_SESSION_RESET_REPLY:
        case MH_NM2_RETRANSMIT_ERROR:
        case MH_NM2_NAK:
            break;
        default:
            (void)mh_nm2_add_nak(out, MH_NM2_NAK_NOT_SUPPORTED, c);
            break;
        }
    }
    return 0;
}

int mh_nm2_bye(struct mh_nm2_session *session, uint8_t reason, uint32_t now_ms,
               struct mh_nm2_writer *out)
{
    session->now_ms = now_ms;
    if (session->state == MH_NM2_IDLE)
        return -1;
    if (session->state == MH_NM2_INVITING)
        reason = MH_NM2_BYE_INVITATION_CANCELED;
    set_state(session, MH_NM2_CLOSING);
    session->bye_reason = reason;
    return mh_nm2_add(out, MH_NM2_BYE, reason, 0, NULL, 0);
}

int mh_nm2_tick(struct mh_nm2_session *session, uint32_t now_ms,
                struct mh_nm2_writer *out)
{
    size_t before = out->length;
    uint32_t elapsed = now_ms - session->state_since_ms;

    session->now_ms = now_ms;
    switch (session->state) {
    case MH_NM2_IDLE:
        break;
    case MH_NM2_INVITING:
        if (elapsed >= RETRY_MS * (session->retries + 1)) {
            if (++session->retries >= RETRY_LIMIT)
                set_state(session, MH_NM2_IDLE);
            else
                (void)mh_nm2_add_identity(out, MH_NM2_INVITATION, 0, session->name,
                                          session->product_id);
        }
        break;
    case MH_NM2_CLOSING:
        if (elapsed >= RETRY_MS * (session->retries + 1)) {
            if (++session->retries >= RETRY_LIMIT / 2)
                set_state(session, MH_NM2_IDLE);
            else
                (void)mh_nm2_add(out, MH_NM2_BYE, session->bye_reason, 0, NULL, 0);
        }
        break;
    case MH_NM2_ESTABLISHED:
        /* Idle: tell the peer there is no data, repeating the last data
           for FEC first, then at growing intervals until stopping. */
        if ((int32_t)(now_ms - session->next_idle_ms) >= 0 &&
            now_ms - session->last_send_ms < IDLE_STOP_MS) {
            (void)append_ump_command(session, NULL, 0, out);
            ++session->idle_sent;
            session->next_idle_ms = now_ms +
                (session->idle_sent <= MH_NM2_FEC_REPEATS ? IDLE_FEC_MS :
                 IDLE_FIRST_MS << (session->idle_sent - MH_NM2_FEC_REPEATS));
        }
        /* Ping a quiet peer; give up after several unanswered pings. */
        if (now_ms - session->last_receive_ms >= PING_IDLE_MS &&
            now_ms - session->ping_sent_ms >= PING_IDLE_MS) {
            if (session->pings_unanswered >= PING_LIMIT) {
                (void)mh_nm2_bye(session, MH_NM2_BYE_TIMEOUT, now_ms, out);
                break;
            }
            session->ping_id = now_ms ^ 0x5a5a0000u;
            session->ping_sent_ms = now_ms;
            ++session->pings_unanswered;
            (void)mh_nm2_add_ping(out, MH_NM2_PING, session->ping_id);
        }
        break;
    }
    return out->length > before;
}
