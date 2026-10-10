#include "../prototypes/camd/identity_file.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct CAMDIdentityRecordV1 make_record(uint32_t number)
{
    struct CAMDIdentityRecordV1 record;

    memset(&record, 0, sizeof(record));
    record.Size = sizeof(record);
    record.Version = 1;
    record.Namespace = 0x1000u + number;
    record.KeyByteCount = 3;
    record.Key[0] = 'k';
    record.Key[1] = (uint8_t)(number >> 8);
    record.Key[2] = (uint8_t)number;
    record.ID.word[0] = 0x43414d44u;
    record.ID.word[3] = number;
    return record;
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

int main(void)
{
    struct CAMDIdentityRecordV1 source[2], decoded[2], duplicate[2];
    uint8_t bytes[CAMD_IDENTITY_FILE_HEADER_BYTES +
                  2 * CAMD_IDENTITY_FILE_RECORD_BYTES];
    uint8_t corrupt[sizeof(bytes)];
    uint8_t extended[sizeof(bytes) + 10];
    size_t byte_count, record_count, i;

    source[0] = make_record(1);
    source[1] = make_record(2);
    assert(camd_identity_file_size(2) == sizeof(bytes));
    assert(camd_identity_file_encode(source, 2, bytes, sizeof(bytes),
                                     &byte_count) == CAMD_IDENTITY_FILE_OK);
    assert(byte_count == sizeof(bytes));
    assert(memcmp(bytes, "FORM", 4) == 0);
    assert(memcmp(bytes + 8, "CAMDIDMP", 8) == 0);
    assert(camd_identity_file_decode(bytes, byte_count, decoded, 2,
                                     &record_count) == CAMD_IDENTITY_FILE_OK);
    assert(record_count == 2);
    assert(memcmp(source, decoded, sizeof(source)) == 0);

    memcpy(extended, bytes, 12);
    memcpy(extended + 12, "NOTE", 4);
    write_u32(extended + 16, 1);
    extended[20] = 0x42;
    extended[21] = 0;
    memcpy(extended + 22, bytes + 12, sizeof(bytes) - 12);
    write_u32(extended + 4, (uint32_t)(sizeof(extended) - 8));
    assert(camd_identity_file_decode(extended, sizeof(extended), decoded, 2,
                                     &record_count) == CAMD_IDENTITY_FILE_OK);
    assert(record_count == 2 && memcmp(source, decoded, sizeof(source)) == 0);

    for (i = 0; i < byte_count; ++i) {
        record_count = 99;
        assert(camd_identity_file_decode(bytes, i, decoded, 2,
                                         &record_count) ==
               CAMD_IDENTITY_FILE_INVALID);
        assert(record_count == 0);
    }
    assert(camd_identity_file_decode(bytes, byte_count, decoded, 1,
                                     &record_count) ==
           CAMD_IDENTITY_FILE_CAPACITY);

    memcpy(corrupt, bytes, sizeof(bytes));
    corrupt[16] ^= 1;
    assert(camd_identity_file_decode(corrupt, sizeof(corrupt), decoded, 2,
                                     &record_count) ==
           CAMD_IDENTITY_FILE_INVALID);
    memcpy(corrupt, bytes, sizeof(bytes));
    corrupt[CAMD_IDENTITY_FILE_HEADER_BYTES + 8] ^= 1;
    assert(camd_identity_file_decode(corrupt, sizeof(corrupt), decoded, 2,
                                     &record_count) ==
           CAMD_IDENTITY_FILE_INVALID);

    duplicate[0] = source[0];
    duplicate[1] = source[0];
    duplicate[1].ID.word[3] = 3;
    assert(camd_identity_file_encode(duplicate, 2, bytes, sizeof(bytes),
                                     &byte_count) ==
           CAMD_IDENTITY_FILE_COLLISION);
    duplicate[1] = source[1];
    duplicate[1].ID = source[0].ID;
    assert(camd_identity_file_encode(duplicate, 2, bytes, sizeof(bytes),
                                     &byte_count) ==
           CAMD_IDENTITY_FILE_COLLISION);

    source[0].KeyByteCount = 0;
    assert(camd_identity_file_encode(source, 2, bytes, sizeof(bytes),
                                     &byte_count) ==
           CAMD_IDENTITY_FILE_INVALID);
    puts("CAMD identity file OK");
    return 0;
}
