#include "../prototypes/camd/identity_map.h"
#include "../prototypes/camd/legacy_identity_key.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

struct storage {
    struct CAMDIdentityRecordV1 records[8];
    size_t count;
    uint32_t next_id;
    uint32_t forced_id;
    unsigned int generate_calls;
    unsigned int commit_calls;
    int commit_enabled;
};

struct resolve_thread {
    struct CAMDIdentityMap *map;
    const struct CAMDIdentityKeyV1 *key;
    struct CAMDIdentityResolutionV1 result;
};

static int generate_id(void *context, struct CAMDEndpointIDV1 *id)
{
    struct storage *storage = context;
    uint32_t value;

    ++storage->generate_calls;
    if (storage->forced_id) {
        value = storage->forced_id;
        storage->forced_id = 0;
    } else {
        value = storage->next_id++;
    }
    memset(id, 0, sizeof(*id));
    id->word[0] = 0x43414d44u;
    id->word[3] = value;
    return 1;
}

static int commit_records(void *context,
                          const struct CAMDIdentityRecordV1 *records,
                          size_t record_count)
{
    struct storage *storage = context;

    ++storage->commit_calls;
    if (!storage->commit_enabled)
        return 0;
    assert(record_count <= sizeof(storage->records) / sizeof(storage->records[0]));
    memcpy(storage->records, records, record_count * sizeof(*records));
    storage->count = record_count;
    return 1;
}

static int same_id(const struct CAMDEndpointIDV1 *left,
                   const struct CAMDEndpointIDV1 *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static struct CAMDIdentityResolutionV1 resolve(
    struct CAMDIdentityMap *map, const struct CAMDIdentityKeyV1 *key)
{
    struct CAMDIdentityResolutionV1 result;

    memset(&result, 0, sizeof(result));
    result.Size = sizeof(result);
    result.Version = 1;
    assert(camd_identity_map_resolve(map, key, &result) ==
           CAMD_IDENTITY_MAP_OK);
    return result;
}

static void *resolve_thread_entry(void *opaque)
{
    struct resolve_thread *thread = opaque;

    memset(&thread->result, 0, sizeof(thread->result));
    thread->result.Size = sizeof(thread->result);
    thread->result.Version = 1;
    assert(camd_identity_map_resolve(thread->map, thread->key,
                                     &thread->result) ==
           CAMD_IDENTITY_MAP_OK);
    return NULL;
}

int main(void)
{
    struct CAMDIdentityMapConfigV1 config;
    struct CAMDIdentityMap *map = NULL, *reloaded = NULL;
    struct CAMDIdentityKeyV1 provider, provider_alias;
    struct CAMDIdentityKeyV1 endpoint0, endpoint0_alias, endpoint1, endpoint2;
    struct CAMDIdentityKeyV1 endpoint3;
    struct CAMDIdentityResolutionV1 first, promoted, port0, alias, port1;
    struct CAMDIdentityResolutionV1 collision, restored;
    struct CAMDIdentityRecordV1 duplicates[2];
    struct storage storage;
    struct resolve_thread threads[8];
    pthread_t thread_ids[8];
    unsigned int calls;
    size_t i, creators;

    memset(&storage, 0, sizeof(storage));
    storage.next_id = 1;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Capacity = 4;
    config.CallbackContext = &storage;
    config.Generate = generate_id;
    config.Commit = commit_records;
    assert(camd_identity_map_create(&config, &map) == CAMD_IDENTITY_MAP_OK);

    assert(camd_legacy_provider_identity_key("DEVS:Midi/DebugDriver",
                                              &provider) ==
           CAMD_IDENTITY_MAP_OK);
    assert(camd_legacy_provider_identity_key("devs:midi/debugdriver",
                                              &provider_alias) ==
           CAMD_IDENTITY_MAP_OK);
    assert(provider.Namespace == CAMD_IDENTITY_NAMESPACE_LEGACY_PROVIDER);
    assert(provider.Confidence == CAMD_IDENTITY_PATH_BOUND);
    assert(provider.ByteCount == provider_alias.ByteCount);
    assert(memcmp(provider.Bytes, provider_alias.Bytes,
                  provider.ByteCount) == 0);
    assert(camd_legacy_provider_identity_key("DEVS:Midi/", &provider_alias) ==
           CAMD_IDENTITY_MAP_INVALID);

    first = resolve(map, &provider);
    assert(!first.Persistent && !first.Existing);
    assert(first.EffectiveConfidence == CAMD_IDENTITY_EPHEMERAL);
    assert(storage.count == 0 && storage.commit_calls == 1);
    storage.commit_enabled = 1;
    promoted = resolve(map, &provider_alias);
    assert(promoted.Persistent && promoted.Existing);
    assert(promoted.EffectiveConfidence == CAMD_IDENTITY_PATH_BOUND);
    assert(same_id(&first.ID, &promoted.ID));
    assert(storage.count == 1);

    assert(camd_legacy_endpoint_identity_key("DEVS:Midi/DebugDriver", 0,
                                              &endpoint0) ==
           CAMD_IDENTITY_MAP_OK);
    assert(camd_legacy_endpoint_identity_key("debugdriver", 0,
                                              &endpoint0_alias) ==
           CAMD_IDENTITY_MAP_OK);
    assert(camd_legacy_endpoint_identity_key("debugdriver", 1,
                                              &endpoint1) ==
           CAMD_IDENTITY_MAP_OK);
    assert(endpoint0.Namespace == CAMD_IDENTITY_NAMESPACE_LEGACY_ENDPOINT);
    assert(endpoint0.ByteCount == endpoint0_alias.ByteCount);
    assert(memcmp(endpoint0.Bytes, endpoint0_alias.Bytes,
                  endpoint0.ByteCount) == 0);
    assert(endpoint0.ByteCount == endpoint1.ByteCount);
    assert(memcmp(endpoint0.Bytes, endpoint1.Bytes,
                  endpoint0.ByteCount) != 0);
    port0 = resolve(map, &endpoint0);
    alias = resolve(map, &endpoint0_alias);
    assert(port0.Persistent && !port0.Existing);
    assert(alias.Persistent && alias.Existing);
    assert(same_id(&port0.ID, &alias.ID));
    port1 = resolve(map, &endpoint1);
    assert(port1.Persistent && !same_id(&port0.ID, &port1.ID));

    assert(camd_legacy_endpoint_identity_key("debugdriver", 2,
                                              &endpoint2) ==
           CAMD_IDENTITY_MAP_OK);
    storage.forced_id = port0.ID.word[3];
    calls = storage.generate_calls;
    collision = resolve(map, &endpoint2);
    assert(collision.Persistent);
    assert(storage.generate_calls == calls + 2);
    assert(!same_id(&collision.ID, &port0.ID));
    assert(camd_legacy_endpoint_identity_key("debugdriver", 3,
                                              &endpoint3) ==
           CAMD_IDENTITY_MAP_OK);
    memset(&restored, 0, sizeof(restored));
    restored.Size = sizeof(restored);
    restored.Version = 1;
    assert(camd_identity_map_resolve(map, &endpoint3, &restored) ==
           CAMD_IDENTITY_MAP_FULL);
    camd_identity_map_destroy(map);

    config.InitialRecords = storage.records;
    config.InitialRecordCount = storage.count;
    assert(camd_identity_map_create(&config, &reloaded) ==
           CAMD_IDENTITY_MAP_OK);
    restored = resolve(reloaded, &endpoint0);
    assert(restored.Persistent && restored.Existing);
    assert(same_id(&restored.ID, &port0.ID));
    camd_identity_map_destroy(reloaded);

    duplicates[0] = storage.records[0];
    duplicates[1] = storage.records[0];
    config.InitialRecords = duplicates;
    config.InitialRecordCount = 2;
    assert(camd_identity_map_create(&config, &map) ==
           CAMD_IDENTITY_MAP_COLLISION);
    assert(map == NULL);

    memset(&storage, 0, sizeof(storage));
    storage.next_id = 100;
    storage.commit_enabled = 1;
    config.Capacity = 4;
    config.InitialRecords = NULL;
    config.InitialRecordCount = 0;
    assert(camd_identity_map_create(&config, &map) == CAMD_IDENTITY_MAP_OK);
    creators = 0;
    for (i = 0; i < sizeof(threads) / sizeof(threads[0]); ++i) {
        memset(&threads[i], 0, sizeof(threads[i]));
        threads[i].map = map;
        threads[i].key = &endpoint0;
        assert(pthread_create(&thread_ids[i], NULL, resolve_thread_entry,
                              &threads[i]) == 0);
    }
    for (i = 0; i < sizeof(threads) / sizeof(threads[0]); ++i) {
        assert(pthread_join(thread_ids[i], NULL) == 0);
        assert(threads[i].result.Persistent);
        assert(same_id(&threads[0].result.ID, &threads[i].result.ID));
        if (!threads[i].result.Existing)
            ++creators;
    }
    assert(creators == 1);
    assert(storage.generate_calls == 1 && storage.commit_calls == 1);
    camd_identity_map_destroy(map);
    puts("CAMD identity map OK");
    return 0;
}
