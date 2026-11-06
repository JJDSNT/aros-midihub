/*
 * Portable MIDI Capability Inquiry foundation.
 *
 * This module owns wire-format and per-engine protocol state only. It does
 * not own a CAMD endpoint, create tasks, read clocks, or perform I/O. AROS
 * integration supplies serialization, time and transport delivery.
 */
#ifndef MIDIHUB_MIDICI_H
#define MIDIHUB_MIDICI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDICI_VERSION_1_1       0x01u
#define MIDICI_VERSION_1_2       0x02u
#define MIDICI_BROADCAST_MUID    0x0fffffffu
#define MIDICI_RESERVED_MUID     0x0fffff00u
#define MIDICI_MAX_TRANSACTIONS  16u
#define MIDICI_MAX_FUNCTION_BLOCK 31u

#define MIDICI_SUB_ID_DISCOVERY        0x70u
#define MIDICI_SUB_ID_DISCOVERY_REPLY  0x71u
#define MIDICI_SUB_ID_ENDPOINT_INQUIRY 0x72u
#define MIDICI_SUB_ID_ENDPOINT_REPLY   0x73u
#define MIDICI_SUB_ID_ACK              0x7du
#define MIDICI_SUB_ID_INVALIDATE_MUID  0x7eu
#define MIDICI_SUB_ID_NAK              0x7fu

#define MIDICI_ENDPOINT_PRODUCT_INSTANCE_ID 0x00u
#define MIDICI_MAX_PRODUCT_INSTANCE_ID      16u
#define MIDICI_RESPONDER_MIN_RESPONSE_CAPACITY 34u

#define MIDICI_ACK_STATUS_OK           0x00u
#define MIDICI_ACK_STATUS_TIMEOUT_WAIT 0x10u
#define MIDICI_NAK_STATUS_NOT_SUPPORTED 0x01u
#define MIDICI_NAK_STATUS_VERSION_NOT_SUPPORTED 0x02u
#define MIDICI_NAK_STATUS_MALFORMED     0x41u

enum midici_result {
    MIDICI_OK = 0,
    MIDICI_INVALID_ARGUMENT,
    MIDICI_BUFFER_TOO_SMALL,
    MIDICI_MALFORMED,
    MIDICI_UNSUPPORTED,
    MIDICI_COLLISION,
    MIDICI_BUSY,
    MIDICI_NOT_FOUND,
    MIDICI_TIMEOUT,
    MIDICI_CAPACITY
};

struct midici_envelope {
    uint8_t device_id;
    uint8_t sub_id2;
    uint8_t version;
    uint32_t source_muid;
    uint32_t destination_muid;
};

struct midici_identity {
    uint8_t manufacturer[3];
    uint16_t family;
    uint16_t model;
    uint8_t revision[4];
};

struct midici_discovery {
    struct midici_envelope envelope;
    struct midici_identity identity;
    uint8_t categories;
    uint32_t max_sysex_bytes;
    uint8_t output_path_id;
    uint8_t has_function_block;
    uint8_t function_block;
};

/*
 * Endpoint Reply data and ACK/NAK text are borrowed views into the frame
 * passed to their decoder. They remain valid only while that frame does.
 */
struct midici_endpoint_message {
    struct midici_envelope envelope;
    uint8_t status;
    const uint8_t *data;
    uint16_t data_length;
};

struct midici_invalidate {
    struct midici_envelope envelope;
    uint32_t invalidated_muid;
};

struct midici_report {
    struct midici_envelope envelope;
    uint8_t has_report;
    uint8_t original_sub_id2;
    uint8_t status_code;
    uint8_t status_data;
    uint8_t details[5];
    const uint8_t *text;
    uint16_t text_length;
};

struct midici_transaction {
    uint8_t active;
    uint8_t request_id;
    uint8_t kind;
    uint32_t remote_muid;
    uint64_t deadline_ms;
};

struct midici_context {
    uint32_t local_muid;
    uint8_t next_request_id;
    struct midici_transaction transactions[MIDICI_MAX_TRANSACTIONS];
};

struct midici_timeout {
    uint8_t request_id;
    uint8_t kind;
    uint32_t remote_muid;
};

struct midici_responder_config {
    struct midici_identity identity;
    uint8_t categories;
    uint32_t max_sysex_bytes;
    uint8_t has_function_block;
    uint8_t function_block;
    const uint8_t *product_instance_id;
    uint8_t product_instance_id_length;
};

struct midici_responder {
    struct midici_context context;
    uint8_t used;
};

enum midici_responder_event_type {
    MIDICI_RESPONDER_IGNORED = 0,
    MIDICI_RESPONDER_DISCOVERY,
    MIDICI_RESPONDER_ENDPOINT_INQUIRY,
    MIDICI_RESPONDER_REMOTE_INVALIDATED,
    MIDICI_RESPONDER_LOCAL_MUID_CHANGED,
    MIDICI_RESPONDER_UNSUPPORTED_VERSION
};

struct midici_responder_event {
    enum midici_responder_event_type type;
    uint8_t emitted_sub_id2;
    uint32_t peer_muid;
    uint32_t previous_local_muid;
    uint32_t local_muid;
};

/* MUIDs are four 7-bit bytes, least significant group first. */
int midici_muid_assignable(uint32_t muid);
uint32_t midici_muid_from_entropy(uint32_t entropy);
enum midici_result midici_muid_encode(uint32_t muid, uint8_t bytes[4]);
enum midici_result midici_muid_decode(const uint8_t bytes[4], uint32_t *muid);

/* Parses the common MIDI-CI Universal SysEx envelope, including F0/F7. */
enum midici_result midici_decode_envelope(const uint8_t *bytes, size_t length,
                                          struct midici_envelope *envelope);

/* Discovery and Reply to Discovery, MIDI-CI 1.2 wire format. */
enum midici_result midici_encode_discovery(
    const struct midici_discovery *message, uint8_t *bytes, size_t capacity,
    size_t *length);
enum midici_result midici_decode_discovery(
    const uint8_t *bytes, size_t length, struct midici_discovery *message);

/* Endpoint Inquiry (data_length == 0) and Endpoint Reply. */
enum midici_result midici_encode_endpoint(
    const struct midici_endpoint_message *message, uint8_t *bytes,
    size_t capacity, size_t *length);
enum midici_result midici_decode_endpoint(
    const uint8_t *bytes, size_t length,
    struct midici_endpoint_message *message);

/* ACK and NAK. A MIDI-CI 1.1 NAK is represented by has_report == 0. */
enum midici_result midici_encode_report(
    const struct midici_report *message, uint8_t *bytes, size_t capacity,
    size_t *length);
enum midici_result midici_decode_report(
    const uint8_t *bytes, size_t length, struct midici_report *message);

enum midici_result midici_encode_invalidate_muid(
    uint32_t source_muid, uint32_t invalidated_muid, uint8_t *bytes,
    size_t capacity, size_t *length);
enum midici_result midici_decode_invalidate_muid(
    const uint8_t *bytes, size_t length, struct midici_invalidate *message);

/*
 * The context is deliberately lock-free and sans-I/O. Exactly one owner
 * serializes calls (the proposed AROS coordinator task); clocks and fresh
 * entropy are supplied by that owner.
 */
enum midici_result midici_context_init(struct midici_context *context,
                                       uint32_t entropy);
enum midici_result midici_context_observe_source(
    struct midici_context *context, uint32_t source_muid,
    uint32_t replacement_entropy);
enum midici_result midici_transaction_begin(
    struct midici_context *context, uint32_t remote_muid, uint8_t kind,
    uint64_t now_ms, uint64_t timeout_ms, uint8_t *request_id);
enum midici_result midici_transaction_complete(
    struct midici_context *context, uint32_t remote_muid, uint8_t request_id,
    uint8_t kind);
enum midici_result midici_transaction_next_timeout(
    struct midici_context *context, uint64_t now_ms,
    struct midici_timeout *timeout);
void midici_context_cancel_remote(struct midici_context *context,
                                  uint32_t remote_muid);

/*
 * Initial Management Responder state machine. It emits at most one complete
 * SysEx response per call. A zero response length means that no reply is
 * required. The caller supplies fresh entropy for any MUID replacement and
 * at least MIDICI_RESPONDER_MIN_RESPONSE_CAPACITY bytes of response storage,
 * so a state transition never loses its mandatory output.
 */
enum midici_result midici_responder_init(struct midici_responder *responder,
                                         uint32_t entropy);
enum midici_result midici_responder_handle(
    struct midici_responder *responder,
    const struct midici_responder_config *config, const uint8_t *bytes,
    size_t length, uint32_t replacement_entropy, uint8_t *response,
    size_t response_capacity, size_t *response_length,
    struct midici_responder_event *event);

#ifdef __cplusplus
}
#endif

#endif
