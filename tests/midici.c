#include "midici/midici.h"
#include "ump/ump.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static const uint8_t discovery_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x70, 0x02,
    0x67, 0x0a, 0x0d, 0x09, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7d, 0x00, 0x00, 0x02, 0x02, 0x03, 0x00, 0x00,
    0x01, 0x02, 0x03, 0x0c, 0x00, 0x04, 0x00, 0x00,
    0x00, 0xf7
};

static void test_discovery(void)
{
    struct midici_discovery decoded, built;
    uint8_t bytes[64];
    size_t length = 0;

    CHECK(midici_decode_discovery(discovery_fixture,
                                  sizeof(discovery_fixture), &decoded) == MIDICI_OK);
    CHECK(decoded.envelope.source_muid == 0x01234567u);
    CHECK(decoded.envelope.destination_muid == MIDICI_BROADCAST_MUID);
    CHECK(decoded.identity.manufacturer[0] == 0x7d);
    CHECK(decoded.identity.family == 0x0102u);
    CHECK(decoded.identity.model == 3u);
    CHECK(decoded.categories == 0x0cu);
    CHECK(decoded.max_sysex_bytes == 512u);
    CHECK(!decoded.has_function_block);
    CHECK(midici_encode_discovery(&decoded, bytes, sizeof(bytes), &length) == MIDICI_OK);
    CHECK(length == sizeof(discovery_fixture));
    CHECK(memcmp(bytes, discovery_fixture, length) == 0);

    built = decoded;
    built.envelope.sub_id2 = MIDICI_SUB_ID_DISCOVERY_REPLY;
    built.envelope.source_muid = 0x07654321u;
    built.envelope.destination_muid = decoded.envelope.source_muid;
    built.has_function_block = 1;
    built.function_block = 3;
    CHECK(midici_encode_discovery(&built, bytes, sizeof(bytes), &length) == MIDICI_OK);
    CHECK(length == 33u);
    CHECK(midici_decode_discovery(bytes, length, &decoded) == MIDICI_OK);
    CHECK(decoded.has_function_block && decoded.function_block == 3u);
}

static void test_malformed(void)
{
    struct midici_discovery decoded;
    uint8_t bytes[sizeof(discovery_fixture) + 2];

    memcpy(bytes, discovery_fixture, sizeof(discovery_fixture));
    CHECK(midici_decode_discovery(bytes, sizeof(discovery_fixture) - 1u,
                                  &decoded) == MIDICI_MALFORMED);
    bytes[20] = 0x80;
    CHECK(midici_decode_discovery(bytes, sizeof(discovery_fixture),
                                  &decoded) == MIDICI_MALFORMED);
    memcpy(bytes, discovery_fixture, sizeof(discovery_fixture));
    bytes[5] = 0x12;
    CHECK(midici_decode_discovery(bytes, sizeof(discovery_fixture),
                                  &decoded) == MIDICI_UNSUPPORTED);

    /* A later minor version may append fields; the known prefix is read. */
    memcpy(bytes, discovery_fixture, sizeof(discovery_fixture) - 1u);
    bytes[5] = 3;
    bytes[31] = 0x11;
    bytes[32] = 0x22;
    bytes[33] = 0xf7;
    CHECK(midici_decode_discovery(bytes, 34u, &decoded) == MIDICI_OK);

    memcpy(bytes, discovery_fixture, sizeof(discovery_fixture));
    bytes[4] = MIDICI_SUB_ID_DISCOVERY_REPLY;
    CHECK(midici_muid_encode(0x07654321u, bytes + 10) == MIDICI_OK);
    bytes[31] = MIDICI_MAX_FUNCTION_BLOCK + 1u;
    bytes[32] = 0xf7;
    CHECK(midici_decode_discovery(bytes, 33u, &decoded) == MIDICI_MALFORMED);
}

static uint32_t ump_words_seen[64];
static unsigned ump_word_count;

static void take_ump(void *context, const uint32_t *words, unsigned count)
{
    (void)context;
    CHECK(ump_word_count + count <= 64u);
    if (ump_word_count + count <= 64u) {
        memcpy(ump_words_seen + ump_word_count, words, count * sizeof(*words));
        ump_word_count += count;
    }
}

struct sysex_sink {
    uint8_t bytes[64];
    size_t length;
};

static void take_sysex(void *context, const uint8_t *bytes, size_t length)
{
    struct sysex_sink *sink = context;

    if (bytes[0] != 0xf0)
        return;
    CHECK(length <= sizeof(sink->bytes));
    if (length <= sizeof(sink->bytes)) {
        memcpy(sink->bytes, bytes, length);
        sink->length = length;
    }
}

static void test_sysex7_ump_roundtrip(void)
{
    struct ump_from_midi1 from;
    struct ump_to_midi1 to;
    struct sysex_sink sink;
    struct midici_discovery decoded;
    unsigned i, offset;

    ump_from_midi1_init(&from, 5);
    ump_word_count = 0;
    for (i = 0; i < sizeof(discovery_fixture); ++i)
        ump_from_midi1_byte(&from, discovery_fixture[i], take_ump, NULL);
    CHECK(ump_word_count > 2u && (ump_word_count % 2u) == 0u);

    memset(&sink, 0, sizeof(sink));
    ump_to_midi1_init(&to, 5);
    ump_to_midi1_buffer(&to, sink.bytes, sizeof(sink.bytes));
    for (offset = 0; offset < ump_word_count; offset += 2u)
        CHECK(ump_to_midi1(&to, ump_words_seen + offset, 2,
                           take_sysex, &sink) == 0);
    CHECK(sink.length == sizeof(discovery_fixture));
    CHECK(memcmp(sink.bytes, discovery_fixture, sink.length) == 0);
    CHECK(midici_decode_discovery(sink.bytes, sink.length, &decoded) == MIDICI_OK);
}

static void test_muid_and_transactions(void)
{
    struct midici_context first, second;
    struct midici_timeout timeout;
    uint8_t muid_bytes[4];
    uint8_t first_id, second_id;
    uint32_t old;

    CHECK(midici_context_init(&first, 0xffffffffu) == MIDICI_OK);
    CHECK(midici_muid_assignable(first.local_muid));
    CHECK(midici_muid_encode(0x10000000u, muid_bytes) == MIDICI_INVALID_ARGUMENT);
    CHECK(midici_context_init(&second, 0xffffffffu) == MIDICI_OK);
    CHECK(first.local_muid == second.local_muid);
    old = second.local_muid;
    CHECK(midici_context_observe_source(&second, first.local_muid,
                                        0x12345678u) == MIDICI_COLLISION);
    CHECK(second.local_muid != old && midici_muid_assignable(second.local_muid));

    CHECK(midici_transaction_begin(&first, 0x1234u, 0x34u, 100u, 3000u,
                                   &first_id) == MIDICI_OK);
    CHECK(midici_transaction_begin(&first, 0x1234u, 0x34u, 101u, 3000u,
                                   &second_id) == MIDICI_BUSY);
    CHECK(midici_transaction_begin(&first, 0x1234u, 0x30u, 101u, 3000u,
                                   &second_id) == MIDICI_OK);
    CHECK(first_id != second_id);
    CHECK(midici_transaction_complete(&first, 0x1234u, first_id, 0x34u) == MIDICI_OK);
    CHECK(midici_transaction_complete(&first, 0x1234u, first_id, 0x34u) ==
          MIDICI_NOT_FOUND);
    CHECK(midici_transaction_next_timeout(&first, 3100u, &timeout) ==
          MIDICI_NOT_FOUND);
    CHECK(midici_transaction_next_timeout(&first, 3101u, &timeout) == MIDICI_TIMEOUT);
    CHECK(timeout.request_id == second_id && timeout.kind == 0x30u);
}

static void test_invalidate(void)
{
    static const uint8_t expected[] = {
        0xf0, 0x7e, 0x7f, 0x0d, 0x7e, 0x02,
        0x67, 0x0a, 0x0d, 0x09, 0x7f, 0x7f, 0x7f, 0x7f,
        0x21, 0x06, 0x15, 0x3b, 0xf7
    };
    uint8_t bytes[sizeof(expected)];
    size_t length = 0;

    CHECK(midici_encode_invalidate_muid(0x01234567u, 0x07654321u, bytes,
                                        sizeof(bytes), &length) == MIDICI_OK);
    CHECK(length == sizeof(expected));
    CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
}

int main(void)
{
    test_discovery();
    test_malformed();
    test_sysex7_ump_roundtrip();
    test_muid_and_transactions();
    test_invalidate();
    printf("midici: %d failure(s)\n", failures);
    return failures != 0;
}
