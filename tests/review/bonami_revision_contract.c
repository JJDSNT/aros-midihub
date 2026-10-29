/* Compile-only contract for Bonami's identity and dnssd's record minimum. */
#include <bonami/mdns.h>

#ifndef EXPECT_BONAMI_REVISION
#error define EXPECT_BONAMI_REVISION for the proposal under test
#endif

#define RECORD_API_VERSION  41
#define RECORD_API_REVISION 3

#define HAS_RECORD_API(version, revision) \
    ((version) > RECORD_API_VERSION || \
     ((version) == RECORD_API_VERSION && \
      (revision) >= RECORD_API_REVISION))

#if BONAMI_LIB_VERSION != 41
#error unexpected Bonami major version
#endif
#if BONAMI_LIB_REVISION != EXPECT_BONAMI_REVISION
#error the public identity does not match the implementation revision
#endif
#if HAS_RECORD_API(41, 2)
#error revision 41.2 must not expose the record API
#endif
#if !HAS_RECORD_API(41, 3)
#error revision 41.3 must expose the record API
#endif
#if !HAS_RECORD_API(41, 4)
#error revision 41.4 must retain the record API
#endif
#if !HAS_RECORD_API(42, 0)
#error a later major version must retain the record API
#endif

int bonami_revision_contract;
