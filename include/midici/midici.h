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
#define MIDICI_SUB_ID_INVALIDATE_MUID  0x7eu

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

enum midici_result midici_encode_invalidate_muid(
    uint32_t source_muid, uint32_t invalidated_muid, uint8_t *bytes,
    size_t capacity, size_t *length);

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

#ifdef __cplusplus
}
#endif

#endif
