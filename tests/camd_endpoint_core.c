#include "../prototypes/camd/endpoint_registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct CAMDEndpointIDV1 make_id(uint32_t value)
{
    struct CAMDEndpointIDV1 id = { { 0x43414d44u, 0, 0, value } };
    return id;
}

static struct CAMDEndpointInfoV1 make_endpoint(uint32_t value,
                                               const char *name)
{
    struct CAMDEndpointInfoV1 endpoint;

    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.Size = sizeof(endpoint);
    endpoint.Version = 1;
    endpoint.ID = make_id(value);
    endpoint.ProviderID = make_id(0x1000u);
    endpoint.State = CAMD_ENDPOINT_REGISTERED;
    endpoint.IdentityKind = 1;
    endpoint.NativeDataFormat = 2;
    strncpy(endpoint.Name, name, sizeof(endpoint.Name) - 1);
    strncpy(endpoint.ProductInstance, "test-instance",
            sizeof(endpoint.ProductInstance) - 1);
    strncpy(endpoint.Transport, "software", sizeof(endpoint.Transport) - 1);
    return endpoint;
}

static struct CAMDGroupInfoV1 make_group(
    const struct CAMDEndpointInfoV1 *endpoint, uint32_t group)
{
    struct CAMDGroupInfoV1 info;

    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    info.Version = 1;
    info.EndpointID = endpoint->ID;
    info.Group = group;
    strcpy(info.Name, "Group");
    return info;
}

static struct CAMDFunctionBlockInfoV1 make_block(
    const struct CAMDEndpointInfoV1 *endpoint, uint32_t number,
    uint32_t first_group, uint32_t group_count)
{
    struct CAMDFunctionBlockInfoV1 info;

    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    info.Version = 1;
    info.EndpointID = endpoint->ID;
    info.Number = number;
    info.FirstGroup = first_group;
    info.GroupCount = group_count;
    strcpy(info.Name, "Function Block");
    return info;
}

static const struct CAMDEndpointInfoV1 *snapshot_first(
    const struct CAMDEndpointSnapshot *snapshot)
{
    const struct CAMDEndpointInfoV1 *endpoint;
    const struct CAMDGroupInfoV1 *groups;
    const struct CAMDFunctionBlockInfoV1 *blocks;
    size_t group_count, block_count;

    assert(camd_snapshot_endpoint(snapshot, 0, &endpoint, &groups,
                                  &group_count, &blocks,
                                  &block_count) == CAMD_REGISTRY_OK);
    assert(group_count == 1);
    assert(block_count == 1);
    assert(groups[0].Group == 0);
    assert(blocks[0].FirstGroup == 0);
    return endpoint;
}

int main(void)
{
    struct CAMDEndpointRegistry *registry = camd_registry_create();
    struct CAMDEndpointSnapshot *before, *after;
    struct CAMDEndpointInfoV1 endpoint = make_endpoint(1, "Original");
    struct CAMDGroupInfoV1 group = make_group(&endpoint, 0);
    struct CAMDFunctionBlockInfoV1 block = make_block(&endpoint, 0, 0, 1);
    struct CAMDHandleV1 provider, client, replacement;
    struct CAMDGenerationV1 generation;
    const struct CAMDEndpointInfoV1 *copy;

    assert(registry != NULL);
    assert(sizeof(struct CAMDHandleV1) == 8);
    assert(sizeof(struct CAMDEndpointInfoV1) == 360);
    assert(sizeof(struct CAMDGroupInfoV1) == 100);
    assert(sizeof(struct CAMDFunctionBlockInfoV1) == 104);

    assert(camd_registry_publish(registry, &endpoint, &group, 1, &block, 1,
                                 &provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_publish(registry, &endpoint, &group, 1, &block, 1,
                                 &replacement) == CAMD_REGISTRY_DUPLICATE);
    assert(camd_registry_snapshot(registry, &before) == CAMD_REGISTRY_OK);
    generation = camd_snapshot_generation(before);
    copy = snapshot_first(before);
    assert(strcmp(copy->Name, "Original") == 0);

    endpoint = *copy;
    strcpy(endpoint.Name, "Updated");
    group = make_group(&endpoint, 0);
    block = make_block(&endpoint, 0, 0, 1);
    assert(camd_registry_replace(registry, provider, &endpoint, &group, 1,
                                 &block, 1) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(before)->Name, "Original") == 0);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(after)->Name, "Updated") == 0);
    assert(camd_snapshot_generation(after).low > generation.low);
    generation = camd_snapshot_generation(after);
    camd_snapshot_destroy(after);

    block.GroupCount = 17;
    assert(camd_registry_replace(registry, provider, &endpoint, &group, 1,
                                 &block, 1) == CAMD_REGISTRY_INVALID);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(strcmp(snapshot_first(after)->Name, "Updated") == 0);
    assert(camd_snapshot_generation(after).high == generation.high);
    assert(camd_snapshot_generation(after).low == generation.low);
    camd_snapshot_destroy(after);

    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_STATE);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_DISCOVERING) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_set_state(registry, provider,
                                   CAMD_ENDPOINT_AVAILABLE) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_acquire(registry, &endpoint.ID, &client) ==
           CAMD_REGISTRY_OK);
    assert(camd_registry_retire(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_acquire(registry, &endpoint.ID, &replacement) ==
           CAMD_REGISTRY_RETIRED);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_OK);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(after) == 1);
    assert(snapshot_first(after)->State == CAMD_ENDPOINT_RETIRING);
    camd_snapshot_destroy(after);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, client) == CAMD_REGISTRY_STALE);
    assert(camd_registry_snapshot(registry, &after) == CAMD_REGISTRY_OK);
    assert(camd_snapshot_endpoint_count(after) == 0);
    camd_snapshot_destroy(after);

    endpoint = make_endpoint(2, "Replacement");
    group = make_group(&endpoint, 0);
    block = make_block(&endpoint, 0, 0, 1);
    assert(camd_registry_publish(registry, &endpoint, &group, 1, &block, 1,
                                 &replacement) == CAMD_REGISTRY_OK);
    assert(replacement.slot == provider.slot);
    assert(replacement.generation != provider.generation);
    assert(camd_registry_release(registry, provider) == CAMD_REGISTRY_STALE);

    assert(camd_registry_retire(registry, replacement) == CAMD_REGISTRY_OK);
    assert(camd_registry_release(registry, replacement) == CAMD_REGISTRY_OK);
    camd_snapshot_destroy(before);
    camd_registry_destroy(registry);
    puts("CAMD private endpoint core OK");
    return 0;
}
