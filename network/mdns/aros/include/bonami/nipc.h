#ifndef BONAMI_NIPC_H
#define BONAMI_NIPC_H
/*------------------------------------------------------------------------*/
/*
 * nipc.h - Envoy / nipc.library integration (bonami.library)
 *
 * These entry points adapt NIPCInquiryA tag lists and public entities to mDNS.
 * nipc.library calls them at runtime (dual stack); application code normally
 * uses NIPCInquiryA only.
 *
 * LVO offsets must match SDK/FD/bonami_lib.fd (bias 30, -6 per slot).
 */
/*------------------------------------------------------------------------*/

#include <exec/types.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

#define BONAMI_LVO_INQUIRY      102
#define BONAMI_LVO_PUBLISH      108
#define BONAMI_LVO_UNPUBLISH    114

#define MDNS_NIPC_Base          (TAG_USER + 0xB3020)

/*
 * PublishNIPCEntitiesA() tags. The host is announced as one instance of
 * MDNS_TYPE_ENVOY_NIPC, named after itself, whose TXT record lists what it
 * offers. A second call from the same task replaces the first.
 */
#define MDNS_NIPC_Entity        (MDNS_NIPC_Base + 1)   /* STRPTR, may repeat */
#define MDNS_NIPC_Service       (MDNS_NIPC_Base + 2)   /* STRPTR, may repeat */
#define MDNS_NIPC_Owner         (MDNS_NIPC_Base + 3)   /* STRPTR */
#define MDNS_NIPC_Port          (MDNS_NIPC_Base + 4)   /* UWORD, default 376 */

#define MDNS_NIPC_DEFAULT_PORT  376

/*------------------------------------------------------------------------*/
/* Function prototypes: clib/bonami_protos.h (via proto/bonami.h) */

/*------------------------------------------------------------------------*/

#endif /* BONAMI_NIPC_H */
