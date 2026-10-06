/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * AROS lifecycle glue for the Bonami mDNS engine.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <aros/symbolsets.h>

#include "private/bonami_internal.h"

static int BonamiInit(struct BonamiPrivate *base)
{
    base->EngineToken = NULL;
    base->EngineOpen = FALSE;
    InitSemaphore(&base->Lock);
    return TRUE;
}

static int BonamiExpunge(struct BonamiPrivate *base)
{
    MDNSTokenDetach(base);
    return TRUE;
}

ADD2INITLIB(BonamiInit, 0);
ADD2EXPUNGELIB(BonamiExpunge, 0);
