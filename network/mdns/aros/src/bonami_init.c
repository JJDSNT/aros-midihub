/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * AROS lifecycle glue for bonami.library.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <aros/symbolsets.h>

#include "private/bonami_internal.h"

struct BonamiOpener
{
    struct MinNode  Node;
    struct Task    *Task;
    ULONG           Count;
};

static struct BonamiOpener *find_opener(struct BonamiPrivate *base)
{
    struct Task *me = FindTask(NULL);
    struct BonamiOpener *opener;

    for (opener = (struct BonamiOpener *)base->Openers.mlh_Head;
         opener->Node.mln_Succ != NULL;
         opener = (struct BonamiOpener *)opener->Node.mln_Succ) {
        if (opener->Task == me)
            return opener;
    }
    return NULL;
}

static int BonamiInit(struct BonamiPrivate *base)
{
    InitSemaphore(&base->Lock);
    NEWLIST(&base->Openers);
    return TRUE;
}

static int BonamiOpen(struct BonamiPrivate *base)
{
    struct BonamiOpener *opener;

    ObtainSemaphore(&base->Lock);
    opener = find_opener(base);
    if (opener == NULL) {
        opener = AllocMem(sizeof(*opener), MEMF_CLEAR);
        if (opener != NULL) {
            opener->Task = FindTask(NULL);
            AddTail((struct List *)&base->Openers, (struct Node *)opener);
        }
    }
    if (opener != NULL)
        ++opener->Count;
    ReleaseSemaphore(&base->Lock);
    return opener != NULL;
}

/* With a task's last close, mdnsd drops what the task registered and
 * browsed. A task that ends without closing is not noticed. */
static int BonamiClose(struct BonamiPrivate *base)
{
    struct BonamiOpener *opener;
    BOOL last = FALSE;

    ObtainSemaphore(&base->Lock);
    opener = find_opener(base);
    if (opener != NULL && --opener->Count == 0) {
        Remove((struct Node *)opener);
        FreeMem(opener, sizeof(*opener));
        last = TRUE;
    }
    ReleaseSemaphore(&base->Lock);
    if (last)
        MDNSReleaseTask(base);
    return TRUE;
}

static int BonamiExpunge(struct BonamiPrivate *base)
{
    return IsListEmpty((struct List *)&base->Openers);
}

ADD2INITLIB(BonamiInit, 0);
ADD2OPENLIB(BonamiOpen, 0);
ADD2CLOSELIB(BonamiClose, 0);
ADD2EXPUNGELIB(BonamiExpunge, 0);
