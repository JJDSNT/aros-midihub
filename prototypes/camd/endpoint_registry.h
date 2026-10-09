#ifndef CAMD_ENDPOINT_REGISTRY_H
#define CAMD_ENDPOINT_REGISTRY_H

/*
 * Executable model of CAMD's private endpoint registry.
 *
 * This is deliberately not installed as a public header.  It validates the
 * data and lifetime invariants that must precede the version 43 ABI review.
 */

#include <stddef.h>
#include <stdint.h>

#define CAMD_ENDPOINT_NAME_BYTES       128u
#define CAMD_ENDPOINT_PRODUCT_BYTES    128u
#define CAMD_ENDPOINT_TRANSPORT_BYTES   32u
#define CAMD_TOPOLOGY_NAME_BYTES        64u

struct CAMDHandleV1 {
    uint32_t slot;
    uint32_t generation;
};

struct CAMDEndpointIDV1 {
    uint32_t word[4];
};

struct CAMDGenerationV1 {
    uint32_t high;
    uint32_t low;
};

enum CAMDEndpointStateV1 {
    CAMD_ENDPOINT_REGISTERED = 1,
    CAMD_ENDPOINT_DISCOVERING,
    CAMD_ENDPOINT_AVAILABLE,
    CAMD_ENDPOINT_OFFLINE,
    CAMD_ENDPOINT_RETIRING,
    CAMD_ENDPOINT_RETIRED
};

struct CAMDEndpointInfoV1 {
    uint32_t Size;
    uint32_t Version;
    struct CAMDEndpointIDV1 ID;
    struct CAMDEndpointIDV1 ProviderID;
    uint32_t State;
    uint32_t Flags;
    uint32_t IdentityKind;
    uint32_t NativeDataFormat;
    uint32_t ProtocolCapabilities;
    uint32_t CurrentProtocol;
    struct CAMDGenerationV1 Generation;
    char Name[CAMD_ENDPOINT_NAME_BYTES];
    char ProductInstance[CAMD_ENDPOINT_PRODUCT_BYTES];
    char Transport[CAMD_ENDPOINT_TRANSPORT_BYTES];
};

struct CAMDGroupInfoV1 {
    uint32_t Size;
    uint32_t Version;
    struct CAMDEndpointIDV1 EndpointID;
    uint32_t Group;
    uint32_t Flags;
    uint32_t Protocol;
    char Name[CAMD_TOPOLOGY_NAME_BYTES];
};

struct CAMDFunctionBlockInfoV1 {
    uint32_t Size;
    uint32_t Version;
    struct CAMDEndpointIDV1 EndpointID;
    uint32_t Number;
    uint32_t Flags;
    uint32_t FirstGroup;
    uint32_t GroupCount;
    char Name[CAMD_TOPOLOGY_NAME_BYTES];
};

enum CAMDRegistryResult {
    CAMD_REGISTRY_OK = 0,
    CAMD_REGISTRY_INVALID,
    CAMD_REGISTRY_NOMEM,
    CAMD_REGISTRY_DUPLICATE,
    CAMD_REGISTRY_STALE,
    CAMD_REGISTRY_STATE,
    CAMD_REGISTRY_RETIRED,
    CAMD_REGISTRY_RANGE
};

struct CAMDEndpointRegistry;
struct CAMDEndpointSnapshot;

/* Registry operations serialize themselves.  Destroy still requires that no
 * operation is in flight.  Snapshot access is independent after
 * camd_registry_snapshot() returns. */

struct CAMDEndpointRegistry *camd_registry_create(void);
void camd_registry_destroy(struct CAMDEndpointRegistry *registry);

enum CAMDRegistryResult camd_registry_publish(
    struct CAMDEndpointRegistry *registry,
    const struct CAMDEndpointInfoV1 *endpoint,
    const struct CAMDGroupInfoV1 *groups,
    size_t group_count,
    const struct CAMDFunctionBlockInfoV1 *blocks,
    size_t block_count,
    struct CAMDHandleV1 *provider_lease);

enum CAMDRegistryResult camd_registry_replace(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease,
    const struct CAMDEndpointInfoV1 *endpoint,
    const struct CAMDGroupInfoV1 *groups,
    size_t group_count,
    const struct CAMDFunctionBlockInfoV1 *blocks,
    size_t block_count);

enum CAMDRegistryResult camd_registry_set_state(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease,
    uint32_t state);

enum CAMDRegistryResult camd_registry_acquire(
    struct CAMDEndpointRegistry *registry,
    const struct CAMDEndpointIDV1 *id,
    struct CAMDHandleV1 *lease);

enum CAMDRegistryResult camd_registry_retire(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 provider_lease);

enum CAMDRegistryResult camd_registry_release(
    struct CAMDEndpointRegistry *registry,
    struct CAMDHandleV1 lease);

enum CAMDRegistryResult camd_registry_snapshot(
    struct CAMDEndpointRegistry *registry,
    struct CAMDEndpointSnapshot **snapshot);

void camd_snapshot_destroy(struct CAMDEndpointSnapshot *snapshot);
struct CAMDGenerationV1 camd_snapshot_generation(
    const struct CAMDEndpointSnapshot *snapshot);
size_t camd_snapshot_endpoint_count(const struct CAMDEndpointSnapshot *snapshot);

enum CAMDRegistryResult camd_snapshot_endpoint(
    const struct CAMDEndpointSnapshot *snapshot,
    size_t index,
    const struct CAMDEndpointInfoV1 **endpoint,
    const struct CAMDGroupInfoV1 **groups,
    size_t *group_count,
    const struct CAMDFunctionBlockInfoV1 **blocks,
    size_t *block_count);

#endif
