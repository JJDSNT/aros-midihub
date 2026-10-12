/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * mdns_inquiry.c - Envoy inquiries answered from DNS-SD
 *
 * A host that offers Envoy entities or services announces one instance of
 * MDNS_TYPE_ENVOY_NIPC, named after the host, whose TXT record lists them:
 * "entity=<name>", "service=<name>", "owner=<name>". An inquiry is answered
 * once for each such host, with a tag list in the shape nipc.library's
 * NIPCInquiryA() hands to its hook.
 *
 * The tag values are those of <envoy/nipc.h>. They are repeated here so that
 * mdnsd builds without the Envoy headers.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/utility.h>

#include <bonami/mdns.h>
#include <bonami/nipc.h>
#include "private/mdns_engine.h"
#include "private/mdns_dns.h"
#include "private/bonami_ipc.h"

#define NIPC_QUERY_IPADDR       (TAG_USER + 0x2000)
#define NIPC_QUERY_HOSTNAME     (TAG_USER + 0x2004)
#define NIPC_QUERY_SERVICE      (TAG_USER + 0x2006)
#define NIPC_QUERY_ENTITY       (TAG_USER + 0x2008)
#define NIPC_QUERY_OWNER        (TAG_USER + 0x200A)

#define NIPC_TXT_ITEMS          24

/* The value of "key=value" if item starts with key, else NULL. */
static const char *
txt_value(const char *item, const char *key)
{
    size_t len = strlen(key);

    if (Strnicmp((STRPTR)item, (STRPTR)key, (LONG)len) == 0 && item[len] == '=') {
        return item + len + 1;
    }
    return NULL;
}

/* Splits a copy of the text into its items, in place, taking the
 * backslashes out (an item only gets shorter); returns their number. */
static LONG
txt_split(char *text, char **items, LONG max)
{
    LONG n = 0;
    LONG pos = 0;
    LONG out = 0;

    while (n < max) {
        LONG start = pos;
        LONG len;

        /* where the item's own bytes begin, to read it before writing */
        while (text[start] == ' ') {
            start++;
        }
        len = mdnsTxtNext(text, &pos, (UBYTE *)text + out,
                          (LONG)strlen(text + start));
        if (len < 0) {
            break;
        }
        items[n++] = text + out;
        out += len;
        if (text[pos] != '\0') {
            pos++;
        }
        text[out++] = '\0';
    }
    return n;
}

static BOOL
has_value(char **items, LONG n, const char *key, const char *wanted)
{
    const char *value;
    LONG i;

    for (i = 0; i < n; i++) {
        value = txt_value(items[i], key);
        if (value != NULL && Stricmp((STRPTR)value, (STRPTR)wanted) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void
inquiry_end(struct MDNSInquirySession *is, BOOL callHook)
{
    Remove(&is->is_Node);
    if (callHook) {
        CallHookPkt(is->is_Hook, is->is_Task, NULL);
    }
    FreeMem(is, sizeof(*is));
}

/*
 * Called for every instance that is new or has changed, for every inquiry.
 * May end the inquiry it is given.
 */
void
mdnsInquiryFireNIPC(struct MDNSEngineToken *tok, struct MDNSInquirySession *is,
    struct MDNSCacheEntry *ce)
{
    struct TagItem tags[2 * NIPC_TXT_ITEMS + 4];
    char text[MDNS_MAX_TXT + 1];
    char host[MDNS_MAX_HOST + 1];
    char *items[NIPC_TXT_ITEMS];
    const char *value;
    char *dot;
    LONG n, i, out = 0;
    ULONG k;

    (void)tok;

    /* Only a host whose records are all here can be matched and reported. */
    if (is == NULL || ce == NULL || is->is_Hook == NULL ||
        Stricmp(ce->ce_Type, MDNS_TYPE_ENVOY_NIPC) != 0 ||
        ce->ce_Host[0] == '\0' || ce->ce_HostAddr == 0) {
        return;
    }
    for (k = 0; k < is->is_AnsweredCount; k++) {
        if (Stricmp(is->is_Answered[k], ce->ce_Instance) == 0) {
            return;
        }
    }

    strcpy(text, ce->ce_TXT);
    n = txt_split(text, items, NIPC_TXT_ITEMS);
    strncpy(host, ce->ce_Host, MDNS_MAX_HOST);
    host[MDNS_MAX_HOST] = '\0';
    if ((dot = strchr(host, '.')) != NULL) {
        *dot = '\0';            /* Envoy knows the host without ".local" */
    }

    if ((is->is_MatchEntity[0] != '\0' &&
            !has_value(items, n, MDNS_TXTKEY_ENTITY, is->is_MatchEntity)) ||
        (is->is_MatchService[0] != '\0' &&
            !has_value(items, n, MDNS_TXTKEY_SERVICE, is->is_MatchService)) ||
        (is->is_MatchOwner[0] != '\0' &&
            !has_value(items, n, MDNS_TXTKEY_OWNER, is->is_MatchOwner)) ||
        (is->is_MatchHost[0] != '\0' &&
            Stricmp(is->is_MatchHost, host) != 0) ||
        (is->is_MatchAddr != 0 && is->is_MatchAddr != ce->ce_HostAddr)) {
        return;
    }

    if (is->is_Want & MDNS_NIPC_WANT_HOSTNAME) {
        tags[out].ti_Tag = NIPC_QUERY_HOSTNAME;
        tags[out++].ti_Data = (IPTR)host;
    }
    if (is->is_Want & MDNS_NIPC_WANT_IPADDR) {
        tags[out].ti_Tag = NIPC_QUERY_IPADDR;
        tags[out++].ti_Data = ce->ce_HostAddr;
    }
    for (i = 0; i < n; i++) {
        if ((is->is_Want & MDNS_NIPC_WANT_ENTITY) &&
            (value = txt_value(items[i], MDNS_TXTKEY_ENTITY)) != NULL) {
            tags[out].ti_Tag = NIPC_QUERY_ENTITY;
            tags[out++].ti_Data = (IPTR)value;
        } else if ((is->is_Want & MDNS_NIPC_WANT_SERVICE) &&
            (value = txt_value(items[i], MDNS_TXTKEY_SERVICE)) != NULL) {
            tags[out].ti_Tag = NIPC_QUERY_SERVICE;
            tags[out++].ti_Data = (IPTR)value;
        } else if ((is->is_Want & MDNS_NIPC_WANT_OWNER) &&
            (value = txt_value(items[i], MDNS_TXTKEY_OWNER)) != NULL) {
            tags[out].ti_Tag = NIPC_QUERY_OWNER;
            tags[out++].ti_Data = (IPTR)value;
        }
    }
    tags[out].ti_Tag = TAG_DONE;
    tags[out].ti_Data = 0;

    if (is->is_AnsweredCount < MDNS_NIPC_ANSWERED_MAX) {
        strcpy(is->is_Answered[is->is_AnsweredCount++], ce->ce_Instance);
    }
    is->is_Responses++;

    /* As with NIPCInquiryA(): a hook that returns zero ends the inquiry
     * without a last call; the response limit ends it with one. */
    if (CallHookPkt(is->is_Hook, is->is_Task, tags) == 0) {
        inquiry_end(is, FALSE);
    } else if (is->is_Responses >= is->is_MaxResponses) {
        inquiry_end(is, TRUE);
    }
}

LONG
MDNSEngineInquiryNIPC(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg)
{
    struct MDNSInquirySession *is;
    struct Node *n;
    struct Node *next;
    APTR ref;

    if (tok == NULL || msg == NULL || msg->bim_Hook == NULL) {
        return MDNSERR_BADPARAM;
    }

    /* No time: the oldest inquiry with this hook is given up. */
    if (msg->bim_Flags == 0) {
        for (n = tok->et_InquirySessions.lh_Head;
            n != NULL && n->ln_Succ != NULL; n = n->ln_Succ) {
            is = (struct MDNSInquirySession *)n;
            if (is->is_Hook == msg->bim_Hook && is->is_Task == msg->bim_Task) {
                inquiry_end(is, TRUE);
                return MDNS_OK;
            }
        }
        return MDNSERR_NOTFOUND;
    }

    is = (struct MDNSInquirySession *)AllocMem(sizeof(*is), MEMF_CLEAR);
    if (is == NULL) {
        return MDNSERR_NOMEM;
    }
    ref = mdnsEngineNextHandle(tok);
    is->is_Ref = ref;
    is->is_Hook = msg->bim_Hook;
    is->is_Task = msg->bim_Task;
    is->is_Expire = mdnsEngineNow() + msg->bim_Flags;
    is->is_MaxResponses = msg->bim_Port != 0 ? msg->bim_Port : 32;
    is->is_Want = msg->bim_Dbg0;
    is->is_MatchAddr = msg->bim_HostAddr;
    strncpy(is->is_MatchEntity, msg->bim_QName, MDNS_MAX_NAME);
    strncpy(is->is_MatchService, msg->bim_Name, MDNS_MAX_NAME);
    strncpy(is->is_MatchOwner, msg->bim_TXT, MDNS_MAX_NAME);
    strncpy(is->is_MatchHost, msg->bim_HostName, MDNS_MAX_HOST);
    is->is_LastQuery = mdnsEngineNow();
    is->is_Interval = 1;
    AddTail(&tok->et_InquirySessions, &is->is_Node);

    mdnsBrowseSendQuery(tok, MDNS_TYPE_ENVOY_NIPC, TRUE);

    /* What is known already; the inquiry may end while it is told. */
    for (n = tok->et_Cache.lh_Head; n != NULL && n->ln_Succ != NULL; n = next) {
        struct Node *s;

        next = n->ln_Succ;
        for (s = tok->et_InquirySessions.lh_Head;
            s != NULL && s->ln_Succ != NULL; s = s->ln_Succ) {
            if (((struct MDNSInquirySession *)s)->is_Ref == ref) {
                break;
            }
        }
        if (s == NULL || s->ln_Succ == NULL) {
            break;
        }
        mdnsInquiryFireNIPC(tok, (struct MDNSInquirySession *)s,
            (struct MDNSCacheEntry *)n);
    }
    return MDNS_OK;
}

/* Once a second: ask again, less and less often, and end what has run out
 * of time. */
void
MDNSEngineInquiryTick(struct MDNSEngineToken *tok)
{
    struct MDNSInquirySession *is;
    struct Node *n;
    struct Node *next;
    ULONG now;

    if (tok == NULL) {
        return;
    }
    now = mdnsEngineNow();
    for (n = tok->et_InquirySessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        is = (struct MDNSInquirySession *)n;
        if (now >= is->is_Expire) {
            inquiry_end(is, TRUE);
        } else if (now - is->is_LastQuery >= is->is_Interval) {
            mdnsBrowseSendQuery(tok, MDNS_TYPE_ENVOY_NIPC, FALSE);
            is->is_LastQuery = now;
            is->is_Interval *= 2;
        }
    }
}

/* A client is gone: its hooks are not called again. */
void
MDNSEngineReleaseInquiries(struct MDNSEngineToken *tok, struct Task *task)
{
    struct Node *n;
    struct Node *next;

    for (n = tok->et_InquirySessions.lh_Head; n != NULL && n->ln_Succ != NULL;
        n = next) {
        next = n->ln_Succ;
        if (((struct MDNSInquirySession *)n)->is_Task == task) {
            inquiry_end((struct MDNSInquirySession *)n, FALSE);
        }
    }
}
