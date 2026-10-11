/*
 * bonami_internal.h - bonami.library private state
 */

#ifndef BONAMI_PRIVATE_BONAMI_INTERNAL_H
#define BONAMI_PRIVATE_BONAMI_INTERNAL_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <utility/tagitem.h>
#include <libraries/bonamibase.h>

struct MDNSIPCMsg;

/*
 * The library holds no mDNS state: every call is a message to mdnsd. It only
 * counts how often each task has it open, so that a task's last close can
 * tell mdnsd to drop what the task left behind.
 */
struct BonamiPrivate
{
    struct BonamiBase       ba;    /* MUST be first: &ba == library base */
    struct SignalSemaphore  Lock;
    struct MinList          Openers;
};

LONG MDNSEnsureEngine(struct BonamiPrivate *pb);
LONG MDNSSendIPC(struct BonamiPrivate *pb, struct MDNSIPCMsg *msg);
LONG MDNSTagsToRegisterMsg(struct TagItem *tags, struct MDNSIPCMsg *msg);
void MDNSReleaseTask(struct BonamiPrivate *pb);

#endif /* BONAMI_PRIVATE_BONAMI_INTERNAL_H */
