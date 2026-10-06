/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 Jaime Dias
 *
 * AROS currently has no public Envoy NIPC headers. Keep Bonami's ABI slots
 * available while excluding the optional NIPC bridge from the native build.
 */

#include <exec/types.h>
#include <bonami/mdns.h>

#include "private/mdns_engine.h"
#include "private/bonami_ipc.h"

void mdnsInquiryFireNIPC(struct MDNSEngineToken *tok,
    struct MDNSInquirySession *session, struct MDNSCacheEntry *entry)
{
    (void)tok;
    (void)session;
    (void)entry;
}

LONG MDNSEngineInquiryNIPC(struct MDNSEngineToken *tok,
    struct MDNSIPCMsg *msg)
{
    (void)tok;
    (void)msg;
    return MDNSERR_NOTREADY;
}

void MDNSEngineInquiryTick(struct MDNSEngineToken *tok)
{
    (void)tok;
}
