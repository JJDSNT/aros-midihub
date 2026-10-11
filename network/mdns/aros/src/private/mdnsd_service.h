/*
 * mdnsd_service.h - mdnsd's place in the stack's service registry
 */

#ifndef BONAMI_PRIVATE_MDNSD_SERVICE_H
#define BONAMI_PRIVATE_MDNSD_SERVICE_H

#include <exec/types.h>

/* Signal masks; zero where the registry does not signal. */
struct MDNSDSignals
{
    ULONG ms_Stop;      /* the stack is shutting down */
    ULONG ms_Begin;     /* reconfiguration starts: let go of the stack */
    ULONG ms_End;       /* reconfiguration is over: take it up again */
};

BOOL mdnsdServiceOpen(struct MDNSDSignals *sig);
void mdnsdServiceClose(void);

#endif /* BONAMI_PRIVATE_MDNSD_SERVICE_H */
