/*
 * bonami_internal.h - bonami.library private state
 */

#ifndef BONAMI_PRIVATE_BONAMI_INTERNAL_H
#define BONAMI_PRIVATE_BONAMI_INTERNAL_H

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <libraries/bonamibase.h>

struct MDNSEngineToken;
struct MDNSIPCMsg;

/*
 * The public Library node must come FIRST: OpenLibrary() returns &ba, which is
 * the library base.  Under RTF_AUTOINIT the private fields live in the positive
 * region immediately after the Library node (it_LibSize == sizeof this struct;
 * MakeLibrary allocates lib_NegSize vectors + lib_PosSize data).
 *
 * The old layout put the private fields *before* ba and derived the private
 * pointer as base - lib_NegSize.  lib_NegSize is the size of the LVO jump table
 * (set by MakeLibrary), not offsetof(ba), so that pointer aliased the jump
 * table and every write through it corrupted LVO vectors -> random
 * "jump to illegal instruction" crashes.
 */
struct BonamiPrivate
{
    struct BonamiBase       ba;    /* MUST be first: &ba == library base */
    ULONG                   SegList;
    struct MDNSEngineToken *EngineToken;
    struct SignalSemaphore  Lock;
    BOOL                    EngineOpen;
};

#define BonamiGetPrivate(b) ((struct BonamiPrivate *)(b))

int  MDNSTokenAttach(struct BonamiPrivate *pb);
void MDNSTokenDetach(struct BonamiPrivate *pb);

LONG MDNSEnsureEngine(struct BonamiPrivate *pb);
LONG MDNSSendIPC(struct BonamiPrivate *pb, struct MDNSIPCMsg *msg);
LONG MDNSTagsToRegisterMsg(struct TagItem *tags, struct MDNSIPCMsg *msg);

#endif /* BONAMI_PRIVATE_BONAMI_INTERNAL_H */
