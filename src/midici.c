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
#define MIDICI_ENDPOINT_INQUIRY_BYTES 16u
#define MIDICI_ENDPOINT_REPLY_BASE_BYTES 18u
#define MIDICI_REPORT_BASE_BYTES      25u

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
    envelope->device_id = bytes[2];
    envelope->sub_id2 = bytes[4];
    envelope->version = bytes[5];
    envelope->source_muid = read_7bit(bytes + 6, 4);
    envelope->destination_muid = read_7bit(bytes + 10, 4);
    if (bytes[5] & 0x70u)
        return MIDICI_UNSUPPORTED;
    return MIDICI_OK;
}

static int known_length_matches(size_t length, size_t needed, uint8_t version)
{
    return version > MIDICI_VERSION_1_2 ? length >= needed : length == needed;
}

static enum midici_result encode_envelope(
    const struct midici_envelope *envelope, size_t needed, uint8_t *bytes,
    size_t capacity, size_t *length)
{
    if (envelope == NULL || bytes == NULL || length == NULL ||
        !device_id_valid(envelope->device_id) ||
        !midici_muid_assignable(envelope->source_muid) ||
        envelope->destination_muid > MIDICI_BROADCAST_MUID ||
        (envelope->version & 0x70u) != 0)
        return MIDICI_INVALID_ARGUMENT;
    *length = needed;
    if (capacity < needed)
        return MIDICI_BUFFER_TOO_SMALL;
    memset(bytes, 0, needed);
    bytes[0] = 0xf0u;
    bytes[1] = MIDICI_UNIVERSAL_NON_REALTIME;
    bytes[2] = envelope->device_id;
    bytes[3] = MIDICI_SUB_ID1;
    bytes[4] = envelope->sub_id2;
    bytes[5] = envelope->version;
    write_7bit(bytes + 6, envelope->source_muid, 4);
    write_7bit(bytes + 10, envelope->destination_muid, 4);
    bytes[needed - 1u] = 0xf7u;
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
    if (!known_length_matches(length, needed, envelope.version))
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

enum midici_result midici_encode_endpoint(
    const struct midici_endpoint_message *message, uint8_t *bytes,
    size_t capacity, size_t *length)
{
    size_t needed;
    enum midici_result result;

    if (message == NULL || bytes == NULL || length == NULL ||
        message->envelope.version < MIDICI_VERSION_1_2 ||
        !midici_muid_assignable(message->envelope.destination_muid) ||
        message->status > 0x7fu || message->data_length > 0x3fffu ||
        (message->data_length != 0 && message->data == NULL) ||
        (message->envelope.sub_id2 != MIDICI_SUB_ID_ENDPOINT_INQUIRY &&
         message->envelope.sub_id2 != MIDICI_SUB_ID_ENDPOINT_REPLY) ||
        (message->envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_INQUIRY &&
         message->data_length != 0))
        return MIDICI_INVALID_ARGUMENT;
    if (message->data_length != 0 &&
        !data_is_7bit(message->data, 0, message->data_length))
        return MIDICI_INVALID_ARGUMENT;

    needed = message->envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_INQUIRY
                 ? MIDICI_ENDPOINT_INQUIRY_BYTES
                 : MIDICI_ENDPOINT_REPLY_BASE_BYTES + message->data_length;
    result = encode_envelope(&message->envelope, needed, bytes, capacity,
                             length);
    if (result != MIDICI_OK)
        return result;
    bytes[14] = message->status;
    if (message->envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_REPLY) {
        write_7bit(bytes + 15, message->data_length, 2);
        if (message->data_length != 0)
            memcpy(bytes + 17, message->data, message->data_length);
    }
    return MIDICI_OK;
}

enum midici_result midici_decode_endpoint(
    const uint8_t *bytes, size_t length,
    struct midici_endpoint_message *message)
{
    struct midici_envelope envelope;
    enum midici_result result;
    size_t needed;
    uint32_t data_length = 0;

    if (message == NULL)
        return MIDICI_INVALID_ARGUMENT;
    result = midici_decode_envelope(bytes, length, &envelope);
    if (result != MIDICI_OK)
        return result;
    if (envelope.sub_id2 != MIDICI_SUB_ID_ENDPOINT_INQUIRY &&
        envelope.sub_id2 != MIDICI_SUB_ID_ENDPOINT_REPLY)
        return MIDICI_UNSUPPORTED;
    if (envelope.version < MIDICI_VERSION_1_2)
        return MIDICI_UNSUPPORTED;
    if (!midici_muid_assignable(envelope.source_muid) ||
        !midici_muid_assignable(envelope.destination_muid))
        return MIDICI_MALFORMED;

    needed = MIDICI_ENDPOINT_INQUIRY_BYTES;
    if (envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_REPLY) {
        if (length < MIDICI_ENDPOINT_REPLY_BASE_BYTES)
            return MIDICI_MALFORMED;
        data_length = read_7bit(bytes + 15, 2);
        needed = MIDICI_ENDPOINT_REPLY_BASE_BYTES + data_length;
    }
    if (!known_length_matches(length, needed, envelope.version))
        return MIDICI_MALFORMED;

    memset(message, 0, sizeof(*message));
    message->envelope = envelope;
    message->status = bytes[14];
    if (envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_REPLY) {
        message->data = bytes + 17;
        message->data_length = (uint16_t)data_length;
    }
    return MIDICI_OK;
}

enum midici_result midici_encode_report(
    const struct midici_report *message, uint8_t *bytes, size_t capacity,
    size_t *length)
{
    size_t needed;
    enum midici_result result;

    if (message == NULL || bytes == NULL || length == NULL ||
        !midici_muid_assignable(message->envelope.destination_muid) ||
        (message->envelope.sub_id2 != MIDICI_SUB_ID_ACK &&
         message->envelope.sub_id2 != MIDICI_SUB_ID_NAK) ||
        message->text_length > 0x3fffu ||
        (message->text_length != 0 && message->text == NULL))
        return MIDICI_INVALID_ARGUMENT;

    if (message->envelope.sub_id2 == MIDICI_SUB_ID_ACK) {
        if (message->envelope.version < MIDICI_VERSION_1_2 ||
            !message->has_report)
            return MIDICI_INVALID_ARGUMENT;
    } else if ((message->envelope.version >= MIDICI_VERSION_1_2) !=
               (message->has_report != 0)) {
        return MIDICI_INVALID_ARGUMENT;
    }
    if (!message->has_report) {
        needed = MIDICI_COMMON_BYTES + 1u;
    } else {
        if (message->original_sub_id2 > 0x7fu ||
            message->status_code > 0x7fu || message->status_data > 0x7fu ||
            !data_is_7bit(message->details, 0, 5) ||
            (message->text_length != 0 &&
             !data_is_7bit(message->text, 0, message->text_length)))
            return MIDICI_INVALID_ARGUMENT;
        needed = MIDICI_REPORT_BASE_BYTES + message->text_length;
    }
    result = encode_envelope(&message->envelope, needed, bytes, capacity,
                             length);
    if (result != MIDICI_OK || !message->has_report)
        return result;
    bytes[14] = message->original_sub_id2;
    bytes[15] = message->status_code;
    bytes[16] = message->status_data;
    memcpy(bytes + 17, message->details, 5);
    write_7bit(bytes + 22, message->text_length, 2);
    if (message->text_length != 0)
        memcpy(bytes + 24, message->text, message->text_length);
    return MIDICI_OK;
}

enum midici_result midici_decode_report(
    const uint8_t *bytes, size_t length, struct midici_report *message)
{
    struct midici_envelope envelope;
    enum midici_result result;
    uint32_t text_length;
    size_t needed;

    if (message == NULL)
        return MIDICI_INVALID_ARGUMENT;
    result = midici_decode_envelope(bytes, length, &envelope);
    if (result != MIDICI_OK)
        return result;
    if (envelope.sub_id2 != MIDICI_SUB_ID_ACK &&
        envelope.sub_id2 != MIDICI_SUB_ID_NAK)
        return MIDICI_UNSUPPORTED;
    if (!midici_muid_assignable(envelope.source_muid) ||
        !midici_muid_assignable(envelope.destination_muid))
        return MIDICI_MALFORMED;

    memset(message, 0, sizeof(*message));
    message->envelope = envelope;
    if (envelope.version < MIDICI_VERSION_1_2) {
        if (envelope.sub_id2 != MIDICI_SUB_ID_NAK ||
            length != MIDICI_COMMON_BYTES + 1u)
            return MIDICI_UNSUPPORTED;
        return MIDICI_OK;
    }
    if (length < MIDICI_REPORT_BASE_BYTES)
        return MIDICI_MALFORMED;
    text_length = read_7bit(bytes + 22, 2);
    needed = MIDICI_REPORT_BASE_BYTES + text_length;
    if (!known_length_matches(length, needed, envelope.version))
        return MIDICI_MALFORMED;
    message->has_report = 1;
    message->original_sub_id2 = bytes[14];
    message->status_code = bytes[15];
    message->status_data = bytes[16];
    memcpy(message->details, bytes + 17, 5);
    message->text = bytes + 24;
    message->text_length = (uint16_t)text_length;
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

enum midici_result midici_decode_invalidate_muid(
    const uint8_t *bytes, size_t length, struct midici_invalidate *message)
{
    struct midici_envelope envelope;
    enum midici_result result;

    if (message == NULL)
        return MIDICI_INVALID_ARGUMENT;
    result = midici_decode_envelope(bytes, length, &envelope);
    if (result != MIDICI_OK)
        return result;
    if (envelope.sub_id2 != MIDICI_SUB_ID_INVALIDATE_MUID)
        return MIDICI_UNSUPPORTED;
    if (!known_length_matches(length, MIDICI_INVALIDATE_BYTES,
                              envelope.version) ||
        envelope.device_id != MIDICI_DEVICE_FUNCTION_BLOCK ||
        !midici_muid_assignable(envelope.source_muid) ||
        envelope.destination_muid != MIDICI_BROADCAST_MUID)
        return MIDICI_MALFORMED;
    memset(message, 0, sizeof(*message));
    message->envelope = envelope;
    message->invalidated_muid = read_7bit(bytes + 14, 4);
    if (!midici_muid_assignable(message->invalidated_muid))
        return MIDICI_MALFORMED;
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

static int responder_config_valid(const struct midici_responder_config *config)
{
    unsigned i;

    if (config == NULL || config->categories > 0x7fu ||
        config->max_sysex_bytes < 128u ||
        config->max_sysex_bytes > MIDICI_BROADCAST_MUID ||
        config->identity.family > 0x3fffu ||
        config->identity.model > 0x3fffu ||
        (config->has_function_block &&
         config->function_block > MIDICI_MAX_FUNCTION_BLOCK) ||
        config->product_instance_id_length > MIDICI_MAX_PRODUCT_INSTANCE_ID ||
        (config->product_instance_id_length != 0 &&
         config->product_instance_id == NULL))
        return 0;
    for (i = 0; i < 3; ++i)
        if (config->identity.manufacturer[i] > 0x7fu)
            return 0;
    for (i = 0; i < 4; ++i)
        if (config->identity.revision[i] > 0x7fu)
            return 0;
    for (i = 0; i < config->product_instance_id_length; ++i)
        if (config->product_instance_id[i] < 32u ||
            config->product_instance_id[i] > 126u)
            return 0;
    return 1;
}

static enum midici_result responder_nak(
    const struct midici_responder *responder,
    const struct midici_envelope *request, uint8_t status, uint8_t *response,
    size_t response_capacity, size_t *response_length)
{
    struct midici_report report;

    memset(&report, 0, sizeof(report));
    report.envelope.device_id = request->device_id;
    report.envelope.sub_id2 = MIDICI_SUB_ID_NAK;
    report.envelope.version = MIDICI_VERSION_1_2;
    report.envelope.source_muid = responder->context.local_muid;
    report.envelope.destination_muid = request->source_muid;
    report.has_report = 1;
    report.original_sub_id2 = request->sub_id2;
    report.status_code = status;
    return midici_encode_report(&report, response, response_capacity,
                                response_length);
}

enum midici_result midici_responder_init(struct midici_responder *responder,
                                         uint32_t entropy)
{
    if (responder == NULL)
        return MIDICI_INVALID_ARGUMENT;
    memset(responder, 0, sizeof(*responder));
    return midici_context_init(&responder->context, entropy);
}

enum midici_result midici_responder_handle(
    struct midici_responder *responder,
    const struct midici_responder_config *config, const uint8_t *bytes,
    size_t length, uint32_t replacement_entropy, uint8_t *response,
    size_t response_capacity, size_t *response_length,
    struct midici_responder_event *event)
{
    struct midici_envelope envelope;
    enum midici_result result;

    if (responder == NULL || !responder_config_valid(config) || bytes == NULL ||
        response == NULL || response_length == NULL || event == NULL)
        return MIDICI_INVALID_ARGUMENT;
    if (response_capacity < MIDICI_RESPONDER_MIN_RESPONSE_CAPACITY) {
        *response_length = MIDICI_RESPONDER_MIN_RESPONSE_CAPACITY;
        return MIDICI_BUFFER_TOO_SMALL;
    }
    *response_length = 0;
    memset(event, 0, sizeof(*event));
    event->local_muid = responder->context.local_muid;

    result = midici_decode_envelope(bytes, length, &envelope);
    if (result == MIDICI_UNSUPPORTED) {
        if (!midici_muid_assignable(envelope.source_muid) ||
            (envelope.destination_muid != responder->context.local_muid &&
             envelope.sub_id2 != MIDICI_SUB_ID_DISCOVERY))
            return MIDICI_UNSUPPORTED;
        event->type = MIDICI_RESPONDER_UNSUPPORTED_VERSION;
        event->peer_muid = envelope.source_muid;
        result = responder_nak(responder, &envelope,
                               MIDICI_NAK_STATUS_VERSION_NOT_SUPPORTED,
                               response, response_capacity, response_length);
        if (result == MIDICI_OK)
            event->emitted_sub_id2 = MIDICI_SUB_ID_NAK;
        return result;
    }
    if (result != MIDICI_OK)
        return result;
    event->peer_muid = envelope.source_muid;

    if (envelope.sub_id2 != MIDICI_SUB_ID_DISCOVERY &&
        envelope.sub_id2 != MIDICI_SUB_ID_INVALIDATE_MUID &&
        envelope.destination_muid != responder->context.local_muid)
        return MIDICI_OK;
    if (envelope.source_muid == responder->context.local_muid &&
        envelope.sub_id2 != MIDICI_SUB_ID_DISCOVERY)
        return MIDICI_OK;

    if (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY) {
        struct midici_discovery inquiry, reply;
        uint32_t previous = responder->context.local_muid;
        int was_used = responder->used != 0;

        result = midici_decode_discovery(bytes, length, &inquiry);
        if (result != MIDICI_OK)
            return result;
        if (envelope.source_muid == previous) {
            result = midici_context_observe_source(
                &responder->context, envelope.source_muid,
                replacement_entropy);
            if (result != MIDICI_COLLISION ||
                responder->context.local_muid == previous)
                return result;
            responder->used = 0;
            event->type = MIDICI_RESPONDER_LOCAL_MUID_CHANGED;
            event->previous_local_muid = previous;
            event->local_muid = responder->context.local_muid;
            if (was_used) {
                result = midici_encode_invalidate_muid(
                    previous, previous, response, response_capacity,
                    response_length);
                if (result == MIDICI_OK)
                    event->emitted_sub_id2 = MIDICI_SUB_ID_INVALIDATE_MUID;
                return result;
            }
        } else {
            event->type = MIDICI_RESPONDER_DISCOVERY;
        }

        memset(&reply, 0, sizeof(reply));
        reply.envelope.device_id = MIDICI_DEVICE_FUNCTION_BLOCK;
        reply.envelope.sub_id2 = MIDICI_SUB_ID_DISCOVERY_REPLY;
        reply.envelope.version = MIDICI_VERSION_1_2;
        reply.envelope.source_muid = responder->context.local_muid;
        reply.envelope.destination_muid = inquiry.envelope.source_muid;
        reply.identity = config->identity;
        reply.categories = config->categories;
        reply.max_sysex_bytes = config->max_sysex_bytes;
        reply.output_path_id = inquiry.output_path_id;
        reply.has_function_block = config->has_function_block;
        reply.function_block = config->function_block;
        result = midici_encode_discovery(&reply, response, response_capacity,
                                         response_length);
        if (result == MIDICI_OK)
            event->emitted_sub_id2 = MIDICI_SUB_ID_DISCOVERY_REPLY;
        return result;
    }

    if (envelope.sub_id2 == MIDICI_SUB_ID_INVALIDATE_MUID) {
        struct midici_invalidate invalidate;

        result = midici_decode_invalidate_muid(bytes, length, &invalidate);
        if (result != MIDICI_OK)
            return result;
        if (invalidate.invalidated_muid == responder->context.local_muid) {
            uint32_t previous = responder->context.local_muid;
            result = midici_context_observe_source(
                &responder->context, previous, replacement_entropy);
            if (result != MIDICI_COLLISION ||
                responder->context.local_muid == previous)
                return result;
            responder->used = 0;
            event->type = MIDICI_RESPONDER_LOCAL_MUID_CHANGED;
            event->previous_local_muid = previous;
            event->local_muid = responder->context.local_muid;
        } else {
            midici_context_cancel_remote(&responder->context,
                                         invalidate.invalidated_muid);
            event->type = MIDICI_RESPONDER_REMOTE_INVALIDATED;
            event->peer_muid = invalidate.invalidated_muid;
        }
        return MIDICI_OK;
    }

    if (envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_INQUIRY) {
        struct midici_endpoint_message inquiry, reply;

        result = midici_decode_endpoint(bytes, length, &inquiry);
        responder->used = 1;
        event->type = MIDICI_RESPONDER_ENDPOINT_INQUIRY;
        if (result != MIDICI_OK) {
            result = responder_nak(responder, &envelope,
                                   MIDICI_NAK_STATUS_MALFORMED, response,
                                   response_capacity, response_length);
            if (result == MIDICI_OK)
                event->emitted_sub_id2 = MIDICI_SUB_ID_NAK;
            return result;
        }
        if (inquiry.status != MIDICI_ENDPOINT_PRODUCT_INSTANCE_ID ||
            config->product_instance_id_length == 0) {
            result = responder_nak(responder, &envelope,
                                   MIDICI_NAK_STATUS_NOT_SUPPORTED, response,
                                   response_capacity, response_length);
            if (result == MIDICI_OK)
                event->emitted_sub_id2 = MIDICI_SUB_ID_NAK;
            return result;
        }
        memset(&reply, 0, sizeof(reply));
        reply.envelope = envelope;
        reply.envelope.sub_id2 = MIDICI_SUB_ID_ENDPOINT_REPLY;
        reply.envelope.version = MIDICI_VERSION_1_2;
        reply.envelope.source_muid = responder->context.local_muid;
        reply.envelope.destination_muid = envelope.source_muid;
        reply.status = inquiry.status;
        reply.data = config->product_instance_id;
        reply.data_length = config->product_instance_id_length;
        result = midici_encode_endpoint(&reply, response, response_capacity,
                                        response_length);
        if (result == MIDICI_OK)
            event->emitted_sub_id2 = MIDICI_SUB_ID_ENDPOINT_REPLY;
        return result;
    }

    if (envelope.sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY ||
        envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_REPLY ||
        envelope.sub_id2 == MIDICI_SUB_ID_ACK ||
        envelope.sub_id2 == MIDICI_SUB_ID_NAK)
        return MIDICI_OK;

    responder->used = 1;
    result = responder_nak(responder, &envelope,
                           MIDICI_NAK_STATUS_NOT_SUPPORTED, response,
                           response_capacity, response_length);
    if (result == MIDICI_OK)
        event->emitted_sub_id2 = MIDICI_SUB_ID_NAK;
    return result;
}
