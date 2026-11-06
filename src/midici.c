/*
 * MIDI-CI 1.2 portable foundation. Section references are to M2-101-UM
 * v1.2.1 unless a compatibility path says otherwise.
 */
#include "midici/midici.h"

#include <string.h>

#define MIDICI_UNIVERSAL_NON_REALTIME 0x7eu
#define MIDICI_DEVICE_FUNCTION_BLOCK  0x7fu
#define MIDICI_SUB_ID1                0x0du
#define MIDICI_COMMON_BYTES           14u
#define MIDICI_DISCOVERY_V1_BYTES     31u
#define MIDICI_DISCOVERY_V2_BYTES     32u
#define MIDICI_DISCOVERY_REPLY_V2_BYTES 33u
#define MIDICI_INVALIDATE_BYTES       19u

static uint32_t read_7bit(const uint8_t *bytes, unsigned count)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < count; ++i)
        value |= (uint32_t)bytes[i] << (7u * i);
    return value;
}

static void write_7bit(uint8_t *bytes, uint32_t value, unsigned count)
{
    unsigned i;

    for (i = 0; i < count; ++i)
        bytes[i] = (uint8_t)((value >> (7u * i)) & 0x7fu);
}

static int data_is_7bit(const uint8_t *bytes, size_t begin, size_t end)
{
    size_t i;

    for (i = begin; i < end; ++i)
        if (bytes[i] & 0x80u)
            return 0;
    return 1;
}

static int device_id_valid(uint8_t device_id)
{
    return device_id <= 0x0fu || device_id == 0x7eu || device_id == 0x7fu;
}

int midici_muid_assignable(uint32_t muid)
{
    return muid < MIDICI_RESERVED_MUID;
}

uint32_t midici_muid_from_entropy(uint32_t entropy)
{
    uint32_t muid = entropy & MIDICI_BROADCAST_MUID;

    if (muid >= MIDICI_RESERVED_MUID)
        muid &= ~0x100u;
    return muid;
}

enum midici_result midici_muid_encode(uint32_t muid, uint8_t bytes[4])
{
    if (bytes == NULL || muid > MIDICI_BROADCAST_MUID)
        return MIDICI_INVALID_ARGUMENT;
    write_7bit(bytes, muid, 4);
    return MIDICI_OK;
}

enum midici_result midici_muid_decode(const uint8_t bytes[4], uint32_t *muid)
{
    if (bytes == NULL || muid == NULL)
        return MIDICI_INVALID_ARGUMENT;
    if (!data_is_7bit(bytes, 0, 4))
        return MIDICI_MALFORMED;
    *muid = read_7bit(bytes, 4);
    return MIDICI_OK;
}

enum midici_result midici_decode_envelope(const uint8_t *bytes, size_t length,
                                          struct midici_envelope *envelope)
{
    if (bytes == NULL || envelope == NULL)
        return MIDICI_INVALID_ARGUMENT;
    if (length < MIDICI_COMMON_BYTES + 1u || bytes[0] != 0xf0u ||
        bytes[length - 1u] != 0xf7u ||
        bytes[1] != MIDICI_UNIVERSAL_NON_REALTIME ||
        bytes[3] != MIDICI_SUB_ID1 || !device_id_valid(bytes[2]) ||
        !data_is_7bit(bytes, 1, length - 1u))
        return MIDICI_MALFORMED;
    if (bytes[5] & 0x70u)
        return MIDICI_UNSUPPORTED;

    envelope->device_id = bytes[2];
    envelope->sub_id2 = bytes[4];
    envelope->version = bytes[5];
    envelope->source_muid = read_7bit(bytes + 6, 4);
    envelope->destination_muid = read_7bit(bytes + 10, 4);
    return MIDICI_OK;
}

static enum midici_result validate_discovery(
    const struct midici_discovery *message)
{
    const struct midici_envelope *envelope;
    unsigned i;

    if (message == NULL)
        return MIDICI_INVALID_ARGUMENT;
    envelope = &message->envelope;
    if (envelope->device_id != MIDICI_DEVICE_FUNCTION_BLOCK ||
        envelope->version != MIDICI_VERSION_1_2 ||
        (envelope->sub_id2 != MIDICI_SUB_ID_DISCOVERY &&
         envelope->sub_id2 != MIDICI_SUB_ID_DISCOVERY_REPLY) ||
        !midici_muid_assignable(envelope->source_muid) ||
        message->categories > 0x7fu || message->max_sysex_bytes < 128u ||
        message->max_sysex_bytes > MIDICI_BROADCAST_MUID ||
        message->output_path_id > 0x7fu || message->identity.family > 0x3fffu ||
        message->identity.model > 0x3fffu)
        return MIDICI_INVALID_ARGUMENT;
    for (i = 0; i < 3; ++i)
        if (message->identity.manufacturer[i] > 0x7fu)
            return MIDICI_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i)
        if (message->identity.revision[i] > 0x7fu)
            return MIDICI_INVALID_ARGUMENT;

    if (envelope->sub_id2 == MIDICI_SUB_ID_DISCOVERY) {
        if (envelope->destination_muid != MIDICI_BROADCAST_MUID ||
            message->has_function_block)
            return MIDICI_INVALID_ARGUMENT;
    } else if (!midici_muid_assignable(envelope->destination_muid) ||
               (message->has_function_block &&
                message->function_block > MIDICI_MAX_FUNCTION_BLOCK)) {
        return MIDICI_INVALID_ARGUMENT;
    }
    return MIDICI_OK;
}

enum midici_result midici_encode_discovery(
    const struct midici_discovery *message, uint8_t *bytes, size_t capacity,
    size_t *length)
{
    size_t needed;
    enum midici_result result = validate_discovery(message);

    if (bytes == NULL || length == NULL)
        return MIDICI_INVALID_ARGUMENT;
    if (result != MIDICI_OK)
        return result;
    needed = message->envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY
                 ? MIDICI_DISCOVERY_REPLY_V2_BYTES
                 : MIDICI_DISCOVERY_V2_BYTES;
    *length = needed;
    if (capacity < needed)
        return MIDICI_BUFFER_TOO_SMALL;

    memset(bytes, 0, needed);
    bytes[0] = 0xf0u;
    bytes[1] = MIDICI_UNIVERSAL_NON_REALTIME;
    bytes[2] = message->envelope.device_id;
    bytes[3] = MIDICI_SUB_ID1;
    bytes[4] = message->envelope.sub_id2;
    bytes[5] = message->envelope.version;
    (void)midici_muid_encode(message->envelope.source_muid, bytes + 6);
    (void)midici_muid_encode(message->envelope.destination_muid, bytes + 10);
    memcpy(bytes + 14, message->identity.manufacturer, 3);
    write_7bit(bytes + 17, message->identity.family, 2);
    write_7bit(bytes + 19, message->identity.model, 2);
    memcpy(bytes + 21, message->identity.revision, 4);
    bytes[25] = message->categories;
    write_7bit(bytes + 26, message->max_sysex_bytes, 4);
    bytes[30] = message->output_path_id;
    if (message->envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY)
        bytes[31] = message->has_function_block ? message->function_block : 0x7fu;
    bytes[needed - 1u] = 0xf7u;
    return MIDICI_OK;
}

enum midici_result midici_decode_discovery(
    const uint8_t *bytes, size_t length, struct midici_discovery *message)
{
    struct midici_envelope envelope;
    size_t needed;
    enum midici_result result;

    if (message == NULL)
        return MIDICI_INVALID_ARGUMENT;
    result = midici_decode_envelope(bytes, length, &envelope);
    if (result != MIDICI_OK)
        return result;
    if (envelope.sub_id2 != MIDICI_SUB_ID_DISCOVERY &&
        envelope.sub_id2 != MIDICI_SUB_ID_DISCOVERY_REPLY)
        return MIDICI_UNSUPPORTED;
    if (envelope.device_id != MIDICI_DEVICE_FUNCTION_BLOCK ||
        !midici_muid_assignable(envelope.source_muid) ||
        (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY &&
         envelope.destination_muid != MIDICI_BROADCAST_MUID) ||
        (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY &&
         !midici_muid_assignable(envelope.destination_muid)))
        return MIDICI_MALFORMED;

    needed = envelope.version >= MIDICI_VERSION_1_2
                 ? (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY
                        ? MIDICI_DISCOVERY_REPLY_V2_BYTES
                        : MIDICI_DISCOVERY_V2_BYTES)
                 : MIDICI_DISCOVERY_V1_BYTES;
    if (length < needed)
        return MIDICI_MALFORMED;
    if (read_7bit(bytes + 26, 4) < 128u)
        return MIDICI_MALFORMED;

    memset(message, 0, sizeof(*message));
    message->envelope = envelope;
    memcpy(message->identity.manufacturer, bytes + 14, 3);
    message->identity.family = (uint16_t)read_7bit(bytes + 17, 2);
    message->identity.model = (uint16_t)read_7bit(bytes + 19, 2);
    memcpy(message->identity.revision, bytes + 21, 4);
    message->categories = bytes[25];
    message->max_sysex_bytes = read_7bit(bytes + 26, 4);
    if (envelope.version >= MIDICI_VERSION_1_2) {
        message->output_path_id = bytes[30];
        if (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY &&
            bytes[31] != 0x7fu) {
            if (bytes[31] > MIDICI_MAX_FUNCTION_BLOCK)
                return MIDICI_MALFORMED;
            message->has_function_block = 1;
            message->function_block = bytes[31];
        }
    }
    return MIDICI_OK;
}

enum midici_result midici_encode_invalidate_muid(
    uint32_t source_muid, uint32_t invalidated_muid, uint8_t *bytes,
    size_t capacity, size_t *length)
{
    if (bytes == NULL || length == NULL || !midici_muid_assignable(source_muid) ||
        !midici_muid_assignable(invalidated_muid))
        return MIDICI_INVALID_ARGUMENT;
    *length = MIDICI_INVALIDATE_BYTES;
    if (capacity < MIDICI_INVALIDATE_BYTES)
        return MIDICI_BUFFER_TOO_SMALL;
    bytes[0] = 0xf0u;
    bytes[1] = MIDICI_UNIVERSAL_NON_REALTIME;
    bytes[2] = MIDICI_DEVICE_FUNCTION_BLOCK;
    bytes[3] = MIDICI_SUB_ID1;
    bytes[4] = MIDICI_SUB_ID_INVALIDATE_MUID;
    bytes[5] = MIDICI_VERSION_1_2;
    (void)midici_muid_encode(source_muid, bytes + 6);
    (void)midici_muid_encode(MIDICI_BROADCAST_MUID, bytes + 10);
    (void)midici_muid_encode(invalidated_muid, bytes + 14);
    bytes[18] = 0xf7u;
    return MIDICI_OK;
}

enum midici_result midici_context_init(struct midici_context *context,
                                       uint32_t entropy)
{
    if (context == NULL)
        return MIDICI_INVALID_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->local_muid = midici_muid_from_entropy(entropy);
    return MIDICI_OK;
}

enum midici_result midici_context_observe_source(
    struct midici_context *context, uint32_t source_muid,
    uint32_t replacement_entropy)
{
    uint32_t replacement;

    if (context == NULL || source_muid > MIDICI_BROADCAST_MUID)
        return MIDICI_INVALID_ARGUMENT;
    if (source_muid != context->local_muid)
        return MIDICI_OK;
    replacement = midici_muid_from_entropy(replacement_entropy);
    if (replacement == context->local_muid)
        replacement = midici_muid_from_entropy(replacement_entropy ^ 0x05a5a5a5u);
    if (replacement == context->local_muid)
        return MIDICI_COLLISION;
    context->local_muid = replacement;
    memset(context->transactions, 0, sizeof(context->transactions));
    return MIDICI_COLLISION;
}

static int request_id_in_use(const struct midici_context *context, uint8_t id)
{
    unsigned i;

    for (i = 0; i < MIDICI_MAX_TRANSACTIONS; ++i)
        if (context->transactions[i].active &&
            context->transactions[i].request_id == id)
            return 1;
    return 0;
}

enum midici_result midici_transaction_begin(
    struct midici_context *context, uint32_t remote_muid, uint8_t kind,
    uint64_t now_ms, uint64_t timeout_ms, uint8_t *request_id)
{
    struct midici_transaction *free_slot = NULL;
    unsigned i, tries;
    uint8_t id;

    if (context == NULL || request_id == NULL ||
        !midici_muid_assignable(remote_muid) || timeout_ms == 0 ||
        UINT64_MAX - now_ms < timeout_ms)
        return MIDICI_INVALID_ARGUMENT;
    for (i = 0; i < MIDICI_MAX_TRANSACTIONS; ++i) {
        struct midici_transaction *transaction = &context->transactions[i];
        if (!transaction->active && free_slot == NULL)
            free_slot = transaction;
        if (transaction->active && transaction->remote_muid == remote_muid &&
            transaction->kind == kind)
            return MIDICI_BUSY;
    }
    if (free_slot == NULL)
        return MIDICI_CAPACITY;
    id = context->next_request_id;
    for (tries = 0; tries < 128u && request_id_in_use(context, id); ++tries)
        id = (uint8_t)((id + 1u) & 0x7fu);
    if (tries == 128u)
        return MIDICI_CAPACITY;
    context->next_request_id = (uint8_t)((id + 1u) & 0x7fu);
    free_slot->active = 1;
    free_slot->request_id = id;
    free_slot->kind = kind;
    free_slot->remote_muid = remote_muid;
    free_slot->deadline_ms = now_ms + timeout_ms;
    *request_id = id;
    return MIDICI_OK;
}

enum midici_result midici_transaction_complete(
    struct midici_context *context, uint32_t remote_muid, uint8_t request_id,
    uint8_t kind)
{
    unsigned i;

    if (context == NULL || !midici_muid_assignable(remote_muid) ||
        request_id > 0x7fu)
        return MIDICI_INVALID_ARGUMENT;
    for (i = 0; i < MIDICI_MAX_TRANSACTIONS; ++i) {
        struct midici_transaction *transaction = &context->transactions[i];
        if (transaction->active && transaction->remote_muid == remote_muid &&
            transaction->request_id == request_id && transaction->kind == kind) {
            memset(transaction, 0, sizeof(*transaction));
            return MIDICI_OK;
        }
    }
    return MIDICI_NOT_FOUND;
}

enum midici_result midici_transaction_next_timeout(
    struct midici_context *context, uint64_t now_ms,
    struct midici_timeout *timeout)
{
    unsigned i;

    if (context == NULL || timeout == NULL)
        return MIDICI_INVALID_ARGUMENT;
    for (i = 0; i < MIDICI_MAX_TRANSACTIONS; ++i) {
        struct midici_transaction *transaction = &context->transactions[i];
        if (transaction->active && transaction->deadline_ms <= now_ms) {
            timeout->request_id = transaction->request_id;
            timeout->kind = transaction->kind;
            timeout->remote_muid = transaction->remote_muid;
            memset(transaction, 0, sizeof(*transaction));
            return MIDICI_TIMEOUT;
        }
    }
    return MIDICI_NOT_FOUND;
}

void midici_context_cancel_remote(struct midici_context *context,
                                  uint32_t remote_muid)
{
    unsigned i;

    if (context == NULL)
        return;
    for (i = 0; i < MIDICI_MAX_TRANSACTIONS; ++i)
        if (context->transactions[i].active &&
            context->transactions[i].remote_muid == remote_muid)
            memset(&context->transactions[i], 0,
                   sizeof(context->transactions[i]));
}
