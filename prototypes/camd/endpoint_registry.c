#include "endpoint_registry.h"

#include <string.h>

#ifdef __AROS__
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>

typedef struct SignalSemaphore core_lock_t;

static void *core_alloc(size_t size, int clear)
{
    return AllocVec(size, MEMF_ANY | (clear ? MEMF_CLEAR : 0));
}

static void core_free(void *memory)
{
    FreeVec(memory);
}
#else
#include <pthread.h>
#include <stdlib.h>

typedef pthread_mutex_t core_lock_t;

static void *core_alloc(size_t size, int clear)
{
    return clear ? calloc(1, size) : malloc(size);
}

static void core_free(void *memory)
{
    free(memory);
}
#endif

struct endpoint_slot {
    uint32_t generation;
    uint32_t references;
    int occupied;
    struct CAMDEndpointInfoV1 endpoint;
    struct CAMDGroupInfoV1 *groups;
    size_t group_count;
    struct CAMDFunctionBlockInfoV1 *blocks;
    size_t block_count;
};

struct CAMDEndpointRegistry {
    core_lock_t lock;
    struct endpoint_slot *slots;
    size_t slot_count;
    struct CAMDGenerationV1 generation;
};

#ifdef __AROS__
static int core_lock_init(core_lock_t *lock)
{
    InitSemaphore(lock);
    return 1;
}

static void core_lock_destroy(core_lock_t *lock)
{
    (void)lock;
}

static void core_lock_acquire(core_lock_t *lock)
{
    ObtainSemaphore(lock);
}

static void core_lock_release(core_lock_t *lock)
{
    ReleaseSemaphore(lock);
}
#else
static int core_lock_init(core_lock_t *lock)
{
    return pthread_mutex_init(lock, NULL) == 0;
}

static void core_lock_destroy(core_lock_t *lock)
{
    pthread_mutex_destroy(lock);
}

static void core_lock_acquire(core_lock_t *lock)
{
    pthread_mutex_lock(lock);
}

static void core_lock_release(core_lock_t *lock)
{
    pthread_mutex_unlock(lock);
}
#endif

struct snapshot_endpoint {
    struct CAMDEndpointInfoV1 endpoint;
    struct CAMDGroupInfoV1 *groups;
    size_t group_count;
    struct CAMDFunctionBlockInfoV1 *blocks;
    size_t block_count;
};

struct CAMDEndpointSnapshot {
    struct CAMDGenerationV1 generation;
    struct snapshot_endpoint *endpoints;
    size_t endpoint_count;
};

typedef char endpoint_size_must_be_360[
    sizeof(struct CAMDEndpointInfoV1) == 360 ? 1 : -1];
typedef char group_size_must_be_100[
    sizeof(struct CAMDGroupInfoV1) == 100 ? 1 : -1];
typedef char block_size_must_be_104[
    sizeof(struct CAMDFunctionBlockInfoV1) == 104 ? 1 : -1];
typedef char handle_size_must_be_8[
    sizeof(struct CAMDHandleV1) == 8 ? 1 : -1];

static int id_is_zero(const struct CAMDEndpointIDV1 *id)
{
    return !(id->word[0] | id->word[1] | id->word[2] | id->word[3]);
}

static int id_equal(const struct CAMDEndpointIDV1 *a,
                    const struct CAMDEndpointIDV1 *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void advance_generation(struct CAMDEndpointRegistry *registry)
{
    if (++registry->generation.low == 0)
        ++registry->generation.high;
}

static int generation_available(const struct CAMDEndpointRegistry *registry)
{
    return registry->generation.high != UINT32_MAX ||
           registry->generation.low != UINT32_MAX;
}

static void free_topology(struct endpoint_slot *slot)
{
    core_free(slot->groups);
    core_free(slot->blocks);
    slot->groups = NULL;
    slot->blocks = NULL;
    slot->group_count = 0;
    slot->block_count = 0;
}

static enum CAMDRegistryResult validate_endpoint(
    const struct CAMDEndpointInfoV1 *endpoint)
{
    if (!endpoint || endpoint->Size != sizeof(*endpoint) ||
        endpoint->Version != 1 || id_is_zero(&endpoint->ID) ||
        id_is_zero(&endpoint->ProviderID) ||
        endpoint->State < CAMD_ENDPOINT_REGISTERED ||
        endpoint->State > CAMD_ENDPOINT_OFFLINE)
        return CAMD_REGISTRY_INVALID;
    if (!memchr(endpoint->Name, '\0', sizeof(endpoint->Name)) ||
        !memchr(endpoint->ProductInstance, '\0',
                sizeof(endpoint->ProductInstance)) ||
        !memchr(endpoint->Transport, '\0', sizeof(endpoint->Transport)))
        return CAMD_REGISTRY_INVALID;
    return CAMD_REGISTRY_OK;
}

static enum CAMDRegistryResult copy_topology(
    const struct CAMDEndpointInfoV1 *endpoint,
    const struct CAMDGroupInfoV1 *groups,
    size_t group_count,
    const struct CAMDFunctionBlockInfoV1 *blocks,
    size_t block_count,
    struct CAMDGroupInfoV1 **group_copy,
    struct CAMDFunctionBlockInfoV1 **block_copy)
{
    size_t i, j;

    *group_copy = NULL;
    *block_copy = NULL;
    if ((group_count && !groups) || (block_count && !blocks))
        return CAMD_REGISTRY_INVALID;
    if (group_count > SIZE_MAX / sizeof(*groups) ||
        block_count > SIZE_MAX / sizeof(*blocks))
        return CAMD_REGISTRY_RANGE;

    for (i = 0; i < group_count; ++i) {
        if (groups[i].Size != sizeof(groups[i]) || groups[i].Version != 1 ||
            groups[i].Group > 15 ||
            !id_equal(&groups[i].EndpointID, &endpoint->ID) ||
            !memchr(groups[i].Name, '\0', sizeof(groups[i].Name)))
            return CAMD_REGISTRY_INVALID;
        for (j = 0; j < i; ++j)
            if (groups[j].Group == groups[i].Group)
                return CAMD_REGISTRY_DUPLICATE;
    }

    for (i = 0; i < block_count; ++i) {
        if (blocks[i].Size != sizeof(blocks[i]) || blocks[i].Version != 1 ||
            blocks[i].FirstGroup > 15 || blocks[i].GroupCount == 0 ||
            blocks[i].GroupCount > 16 - blocks[i].FirstGroup ||
            !id_equal(&blocks[i].EndpointID, &endpoint->ID) ||
            !memchr(blocks[i].Name, '\0', sizeof(blocks[i].Name)))
            return CAMD_REGISTRY_INVALID;
        for (j = 0; j < i; ++j)
            if (blocks[j].Number == blocks[i].Number)
                return CAMD_REGISTRY_DUPLICATE;
    }

    if (group_count) {
        *group_copy = core_alloc(group_count * sizeof(**group_copy), 0);
        if (!*group_copy)
            return CAMD_REGISTRY_NOMEM;
        memcpy(*group_copy, groups, group_count * sizeof(**group_copy));
    }
    if (block_count) {
        *block_copy = core_alloc(block_count * sizeof(**block_copy), 0);
        if (!*block_copy) {
            core_free(*group_copy);
            *group_copy = NULL;
            return CAMD_REGISTRY_NOMEM;
        }
        memcpy(*block_copy, blocks, block_count * sizeof(**block_copy));
    }
    return CAMD_REGISTRY_OK;
}

static struct endpoint_slot *resolve(struct CAMDEndpointRegistry *registry,
                                     struct CAMDHandleV1 handle)
{
    struct endpoint_slot *slot;

    if (!registry || handle.slot == 0 || handle.slot > registry->slot_count)
        return NULL;
    slot = &registry->slots[handle.slot - 1];
    if (!slot->occupied || slot->generation != handle.generation)
        return NULL;
    return slot;
}

static int transition_allowed(uint32_t from, uint32_t to)
{
    if (to == CAMD_ENDPOINT_RETIRING)
        return from != CAMD_ENDPOINT_RETIRING && from != CAMD_ENDPOINT_RETIRED;
    switch (from) {
    case CAMD_ENDPOINT_REGISTERED:
        return to == CAMD_ENDPOINT_DISCOVERING || to == CAMD_ENDPOINT_OFFLINE;
    case CAMD_ENDPOINT_DISCOVERING:
        return to == CAMD_ENDPOINT_AVAILABLE || to == CAMD_ENDPOINT_OFFLINE;
    case CAMD_ENDPOINT_AVAILABLE:
        return to == CAMD_ENDPOINT_OFFLINE;
    case CAMD_ENDPOINT_OFFLINE:
        return to == CAMD_ENDPOINT_DISCOVERING || to == CAMD_ENDPOINT_AVAILABLE;
    default:
        return 0;
    }
}

struct CAMDEndpointRegistry *camd_registry_create(void)
{
    struct CAMDEndpointRegistry *registry =
        core_alloc(sizeof(struct CAMDEndpointRegistry), 1);

    if (registry && !core_lock_init(&registry->lock)) {
        core_free(registry);
        registry = NULL;
    }
    return registry;
}

void camd_registry_destroy(struct CAMDEndpointRegistry *registry)
{
    size_t i;

    if (!registry)
        return;
    for (i = 0; i < registry->slot_count; ++i)
        free_topology(&registry->slots[i]);
    core_free(registry->slots);
    core_lock_destroy(&registry->lock);
    core_free(registry);
}

enum CAMDRegistryResult camd_registry_publish(
    struct CAMDEndpointRegistry *registry,
    const struct CAMDEndpointInfoV1 *endpoint,
    const struct CAMDGroupInfoV1 *groups,
    size_t group_count,
    const struct CAMDFunctionBlockInfoV1 *blocks,
    size_t block_count,
    struct CAMDHandleV1 *provider_lease)
{
    struct CAMDGroupInfoV1 *group_copy;
    struct CAMDFunctionBlockInfoV1 *block_copy;
    struct endpoint_slot *slot = NULL;
    struct endpoint_slot *grown;
    enum CAMDRegistryResult result;
    size_t i, index = 0;

    if (!registry || !provider_lease)
        return CAMD_REGISTRY_INVALID;
    result = validate_endpoint(endpoint);
    if (result != CAMD_REGISTRY_OK)
        return result;
    result = copy_topology(endpoint, groups, group_count, blocks, block_count,
                           &group_copy, &block_copy);
    if (result != CAMD_REGISTRY_OK)
        return result;
    core_lock_acquire(&registry->lock);
    if (!generation_available(registry)) {
        result = CAMD_REGISTRY_RANGE;
        goto out;
    }
    for (i = 0; i < registry->slot_count; ++i) {
        if (registry->slots[i].occupied &&
            id_equal(&registry->slots[i].endpoint.ID, &endpoint->ID)) {
            result = CAMD_REGISTRY_DUPLICATE;
            goto out;
        }
        /* A generation that reached UINT32_MAX is quarantined forever. */
        if (!slot && !registry->slots[i].occupied &&
            registry->slots[i].generation != UINT32_MAX) {
            slot = &registry->slots[i];
            index = i;
        }
    }
    if (!slot) {
        if (registry->slot_count == UINT32_MAX ||
            registry->slot_count >= SIZE_MAX / sizeof(*grown)) {
            result = CAMD_REGISTRY_RANGE;
            goto out;
        }
        index = registry->slot_count;
        grown = core_alloc((registry->slot_count + 1) * sizeof(*grown), 1);
        if (!grown) {
            result = CAMD_REGISTRY_NOMEM;
            goto out;
        }
        if (registry->slot_count)
            memcpy(grown, registry->slots,
                   registry->slot_count * sizeof(*grown));
        core_free(registry->slots);
        registry->slots = grown;
        slot = &registry->slots[index];
        memset(slot, 0, sizeof(*slot));
        ++registry->slot_count;
    }
    ++slot->generation;
    slot->occupied = 1;
    slot->references = 1;
    slot->endpoint = *endpoint;
    slot->groups = group_copy;
    slot->group_count = group_count;
    slot->blocks = block_copy;
    slot->block_count = block_count;
    advance_generation(registry);
    slot->endpoint.Generation = registry->generation;
    provider_lease->slot = (uint32_t)(index + 1);
    provider_lease->generation = slot->generation;
    group_copy = NULL;
    block_copy = NULL;
    result = CAMD_REGISTRY_OK;
out:
    core_lock_release(&registry->lock);
    core_free(group_copy);
    core_free(block_copy);
    return result;
}

enum CAMDRegistryResult camd_registry_replace(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease,
    const struct CAMDEndpointInfoV1 *endpoint,
    const struct CAMDGroupInfoV1 *groups,
    size_t group_count,
    const struct CAMDFunctionBlockInfoV1 *blocks,
    size_t block_count)
{
    struct endpoint_slot *slot;
    struct CAMDGroupInfoV1 *group_copy;
    struct CAMDFunctionBlockInfoV1 *block_copy;
    enum CAMDRegistryResult result;

    if (!registry)
        return CAMD_REGISTRY_INVALID;
    result = validate_endpoint(endpoint);
    if (result != CAMD_REGISTRY_OK)
        return result;
    result = copy_topology(endpoint, groups, group_count, blocks, block_count,
                           &group_copy, &block_copy);
    if (result != CAMD_REGISTRY_OK)
        return result;
    core_lock_acquire(&registry->lock);
    slot = resolve(registry, provider_lease);
    if (!slot) {
        result = CAMD_REGISTRY_STALE;
        goto out;
    }
    if (slot->endpoint.State == CAMD_ENDPOINT_RETIRING) {
        result = CAMD_REGISTRY_RETIRED;
        goto out;
    }
    if (!generation_available(registry)) {
        result = CAMD_REGISTRY_RANGE;
        goto out;
    }
    if (!id_equal(&slot->endpoint.ID, &endpoint->ID) ||
        !id_equal(&slot->endpoint.ProviderID, &endpoint->ProviderID) ||
        slot->endpoint.State != endpoint->State) {
        result = CAMD_REGISTRY_INVALID;
        goto out;
    }
    free_topology(slot);
    slot->endpoint = *endpoint;
    slot->groups = group_copy;
    slot->group_count = group_count;
    slot->blocks = block_copy;
    slot->block_count = block_count;
    advance_generation(registry);
    slot->endpoint.Generation = registry->generation;
    group_copy = NULL;
    block_copy = NULL;
    result = CAMD_REGISTRY_OK;
out:
    core_lock_release(&registry->lock);
    core_free(group_copy);
    core_free(block_copy);
    return result;
}

enum CAMDRegistryResult camd_registry_set_state(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease,
    uint32_t state)
{
    struct endpoint_slot *slot;
    enum CAMDRegistryResult result;

    if (!registry)
        return CAMD_REGISTRY_INVALID;
    core_lock_acquire(&registry->lock);
    slot = resolve(registry, provider_lease);
    if (!slot)
        result = CAMD_REGISTRY_STALE;
    else if (!transition_allowed(slot->endpoint.State, state))
        result = CAMD_REGISTRY_STATE;
    else if (!generation_available(registry))
        result = CAMD_REGISTRY_RANGE;
    else {
        slot->endpoint.State = state;
        advance_generation(registry);
        slot->endpoint.Generation = registry->generation;
        result = CAMD_REGISTRY_OK;
    }
    core_lock_release(&registry->lock);
    return result;
}

enum CAMDRegistryResult camd_registry_acquire(
    struct CAMDEndpointRegistry *registry,
    const struct CAMDEndpointIDV1 *id,
    struct CAMDHandleV1 *lease)
{
    size_t i;
    enum CAMDRegistryResult result = CAMD_REGISTRY_INVALID;

    if (!registry || !id || !lease || id_is_zero(id))
        return CAMD_REGISTRY_INVALID;
    core_lock_acquire(&registry->lock);
    for (i = 0; i < registry->slot_count; ++i) {
        struct endpoint_slot *slot = &registry->slots[i];
        if (slot->occupied && id_equal(&slot->endpoint.ID, id)) {
            if (slot->endpoint.State == CAMD_ENDPOINT_RETIRING)
                result = CAMD_REGISTRY_RETIRED;
            else if (slot->references == UINT32_MAX)
                result = CAMD_REGISTRY_RANGE;
            else {
                ++slot->references;
                lease->slot = (uint32_t)(i + 1);
                lease->generation = slot->generation;
                result = CAMD_REGISTRY_OK;
            }
            break;
        }
    }
    core_lock_release(&registry->lock);
    return result;
}

enum CAMDRegistryResult camd_registry_retire(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease)
{
    return camd_registry_set_state(registry, provider_lease,
                                   CAMD_ENDPOINT_RETIRING);
}

enum CAMDRegistryResult camd_registry_release(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 lease)
{
    struct endpoint_slot *slot;
    enum CAMDRegistryResult result;

    if (!registry)
        return CAMD_REGISTRY_INVALID;
    core_lock_acquire(&registry->lock);
    slot = resolve(registry, lease);
    if (!slot)
        result = CAMD_REGISTRY_STALE;
    else if (slot->references == 0)
        result = CAMD_REGISTRY_INVALID;
    else if (slot->references == 1 &&
             slot->endpoint.State == CAMD_ENDPOINT_RETIRING &&
             !generation_available(registry))
        result = CAMD_REGISTRY_RANGE;
    else {
        --slot->references;
        if (slot->references == 0 &&
            slot->endpoint.State == CAMD_ENDPOINT_RETIRING) {
            free_topology(slot);
            memset(&slot->endpoint, 0, sizeof(slot->endpoint));
            slot->occupied = 0;
            advance_generation(registry);
        }
        result = CAMD_REGISTRY_OK;
    }
    core_lock_release(&registry->lock);
    return result;
}

enum CAMDRegistryResult camd_registry_snapshot(
    struct CAMDEndpointRegistry *registry,
    struct CAMDEndpointSnapshot **snapshot_out)
{
    struct CAMDEndpointSnapshot *snapshot;
    size_t i, capacity;

    if (!registry || !snapshot_out)
        return CAMD_REGISTRY_INVALID;
    core_lock_acquire(&registry->lock);
    capacity = registry->slot_count;
    snapshot = core_alloc(sizeof(*snapshot), 1);
    if (!snapshot) {
        core_lock_release(&registry->lock);
        return CAMD_REGISTRY_NOMEM;
    }
    if (capacity) {
        if (capacity > SIZE_MAX / sizeof(*snapshot->endpoints)) {
            core_free(snapshot);
            core_lock_release(&registry->lock);
            return CAMD_REGISTRY_RANGE;
        }
        snapshot->endpoints = core_alloc(
            capacity * sizeof(*snapshot->endpoints), 1);
        if (!snapshot->endpoints) {
            core_free(snapshot);
            core_lock_release(&registry->lock);
            return CAMD_REGISTRY_NOMEM;
        }
    }
    snapshot->generation = registry->generation;
    for (i = 0; i < registry->slot_count; ++i) {
        struct endpoint_slot *slot = &registry->slots[i];
        struct snapshot_endpoint *copy;
        if (!slot->occupied)
            continue;
        copy = &snapshot->endpoints[snapshot->endpoint_count];
        copy->endpoint = slot->endpoint;
        copy->group_count = slot->group_count;
        copy->block_count = slot->block_count;
        if (copy->group_count) {
            copy->groups = core_alloc(
                copy->group_count * sizeof(*copy->groups), 0);
            if (!copy->groups)
                goto no_memory;
            memcpy(copy->groups, slot->groups,
                   copy->group_count * sizeof(*copy->groups));
        }
        if (copy->block_count) {
            copy->blocks = core_alloc(
                copy->block_count * sizeof(*copy->blocks), 0);
            if (!copy->blocks) {
                core_free(copy->groups);
                copy->groups = NULL;
                goto no_memory;
            }
            memcpy(copy->blocks, slot->blocks,
                   copy->block_count * sizeof(*copy->blocks));
        }
        ++snapshot->endpoint_count;
    }
    *snapshot_out = snapshot;
    core_lock_release(&registry->lock);
    return CAMD_REGISTRY_OK;

no_memory:
    core_lock_release(&registry->lock);
    camd_snapshot_destroy(snapshot);
    return CAMD_REGISTRY_NOMEM;
}

void camd_snapshot_destroy(struct CAMDEndpointSnapshot *snapshot)
{
    size_t i;

    if (!snapshot)
        return;
    for (i = 0; i < snapshot->endpoint_count; ++i) {
        core_free(snapshot->endpoints[i].groups);
        core_free(snapshot->endpoints[i].blocks);
    }
    core_free(snapshot->endpoints);
    core_free(snapshot);
}

struct CAMDGenerationV1 camd_snapshot_generation(
    const struct CAMDEndpointSnapshot *snapshot)
{
    struct CAMDGenerationV1 zero = { 0, 0 };
    return snapshot ? snapshot->generation : zero;
}

size_t camd_snapshot_endpoint_count(const struct CAMDEndpointSnapshot *snapshot)
{
    return snapshot ? snapshot->endpoint_count : 0;
}

enum CAMDRegistryResult camd_snapshot_endpoint(
    const struct CAMDEndpointSnapshot *snapshot,
    size_t index,
    const struct CAMDEndpointInfoV1 **endpoint,
    const struct CAMDGroupInfoV1 **groups,
    size_t *group_count,
    const struct CAMDFunctionBlockInfoV1 **blocks,
    size_t *block_count)
{
    const struct snapshot_endpoint *item;

    if (!snapshot || index >= snapshot->endpoint_count || !endpoint ||
        !groups || !group_count || !blocks || !block_count)
        return CAMD_REGISTRY_RANGE;
    item = &snapshot->endpoints[index];
    *endpoint = &item->endpoint;
    *groups = item->groups;
    *group_count = item->group_count;
    *blocks = item->blocks;
    *block_count = item->block_count;
    return CAMD_REGISTRY_OK;
}
