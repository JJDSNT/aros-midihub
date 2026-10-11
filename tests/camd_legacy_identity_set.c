#include "../prototypes/camd/legacy_identity_set.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct storage {
    uint32_t next_id;
    unsigned int commit_calls;
    int commit_enabled;
};

static int generate(void *context, struct CAMDEndpointIDV1 *id)
{
    struct storage *storage = context;

    memset(id, 0, sizeof(*id));
    id->word[0] = 0x43414d44u;
    id->word[3] = ++storage->next_id;
    return 1;
}

static int commit(void *context,
                  const struct CAMDIdentityRecordV1 *records,
                  size_t record_count)
{
    struct storage *storage = context;

    (void)records;
    (void)record_count;
    ++storage->commit_calls;
    return storage->commit_enabled;
}

static struct CAMDIdentityMap *create_map(struct storage *storage,
                                          uint32_t capacity)
{
    struct CAMDIdentityMapConfigV1 config;
    struct CAMDIdentityMap *map = NULL;

    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Version = 1;
    config.Capacity = capacity;
    config.CallbackContext = storage;
    config.Generate = generate;
    config.Commit = commit;
    assert(camd_identity_map_create(&config, &map) == CAMD_IDENTITY_MAP_OK);
    return map;
}

static int same_id(const struct CAMDEndpointIDV1 *left,
                   const struct CAMDEndpointIDV1 *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int zero_id(const struct CAMDEndpointIDV1 *id)
{
    static const struct CAMDEndpointIDV1 zero;

    return same_id(id, &zero);
}

int main(void)
{
    struct storage storage;
    struct CAMDIdentityMap *map;
    struct CAMDEndpointIDV1 provider, provider_again;
    struct CAMDEndpointIDV1 endpoints[3], endpoints_again[3];
    uint32_t identity_kind, identity_kind_again;
    size_t i;

    memset(&storage, 0, sizeof(storage));
    storage.commit_enabled = 1;
    map = create_map(&storage, 4);
    assert(camd_legacy_identity_set_resolve(
               map, "DEVS:Midi/DebugDriver", 3, &provider, endpoints,
               &identity_kind) == CAMD_IDENTITY_MAP_OK);
    assert(identity_kind == CAMD_IDENTITY_PATH_BOUND);
    assert(!zero_id(&provider));
    for (i = 0; i < 3; ++i) {
        assert(!zero_id(&endpoints[i]));
        assert(!same_id(&provider, &endpoints[i]));
        if (i != 0)
            assert(!same_id(&endpoints[i - 1], &endpoints[i]));
    }
    assert(camd_legacy_identity_set_resolve(
               map, "debugdriver", 3, &provider_again, endpoints_again,
               &identity_kind_again) == CAMD_IDENTITY_MAP_OK);
    assert(identity_kind_again == CAMD_IDENTITY_PATH_BOUND);
    assert(same_id(&provider, &provider_again));
    assert(memcmp(endpoints, endpoints_again, sizeof(endpoints)) == 0);
    camd_identity_map_destroy(map);

    memset(&storage, 0, sizeof(storage));
    map = create_map(&storage, 3);
    memset(&provider, 0xff, sizeof(provider));
    memset(endpoints, 0xff, sizeof(endpoints));
    identity_kind = 99;
    assert(camd_legacy_identity_set_resolve(
               map, "debugdriver", 3, &provider, endpoints,
               &identity_kind) == CAMD_IDENTITY_MAP_FULL);
    assert(zero_id(&provider) && identity_kind == 0);
    for (i = 0; i < 3; ++i)
        assert(zero_id(&endpoints[i]));
    camd_identity_map_destroy(map);

    memset(&storage, 0, sizeof(storage));
    map = create_map(&storage, 2);
    assert(camd_legacy_identity_set_resolve(
               map, "debugdriver", 1, &provider, endpoints,
               &identity_kind) == CAMD_IDENTITY_MAP_OK);
    assert(identity_kind == CAMD_IDENTITY_EPHEMERAL);
    storage.commit_enabled = 1;
    assert(camd_legacy_identity_set_resolve(
               map, "DEBUGDRIVER", 1, &provider_again, endpoints_again,
               &identity_kind_again) == CAMD_IDENTITY_MAP_OK);
    assert(identity_kind_again == CAMD_IDENTITY_PATH_BOUND);
    assert(same_id(&provider, &provider_again));
    assert(same_id(&endpoints[0], &endpoints_again[0]));
    camd_identity_map_destroy(map);

    assert(camd_legacy_identity_set_resolve(
               NULL, "debugdriver", 1, &provider, endpoints,
               &identity_kind) == CAMD_IDENTITY_MAP_INVALID);
    assert(camd_legacy_identity_set_resolve(
               NULL, "debugdriver", 0, &provider, endpoints,
               &identity_kind) == CAMD_IDENTITY_MAP_INVALID);
    puts("CAMD legacy identity set OK");
    return 0;
}
