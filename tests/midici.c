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

static const uint8_t endpoint_inquiry_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x72, 0x02,
    0x67, 0x0a, 0x0d, 0x09, 0x21, 0x06, 0x15, 0x3b,
    0x00, 0xf7
};

static const uint8_t endpoint_reply_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x73, 0x02,
    0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09,
    0x00, 0x08, 0x00, 0x46, 0x4d, 0x31, 0x2d, 0x30, 0x30, 0x34,
    0x32, 0xf7
};

static const uint8_t ack_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x7d, 0x02,
    0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09,
    0x34, 0x10, 0x0a, 0x01, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x77, 0x61, 0x69, 0x74, 0xf7
};

static const uint8_t nak_v1_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x7f, 0x01,
    0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09, 0xf7
};

static const uint8_t nak_v2_fixture[] = {
    0xf0, 0x7e, 0x7f, 0x0d, 0x7f, 0x02,
    0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09,
    0x34, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x62, 0x61, 0x64, 0x20, 0x6a, 0xf7
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

static void test_endpoint_and_reports(void)
{
    struct midici_endpoint_message endpoint;
    struct midici_report report;
    uint8_t bytes[64];
    size_t length = 0;

    CHECK(midici_decode_endpoint(endpoint_inquiry_fixture,
                                 sizeof(endpoint_inquiry_fixture),
                                 &endpoint) == MIDICI_OK);
    CHECK(endpoint.envelope.sub_id2 == MIDICI_SUB_ID_ENDPOINT_INQUIRY);
    CHECK(endpoint.status == MIDICI_ENDPOINT_PRODUCT_INSTANCE_ID);
    CHECK(endpoint.data == NULL && endpoint.data_length == 0);
    CHECK(midici_encode_endpoint(&endpoint, bytes, sizeof(bytes), &length) ==
          MIDICI_OK);
    CHECK(length == sizeof(endpoint_inquiry_fixture));
    CHECK(memcmp(bytes, endpoint_inquiry_fixture, length) == 0);

    CHECK(midici_decode_endpoint(endpoint_reply_fixture,
                                 sizeof(endpoint_reply_fixture),
                                 &endpoint) == MIDICI_OK);
    CHECK(endpoint.data_length == 8u);
    CHECK(memcmp(endpoint.data, "FM1-0042", 8) == 0);
    CHECK(midici_encode_endpoint(&endpoint, bytes, sizeof(bytes), &length) ==
          MIDICI_OK);
    CHECK(length == sizeof(endpoint_reply_fixture));
    CHECK(memcmp(bytes, endpoint_reply_fixture, length) == 0);

    memcpy(bytes, endpoint_reply_fixture, sizeof(endpoint_reply_fixture));
    bytes[15] = 9;
    CHECK(midici_decode_endpoint(bytes, sizeof(endpoint_reply_fixture),
                                 &endpoint) == MIDICI_MALFORMED);

    CHECK(midici_decode_report(ack_fixture, sizeof(ack_fixture), &report) ==
          MIDICI_OK);
    CHECK(report.has_report && report.original_sub_id2 == 0x34u);
    CHECK(report.status_code == MIDICI_ACK_STATUS_TIMEOUT_WAIT);
    CHECK(report.status_data == 10u && report.details[0] == 1u &&
          report.details[1] == 2u);
    CHECK(report.text_length == 4u && memcmp(report.text, "wait", 4) == 0);
    CHECK(midici_encode_report(&report, bytes, sizeof(bytes), &length) ==
          MIDICI_OK);
    CHECK(length == sizeof(ack_fixture));
    CHECK(memcmp(bytes, ack_fixture, length) == 0);

    CHECK(midici_decode_report(nak_v1_fixture, sizeof(nak_v1_fixture),
                               &report) == MIDICI_OK);
    CHECK(!report.has_report);
    CHECK(midici_encode_report(&report, bytes, sizeof(bytes), &length) ==
          MIDICI_OK);
    CHECK(length == sizeof(nak_v1_fixture));
    CHECK(memcmp(bytes, nak_v1_fixture, length) == 0);

    CHECK(midici_decode_report(nak_v2_fixture, sizeof(nak_v2_fixture),
                               &report) == MIDICI_OK);
    CHECK(report.has_report && report.status_code == MIDICI_NAK_STATUS_MALFORMED);
    CHECK(report.text_length == 5u && memcmp(report.text, "bad j", 5) == 0);
    CHECK(midici_encode_report(&report, bytes, sizeof(bytes), &length) ==
          MIDICI_OK);
    CHECK(length == sizeof(nak_v2_fixture));
    CHECK(memcmp(bytes, nak_v2_fixture, length) == 0);
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
    struct midici_invalidate decoded;
    size_t length = 0;

    CHECK(midici_encode_invalidate_muid(0x01234567u, 0x07654321u, bytes,
                                        sizeof(bytes), &length) == MIDICI_OK);
    CHECK(length == sizeof(expected));
    CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
    CHECK(midici_decode_invalidate_muid(bytes, length, &decoded) == MIDICI_OK);
    CHECK(decoded.invalidated_muid == 0x07654321u);
}

static struct midici_responder_config responder_config(void)
{
    static const uint8_t product[] = "CONF-01";
    struct midici_responder_config config;

    memset(&config, 0, sizeof(config));
    config.identity.manufacturer[1] = 0x20;
    config.identity.manufacturer[2] = 0x6b;
    config.identity.family = 1;
    config.identity.model = 2;
    config.identity.revision[0] = 1;
    config.categories = 8;
    config.max_sysex_bytes = 128;
    config.product_instance_id = product;
    config.product_instance_id_length = 7;
    return config;
}

static void test_responder(void)
{
    static const uint8_t discovery_reply[] = {
        0xf0, 0x7e, 0x7f, 0x0d, 0x71, 0x02,
        0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09,
        0x00, 0x20, 0x6b, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x7f, 0xf7
    };
    static const uint8_t endpoint_reply[] = {
        0xf0, 0x7e, 0x7f, 0x0d, 0x73, 0x02,
        0x21, 0x06, 0x15, 0x3b, 0x67, 0x0a, 0x0d, 0x09,
        0x00, 0x07, 0x00, 0x43, 0x4f, 0x4e, 0x46, 0x2d, 0x30, 0x31,
        0xf7
    };
    struct midici_responder responder, unused;
    struct midici_responder_config config = responder_config();
    struct midici_responder_event event;
    struct midici_invalidate invalidate;
    uint8_t input[64], output[128];
    size_t length, output_length;
    uint32_t previous;

    CHECK(midici_responder_init(&responder, 0x07654321u) == MIDICI_OK);
    previous = responder.context.local_muid;
    CHECK(midici_responder_handle(&responder, &config, discovery_fixture,
                                  sizeof(discovery_fixture), 0x01111111u,
                                  output, 20, &output_length, &event) ==
          MIDICI_BUFFER_TOO_SMALL);
    CHECK(output_length == MIDICI_RESPONDER_MIN_RESPONSE_CAPACITY);
    CHECK(responder.context.local_muid == previous && !responder.used);
    CHECK(midici_responder_handle(&responder, &config, discovery_fixture,
                                  sizeof(discovery_fixture), 0x01111111u,
                                  output, sizeof(output), &output_length,
                                  &event) == MIDICI_OK);
    CHECK(event.type == MIDICI_RESPONDER_DISCOVERY);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY);
    CHECK(output_length == sizeof(discovery_reply));
    CHECK(memcmp(output, discovery_reply, output_length) == 0);

    memcpy(input, endpoint_inquiry_fixture, sizeof(endpoint_inquiry_fixture));
    input[5] = 0x12;
    CHECK(midici_responder_handle(&responder, &config, input,
                                  sizeof(endpoint_inquiry_fixture), 0,
                                  output, sizeof(output), &output_length,
                                  &event) == MIDICI_OK);
    CHECK(event.type == MIDICI_RESPONDER_UNSUPPORTED_VERSION);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_NAK);
    {
        struct midici_report report;
        CHECK(midici_decode_report(output, output_length, &report) == MIDICI_OK);
        CHECK(report.status_code == MIDICI_NAK_STATUS_VERSION_NOT_SUPPORTED);
    }

    CHECK(midici_responder_handle(&responder, &config,
                                  endpoint_inquiry_fixture,
                                  sizeof(endpoint_inquiry_fixture), 0,
                                  output, sizeof(output), &output_length,
                                  &event) == MIDICI_OK);
    CHECK(responder.used && event.type == MIDICI_RESPONDER_ENDPOINT_INQUIRY);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_ENDPOINT_REPLY);
    CHECK(output_length == sizeof(endpoint_reply));
    CHECK(memcmp(output, endpoint_reply, output_length) == 0);

    /* Once used, a same-source Discovery emits Invalidate and takes a MUID. */
    memcpy(input, discovery_fixture, sizeof(discovery_fixture));
    previous = responder.context.local_muid;
    CHECK(midici_muid_encode(previous, input + 6) == MIDICI_OK);
    CHECK(midici_responder_handle(&responder, &config, input,
                                  sizeof(discovery_fixture), 0x02222222u,
                                  output, sizeof(output), &output_length,
                                  &event) == MIDICI_OK);
    CHECK(event.type == MIDICI_RESPONDER_LOCAL_MUID_CHANGED);
    CHECK(event.previous_local_muid == previous);
    CHECK(event.local_muid != previous && !responder.used);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_INVALIDATE_MUID);
    CHECK(midici_decode_invalidate_muid(output, output_length, &invalidate) ==
          MIDICI_OK);
    CHECK(invalidate.invalidated_muid == previous);

    /* An unused responder takes collision option A: change and reply. */
    CHECK(midici_responder_init(&unused, 0x01234567u) == MIDICI_OK);
    CHECK(midici_responder_handle(&unused, &config, discovery_fixture,
                                  sizeof(discovery_fixture), 0x03333333u,
                                  output, sizeof(output), &output_length,
                                  &event) == MIDICI_OK);
    CHECK(event.type == MIDICI_RESPONDER_LOCAL_MUID_CHANGED);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_DISCOVERY_REPLY);
    CHECK(unused.context.local_muid != 0x01234567u);

    /* A received Invalidate for the local MUID renews it without a reply. */
    previous = unused.context.local_muid;
    CHECK(midici_encode_invalidate_muid(0x01234567u, previous, input,
                                        sizeof(input), &length) == MIDICI_OK);
    CHECK(midici_responder_handle(&unused, &config, input, length,
                                  0x04444444u, output, sizeof(output),
                                  &output_length, &event) == MIDICI_OK);
    CHECK(event.type == MIDICI_RESPONDER_LOCAL_MUID_CHANGED);
    CHECK(unused.context.local_muid != previous && output_length == 0);

    /* A structurally short directed inquiry receives a malformed NAK. */
    memcpy(input, endpoint_inquiry_fixture, 14);
    CHECK(midici_muid_encode(unused.context.local_muid, input + 10) ==
          MIDICI_OK);
    input[14] = 0xf7;
    CHECK(midici_responder_handle(&unused, &config, input, 15,
                                  0x05555555u, output, sizeof(output),
                                  &output_length, &event) == MIDICI_OK);
    CHECK(event.emitted_sub_id2 == MIDICI_SUB_ID_NAK);
    {
        struct midici_report report;
        CHECK(midici_decode_report(output, output_length, &report) == MIDICI_OK);
        CHECK(report.status_code == MIDICI_NAK_STATUS_MALFORMED);
    }
}

static void test_decoder_bounds(void)
{
    struct midici_discovery discovery;
    struct midici_endpoint_message endpoint;
    struct midici_invalidate invalidate;
    struct midici_report report;
    uint8_t bytes[80];
    uint32_t state = 0x91e10da5u;
    unsigned round, i;
    size_t length;

    for (round = 0; round < 10000u; ++round) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        length = state % (sizeof(bytes) + 1u);
        for (i = 0; i < length; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            bytes[i] = (uint8_t)state;
        }
        (void)midici_decode_discovery(bytes, length, &discovery);
        (void)midici_decode_endpoint(bytes, length, &endpoint);
        (void)midici_decode_invalidate_muid(bytes, length, &invalidate);
        (void)midici_decode_report(bytes, length, &report);
    }
}

int main(void)
{
    test_discovery();
    test_malformed();
    test_endpoint_and_reports();
    test_sysex7_ump_roundtrip();
    test_muid_and_transactions();
    test_invalidate();
    test_responder();
    test_decoder_bounds();
    printf("midici: %d failure(s)\n", failures);
    return failures != 0;
}
