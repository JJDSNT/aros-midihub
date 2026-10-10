#include "../prototypes/camd/legacy_port_refs.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    struct CAMDLegacyPortRefs refs;
    enum CAMDLegacyPortTransition transition;

    camd_legacy_port_refs_init(&refs);
    assert(!camd_legacy_port_refs_in_use(&refs));

    /* A failed physical open is rolled back by applying the inverse logical
       mutation; no owner or phantom open remains. */
    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, 1, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_OPEN);
    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, 0, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_CLOSE);
    assert(!camd_legacy_port_refs_in_use(&refs));
    assert(camd_legacy_port_refs_acquire_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_OPEN);
    assert(camd_legacy_port_refs_release_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_CLOSE);
    assert(!camd_legacy_port_refs_in_use(&refs));

    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, 1, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_OPEN);
    assert(camd_legacy_port_refs_acquire_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, 0, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_release_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_CLOSE);

    assert(camd_legacy_port_refs_acquire_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_OPEN);
    assert(camd_legacy_port_refs_acquire_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, 1, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_release_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_release_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_NONE);
    assert(camd_legacy_port_refs_set_legacy(
               &refs, CAMD_PROVIDER_DIRECTION_OUTPUT, 0, &transition) ==
           CAMD_LEGACY_PORT_REF_OK);
    assert(transition == CAMD_LEGACY_PORT_TRANSITION_CLOSE);

    assert(camd_legacy_port_refs_release_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_STATE);
    refs.EndpointInputUsers = UINT32_MAX;
    assert(camd_legacy_port_refs_acquire_endpoint(
               &refs, CAMD_PROVIDER_DIRECTION_INPUT, &transition) ==
           CAMD_LEGACY_PORT_REF_OVERFLOW);
    assert(refs.EndpointInputUsers == UINT32_MAX);
    puts("CAMD legacy port references OK");
    return 0;
}
