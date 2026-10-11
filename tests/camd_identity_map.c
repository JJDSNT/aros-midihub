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
    {
        struct storage temp_state;
        struct CAMDIdentityMapConfigV1 temp_config;
        struct CAMDIdentityMap *temp_map = NULL;
        struct CAMDEndpointIDV1 one, two;
        static const struct CAMDEndpointIDV1 zero;

        memset(&temp_state, 0, sizeof(temp_state));
        temp_state.next_id = 1;
        memset(&temp_config, 0, sizeof(temp_config));
        temp_config.Size = sizeof(temp_config);
        temp_config.Version = 1;
        temp_config.Capacity = 1;
        temp_config.CallbackContext = &temp_state;
        temp_config.Generate = generate_id;
        temp_config.Commit = commit_records;
        assert(camd_identity_map_create(&temp_config, &temp_map) ==
               CAMD_IDENTITY_MAP_OK);
        /* Temporary IDs need no room and are not committed. */
        assert(camd_identity_map_temporary(temp_map, &one) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_identity_map_temporary(temp_map, &two) ==
               CAMD_IDENTITY_MAP_OK);
        assert(memcmp(&one, &zero, sizeof(one)) != 0 &&
               memcmp(&one, &two, sizeof(one)) != 0);
        assert(temp_state.commit_calls == 0);
        assert(camd_identity_map_temporary(NULL, &one) ==
               CAMD_IDENTITY_MAP_INVALID);
        camd_identity_map_destroy(temp_map);
    }
    {
        /* Listing and forgetting: a forgotten key gets a new ID, storage
         * loses the mapping first, and a failed write leaves it. */
        struct storage state;
        struct CAMDIdentityMapConfigV1 list_config;
        struct CAMDIdentityMap *list_map = NULL;
        struct CAMDIdentityKeyV1 key_a, key_b;
        struct CAMDIdentityResolutionV1 res_a, res_b, res_again;
        struct CAMDIdentityRecordV1 record;
        struct CAMDEndpointIDV1 nobody;
        uint32_t persistent = 9;

        memset(&state, 0, sizeof(state));
        state.next_id = 100;
        state.commit_enabled = 1;
        memset(&list_config, 0, sizeof(list_config));
        list_config.Size = sizeof(list_config);
        list_config.Version = 1;
        list_config.Capacity = 4;
        list_config.CallbackContext = &state;
        list_config.Generate = generate_id;
        list_config.Commit = commit_records;
        assert(camd_identity_map_create(&list_config, &list_map) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_identity_map_count(list_map) == 0 &&
               camd_identity_map_capacity(list_map) == 4);
        assert(camd_legacy_endpoint_identity_key("DEVS:Midi/a", 0, &key_a) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_legacy_endpoint_identity_key("DEVS:Midi/b", 0, &key_b) ==
               CAMD_IDENTITY_MAP_OK);
        memset(&res_a, 0, sizeof(res_a));
        res_a.Size = sizeof(res_a);
        res_a.Version = 1;
        res_b = res_again = res_a;
        assert(camd_identity_map_resolve(list_map, &key_a, &res_a) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_identity_map_resolve(list_map, &key_b, &res_b) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_identity_map_count(list_map) == 2 && state.count == 2);
        assert(camd_identity_map_get(list_map, 0, &record, &persistent) ==
               CAMD_IDENTITY_MAP_OK && persistent == 1 &&
               same_id(&record.ID, &res_a.ID));
        assert(camd_identity_map_get(list_map, 2, &record, NULL) ==
               CAMD_IDENTITY_MAP_MISSING);

        memset(&nobody, 0, sizeof(nobody));
        assert(camd_identity_map_remove(list_map, &nobody) ==
               CAMD_IDENTITY_MAP_INVALID);
        nobody.word[0] = 77;
        assert(camd_identity_map_remove(list_map, &nobody) ==
               CAMD_IDENTITY_MAP_MISSING);

        /* storage refuses: nothing changes */
        state.commit_enabled = 0;
        assert(camd_identity_map_remove(list_map, &res_a.ID) ==
               CAMD_IDENTITY_MAP_STORAGE);
        assert(camd_identity_map_count(list_map) == 2 && state.count == 2);
        assert(camd_identity_map_get(list_map, 0, &record, NULL) ==
               CAMD_IDENTITY_MAP_OK && same_id(&record.ID, &res_a.ID));
        assert(camd_identity_map_resolve(list_map, &key_a, &res_again) ==
               CAMD_IDENTITY_MAP_OK && same_id(&res_again.ID, &res_a.ID));

        state.commit_enabled = 1;
        assert(camd_identity_map_remove(list_map, &res_a.ID) ==
               CAMD_IDENTITY_MAP_OK);
        assert(camd_identity_map_count(list_map) == 1 && state.count == 1 &&
               same_id(&state.records[0].ID, &res_b.ID));
        assert(camd_identity_map_get(list_map, 0, &record, NULL) ==
               CAMD_IDENTITY_MAP_OK && same_id(&record.ID, &res_b.ID));
        assert(camd_identity_map_resolve(list_map, &key_a, &res_again) ==
               CAMD_IDENTITY_MAP_OK && res_again.Existing == 0 &&
               !same_id(&res_again.ID, &res_a.ID));
        assert(camd_identity_map_resolve(list_map, &key_b, &res_again) ==
               CAMD_IDENTITY_MAP_OK && same_id(&res_again.ID, &res_b.ID));
        camd_identity_map_destroy(list_map);
    }
    puts("CAMD identity map OK");
    return 0;
}
