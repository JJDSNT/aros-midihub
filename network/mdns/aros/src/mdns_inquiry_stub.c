/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 Jaime Dias
 *
 * AROS now has public Envoy NIPC headers and a native nipc.library. Keep
 * Bonami's ABI slots available while the optional discovery bridge remains
 * deliberately separate from the core mDNS build and awaits validation.
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
